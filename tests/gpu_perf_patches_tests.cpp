// Crackdown's GPU patches in this port: the register ranges without write side effects (0048), the host thread
// affinity (0049) and the signed scissor compare (0050). The predicates are checked on the SDK's own register
// numbers, the wiring on the patch text, and, when the patched SDK source is in out/rexglue-sdk-src, the claim they
// rest on: which registers CommandProcessor::WriteRegister treats specially.
// Run: hcu_gpu_perf_patches_tests

#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <rex/graphics/register_file.h>

using namespace rex::graphics;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// The predicate of patches/rexglue-sdk/0048-gpu-side-effect-free-register-ranges.patch.
bool IsSideEffectFreeRegisterRange(uint32_t first_index, uint32_t last_index) {
  static_assert(XE_GPU_REG_DC_LUT_30_COLOR == XE_GPU_REG_DC_LUT_RW_INDEX + 3);
  auto overlaps = [&](uint32_t first, uint32_t last) { return first_index <= last && last_index >= first; };
  return first_index <= last_index && last_index < RegisterFile::kRegisterCount &&
         !overlaps(XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_LOOP_31) &&
         !overlaps(XE_GPU_REG_SCRATCH_REG0, XE_GPU_REG_SCRATCH_REG7) &&
         !overlaps(XE_GPU_REG_COHER_STATUS_HOST, XE_GPU_REG_COHER_STATUS_HOST) &&
         !overlaps(XE_GPU_REG_DC_LUT_RW_INDEX, XE_GPU_REG_DC_LUT_30_COLOR);
}

// The affinity mask of patches/rexglue-sdk/0049-d3d12-host-threads-avoid-cpu0.windows.patch.
uint64_t HostThreadAffinityMask(uint64_t process_mask, bool enabled) {
  if (!enabled || std::popcount(process_mask) < 8) {
    return 0;
  }
  return process_mask & ~uint64_t(0b11);
}

void TestRegisterRanges() {
  std::printf("register ranges without write side effects\n");
  CHECK(IsSideEffectFreeRegisterRange(0x2000, 0x2100));
  CHECK(IsSideEffectFreeRegisterRange(0x0000, 0x0010));
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_SHADER_CONSTANT_LOOP_31 + 1, RegisterFile::kRegisterCount - 1));

  // Each register with a side effect, alone and inside a longer run.
  const uint32_t special[] = {XE_GPU_REG_SCRATCH_REG0,       XE_GPU_REG_SCRATCH_REG7,    XE_GPU_REG_COHER_STATUS_HOST,
                              XE_GPU_REG_DC_LUT_RW_INDEX,    XE_GPU_REG_DC_LUT_SEQ_COLOR, XE_GPU_REG_DC_LUT_PWL_DATA,
                              XE_GPU_REG_DC_LUT_30_COLOR,    XE_GPU_REG_SHADER_CONSTANT_000_X,
                              XE_GPU_REG_SHADER_CONSTANT_511_W, XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0,
                              XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5, XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031,
                              XE_GPU_REG_SHADER_CONSTANT_LOOP_31};
  for (const uint32_t index : special) {
    CHECK(!IsSideEffectFreeRegisterRange(index, index));
    CHECK(!IsSideEffectFreeRegisterRange(index - 2, index + 2));
    CHECK(!IsSideEffectFreeRegisterRange(index, index + 3));
  }
  // The gamma ramp ports sit between the first and last of the four.
  CHECK(XE_GPU_REG_DC_LUT_SEQ_COLOR > XE_GPU_REG_DC_LUT_RW_INDEX && XE_GPU_REG_DC_LUT_SEQ_COLOR < XE_GPU_REG_DC_LUT_30_COLOR);
  CHECK(XE_GPU_REG_DC_LUT_PWL_DATA > XE_GPU_REG_DC_LUT_RW_INDEX && XE_GPU_REG_DC_LUT_PWL_DATA < XE_GPU_REG_DC_LUT_30_COLOR);

  // Runs next to the special ones are still plain.
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_SCRATCH_REG7 + 1, XE_GPU_REG_SCRATCH_REG7 + 4));
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_DC_LUT_30_COLOR + 1, XE_GPU_REG_DC_LUT_30_COLOR + 5));
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_COHER_STATUS_HOST + 1, XE_GPU_REG_COHER_STATUS_HOST + 3));

  // Empty, reversed and out-of-file runs are never block copies.
  CHECK(!IsSideEffectFreeRegisterRange(10, 9));
  CHECK(!IsSideEffectFreeRegisterRange(RegisterFile::kRegisterCount - 2, RegisterFile::kRegisterCount));
  CHECK(!IsSideEffectFreeRegisterRange(0, 0xFFFFFFFFu));
}

void TestWriteRegisterClaim() {
  std::printf("WriteRegister only treats the four groups specially\n");
  const std::string source = ReadFile(std::string(HCU_SRC_DIR) + "/../out/rexglue-sdk-src/src/graphics/command_processor.cpp");
  if (source.empty()) {
    std::printf("  skipped: patched SDK source not present\n");
    return;
  }
  const size_t begin = source.find("void CommandProcessor::WriteRegister(uint32_t index, uint32_t value)");
  const size_t end = source.find("\nvoid CommandProcessor::WriteRegistersFromMem", begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  const std::string body = source.substr(begin, end - begin);

  std::set<std::string> cases;
  for (size_t at = body.find("case XE_GPU_REG_"); at != std::string::npos; at = body.find("case XE_GPU_REG_", at + 1)) {
    const size_t name_start = at + 5;
    const size_t name_end = body.find(':', name_start);
    cases.insert(body.substr(name_start, name_end - name_start));
  }
  const std::set<std::string> expected = {"XE_GPU_REG_COHER_STATUS_HOST", "XE_GPU_REG_DC_LUT_RW_INDEX",
                                          "XE_GPU_REG_DC_LUT_SEQ_COLOR", "XE_GPU_REG_DC_LUT_PWL_DATA",
                                          "XE_GPU_REG_DC_LUT_30_COLOR"};
  CHECK(cases == expected);
  CHECK(Has(body, "index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7"));
  // Anything else the function does is the plain store and the unknown-register log.
  CHECK(!Has(body, "XE_GPU_REG_SHADER_CONSTANT"));
}

void TestHostThreadMask() {
  std::printf("host thread affinity\n");
  CHECK(HostThreadAffinityMask(0xFF, true) == 0xFC);
  CHECK(HostThreadAffinityMask(0xFFFF, true) == 0xFFFC);
  CHECK(HostThreadAffinityMask(0xF0F0, true) == 0xF0F0);
  CHECK(HostThreadAffinityMask(0x7F, true) == 0);
  CHECK(HostThreadAffinityMask(0xF, true) == 0);
  CHECK(HostThreadAffinityMask(0, true) == 0);
  CHECK(HostThreadAffinityMask(0xFFFF, false) == 0);
  CHECK(HostThreadAffinityMask(0xFFFFFFFFFFFFFFFFull, true) == 0xFFFFFFFFFFFFFFFCull);
}

void TestPatchWiring() {
  std::printf("patch wiring\n");
  const std::string dir = std::string(HCU_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string registers = ReadFile(dir + "0048-gpu-side-effect-free-register-ranges.patch");
  CHECK(Has(registers, "+bool IsSideEffectFreeRegisterRange(uint32_t first_index, uint32_t last_index) {"));
  CHECK(Has(registers, "+  static_assert(XE_GPU_REG_DC_LUT_30_COLOR == XE_GPU_REG_DC_LUT_RW_INDEX + 3);"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_LOOP_31)"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_SCRATCH_REG0, XE_GPU_REG_SCRATCH_REG7)"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_COHER_STATUS_HOST, XE_GPU_REG_COHER_STATUS_HOST)"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_DC_LUT_RW_INDEX, XE_GPU_REG_DC_LUT_30_COLOR)"));
  CHECK(Has(registers, "+  return IsSideEffectFreeRegisterRange(first_index, last_index) ||"));
  CHECK(Has(registers, "IsSideEffectFreeRegisterRange(start_index, start_index + num_registers - 1)"));
  CHECK(Has(registers, "Unowa001/crackdown-recomp"));

  const std::string threads = ReadFile(dir + "0049-d3d12-host-threads-avoid-cpu0.windows.patch");
  CHECK(Has(threads, "REXCVAR_DEFINE_BOOL(gpu_host_threads_avoid_cpu0, true"));
  CHECK(Has(threads, "std::popcount(uint64_t(process_mask)) < 8"));
  CHECK(Has(threads, "return uint64_t(process_mask) & ~uint64_t(0b11);"));
  CHECK(Has(threads, "command_processor_thread->set_affinity_mask(host_thread_affinity_mask)"));
  CHECK(Has(threads, "async_submission_thread_->set_affinity_mask(host_thread_affinity_mask)"));
  CHECK(Has(threads, "Unowa001/crackdown-recomp"));

  const std::string scissor = ReadFile(dir + "0050-draw-extent-scissor-signed-compare.patch");
  CHECK(Has(scissor, "+        if (scissor_bottom >= int32_t(xenos::kTexture2DCubeMaxWidthHeight)) {"));
  CHECK(Has(scissor, "+          if (scissor_right >= int32_t(xenos::kTexture2DCubeMaxWidthHeight)) {"));

  // The empty-extent guards of 0036 (the Geometry Wars version).
  const std::string scale = ReadFile(dir + "0036-internal-render-scale.patch");
  CHECK(Has(scale, "axis_extent_int ? (axis_1_int * axis_scale_quarters + 3) >> 2 : axis_0_host"));
  CHECK(Has(scale, "scissor.extent[i] = scissor.extent[i] ? end - start : 0;"));
}

void TestShutdownOrder() {
  std::printf("shutdown order\n");
  const std::string dir = std::string(HCU_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string submission = ReadFile(dir + "0044-d3d12-async-submission.windows.patch");
  CHECK(Has(submission, "+  StopAsyncSubmission();"));
  CHECK(Has(submission, "+D3D12CommandProcessor::~D3D12CommandProcessor() {"));
  CHECK(Has(submission, "Stopping, and everything queued has been submitted."));
  CHECK(Has(submission, "rex::thread::Wait(async_submission_thread_.get(), false);"));

  // A WAIT_REG_MEM in an indirect buffer returns false once Shutdown() clears worker_running_;
  // the stock code logs INDIRECT RINGBUFFER for it.
  const std::string waits = ReadFile(dir + "0003-gpu-waits-and-submissions.patch");
  CHECK(Has(waits, "while (worker_running_ && pending_fns_.empty()"));
}

}  // namespace

int main() {
  TestRegisterRanges();
  TestWriteRegisterClaim();
  TestHostThreadMask();
  TestPatchWiring();
  TestShutdownOrder();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
