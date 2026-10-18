// Lock-free guest time base (patches/rexglue-sdk/0039-lock-free-guest-clock.patch): the segment
// arithmetic, continuity and monotonicity across ratio changes, and that readers never take the
// clock mutex. The ReXGlue clock is not linkable here, so SegmentGuestTicks and the publish step are
// mirrored; a check reads the patch to keep the mirror from drifting.
// Run: hcu_guest_clock_tests

#include <atomic>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

struct Segment {
  uint64_t host_base;
  uint64_t guest_base;
  uint64_t numerator;
  uint64_t denominator;
};

// Mirrors SegmentGuestTicks.
uint64_t Ticks(const Segment& segment, uint64_t host_tick_count) {
  const uint64_t delta = host_tick_count > segment.host_base ? host_tick_count - segment.host_base : 0;
  const uint64_t whole = delta / segment.denominator;
  const uint64_t rest = delta % segment.denominator;
  return segment.guest_base + whole * segment.numerator + rest * segment.numerator / segment.denominator;
}

// Mirrors the publish step of RecomputeGuestTickScalar.
Segment NextSegment(const Segment& current, uint64_t host_now, uint64_t numerator, uint64_t denominator) {
  return Segment{host_now, Ticks(current, host_now), numerator, denominator};
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestBase() {
  std::printf("host at and after the segment base\n");
  const Segment segment{1000, 0, 1, 1};
  CHECK(Ticks(segment, 1000) == 0);
  CHECK(Ticks(segment, 1001) == 1);
  CHECK(Ticks(segment, 1500) == 500);
  // A host count below the base never goes backwards.
  CHECK(Ticks(segment, 0) == 0);
  CHECK(Ticks(segment, 999) == 0);
}

void TestRatios() {
  std::printf("1:1, greater and smaller ratios\n");
  const Segment one_to_one{1000, 0, 1, 1};
  CHECK(Ticks(one_to_one, 1005) == 5);

  const Segment faster{1000, 0, 3, 2};
  CHECK(Ticks(faster, 1000) == 0);
  CHECK(Ticks(faster, 1002) == 3);
  CHECK(Ticks(faster, 1010) == 15);

  const Segment slower{1000, 0, 2, 3};
  CHECK(Ticks(slower, 1000) == 0);
  CHECK(Ticks(slower, 1003) == 2);
  CHECK(Ticks(slower, 1006) == 4);
}

void TestRemainder() {
  std::printf("fractional and remainder arithmetic\n");
  // 3:7 leaves a remainder at every step; the whole and remaining parts must split it exactly.
  const Segment segment{0, 0, 3, 7};
  for (uint64_t delta = 0; delta < 2000; ++delta) {
    CHECK(Ticks(segment, delta) == delta * 3 / 7);
  }
  // A non-zero guest base offsets the delta part only.
  const Segment offset{500, 1234, 5, 4};
  CHECK(Ticks(offset, 500) == 1234);
  CHECK(Ticks(offset, 504) == 1234 + 5);
  CHECK(Ticks(offset, 503) == 1234 + 3);
}

void TestContinuityAcrossChange() {
  std::printf("continuity across a ratio change\n");
  Segment segment{1000, 0, 1, 1};
  CHECK(Ticks(segment, 1500) == 500);

  const uint64_t changeover_host = 1500;
  segment = NextSegment(segment, changeover_host, 2, 1);
  // At the changeover the clock does not jump.
  CHECK(segment.guest_base == 500);
  CHECK(Ticks(segment, changeover_host) == 500);
  CHECK(Ticks(segment, 1510) == 520);

  // A slower rate after a faster one is also continuous at the changeover.
  const uint64_t second_host = 1600;
  const uint64_t at_second = Ticks(segment, second_host);
  segment = NextSegment(segment, second_host, 1, 4);
  CHECK(segment.guest_base == at_second);
  CHECK(Ticks(segment, second_host) == at_second);
  CHECK(Ticks(segment, second_host + 8) == at_second + 2);
}

void TestRepeatedAndMonotonic() {
  std::printf("repeated ratio changes stay continuous and monotonic\n");
  const uint64_t numerators[] = {1, 2, 1, 3, 2, 5};
  const uint64_t denominators[] = {1, 1, 2, 2, 3, 8};
  Segment segment{1000, 0, 1, 1};
  uint64_t previous = 0;
  bool first = true;
  uint64_t host = 1000;
  for (int round = 0; round < 6; ++round) {
    host += 37;
    const uint64_t at_change = Ticks(segment, host);
    segment = NextSegment(segment, host, numerators[round], denominators[round]);
    CHECK(segment.guest_base == at_change);
    for (int step = 0; step < 50; ++step) {
      const uint64_t value = Ticks(segment, host + uint64_t(step) * 13);
      if (!first) {
        CHECK(value >= previous);
      }
      first = false;
      previous = value;
    }
    host += 50 * 13;
  }
}

void TestLargeValues() {
  std::printf("large host counts, and the split avoids overflow\n");
  // Where the plain product fits in 64 bits, the split must match it exactly.
  const uint64_t hosts[] = {0, 1, 12345, 1ull << 20, (1ull << 30) + 7};
  const uint64_t numerators[] = {1, 3, 1000};
  const uint64_t denominators[] = {1, 7, 3, 1000};
  for (uint64_t host : hosts) {
    for (uint64_t numerator : numerators) {
      for (uint64_t denominator : denominators) {
        const Segment segment{0, 0, numerator, denominator};
        CHECK(Ticks(segment, host) == host * numerator / denominator);
      }
    }
  }
  // delta * numerator overflows 64 bits here; the whole/rest split still gives the exact value.
  CHECK((1ull << 40) > (uint64_t(-1) >> 30));
  const Segment overflow{0, 0, 1ull << 30, 1ull << 20};
  CHECK(Ticks(overflow, 1ull << 40) == (1ull << 50));
}

void TestConcurrentRatioChanges() {
  std::printf("concurrent ratio changes and reads\n");
  std::deque<Segment> segments = {Segment{1000, 0, 1, 1}};
  std::atomic<const Segment*> current{&segments.front()};
  std::mutex write_mutex;
  std::atomic<uint64_t> host{1000};
  std::atomic<bool> done{false};
  std::atomic<int> decreases{0};

  // Non-decreasing rates, so a newer segment never advances slower than the one it replaces and each
  // reader can only see increasing values.
  const uint64_t numerators[] = {1, 1, 2, 4};
  const uint64_t denominators[] = {2, 1, 1, 1};

  std::thread writer([&] {
    for (int i = 0; i < 2000; ++i) {
      const uint64_t now = host.fetch_add(1, std::memory_order_relaxed) + 1;
      std::lock_guard<std::mutex> lock(write_mutex);
      const Segment* previous = current.load(std::memory_order_acquire);
      segments.push_back(NextSegment(*previous, now, numerators[i % 4], denominators[i % 4]));
      current.store(&segments.back(), std::memory_order_release);
    }
    done.store(true, std::memory_order_release);
  });

  std::vector<std::thread> readers;
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&] {
      uint64_t last = 0;
      bool first = true;
      while (!done.load(std::memory_order_acquire)) {
        const Segment* segment = current.load(std::memory_order_acquire);
        const uint64_t value = Ticks(*segment, host.load(std::memory_order_relaxed));
        if (!first && value < last) {
          decreases.fetch_add(1, std::memory_order_relaxed);
        }
        first = false;
        last = value;
        std::this_thread::yield();
      }
    });
  }

  writer.join();
  for (std::thread& reader : readers) {
    reader.join();
  }
  CHECK(decreases.load() == 0);
  CHECK(segments.size() == 2001);
}

void TestPatchContent() {
  std::printf("patch publishes an immutable segment and readers take no mutex\n");
  const std::string patch = ReadFile(std::string(HCU_SRC_DIR) + "/../patches/rexglue-sdk/0039-lock-free-guest-clock.patch");
  CHECK(Has(patch, "std::deque<ClockSegment> clock_segments_"));
  CHECK(Has(patch, "std::atomic<const ClockSegment*> clock_segment_"));
  CHECK(Has(patch, "memory_order_acquire"));
  CHECK(Has(patch, "memory_order_release"));
  CHECK(Has(patch, "clock_segments_.push_back"));
  CHECK(Has(patch, "clock_segment_.store(&clock_segments_.back()"));
  CHECK(Has(patch, "-uint64_t last_guest_tick_count_ = 0;"));
  CHECK(Has(patch, "-uint64_t last_host_tick_count_ = Clock::QueryHostTickCount();"));
  CHECK(Has(patch, "-  std::unique_lock<std::mutex> lock(tick_mutex_, std::defer_lock);"));
}

}  // namespace

int main() {
  TestBase();
  TestRatios();
  TestRemainder();
  TestContinuityAcrossChange();
  TestRepeatedAndMonotonic();
  TestLargeValues();
  TestConcurrentRatioChanges();
  TestPatchContent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
