// Visual interpolation tests: the transform tracks, the restore log and the render interpolator over a small fake
// guest memory. Objects and the camera are replaced only while a draw runs, snap on every kind of discontinuity and
// are put back afterwards. The hooks need the running game.
// Run: hcu_visual_interpolation_tests

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "simulation_pacer.h"
#include "visual_interpolation.h"

using namespace hcu;

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A guest memory image: addresses are offsets into it.
struct Guest {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x20000, 0);

  uint8_t* base() { return bytes.data(); }

  float Float(uint32_t addr) const { return std::bit_cast<float>(LoadBE32(bytes.data(), addr)); }
  void SetFloat(uint32_t addr, float value) { StoreBE32(bytes.data(), addr, std::bit_cast<uint32_t>(value)); }
  uint32_t Word(uint32_t addr) const { return LoadBE32(bytes.data(), addr); }
  void SetWord(uint32_t addr, uint32_t value) { StoreBE32(bytes.data(), addr, value); }
};

constexpr uint32_t kFrustumPointer = 0x100;
constexpr uint32_t kBounds = 0x180;
constexpr uint32_t kFrustumBlock = 0x2000;
constexpr uint32_t kCamera = 0x3000;
constexpr uint32_t kSpriteVtable = 0x40000000;
constexpr uint32_t kModelVtable = 0x40000100;

uint32_t Translation(uint32_t object) {
  return object + render_layout::kObjectWorldMatrix + render_layout::kTranslation;
}

struct World {
  Guest guest;
  RenderInterpolator render;

  World() {
    guest.SetWord(kFrustumPointer, kFrustumBlock);
    render.SetAddresses({kFrustumPointer, kBounds});
  }

  void Object(uint32_t object, uint32_t vtable, float x, float y = 0.0f, float z = 0.0f) {
    guest.SetWord(object, vtable);
    SetPosition(object, x, y, z);
  }

  void SetPosition(uint32_t object, float x, float y = 0.0f, float z = 0.0f) {
    const uint32_t at = Translation(object);
    guest.SetFloat(at, x);
    guest.SetFloat(at + 4, y);
    guest.SetFloat(at + 8, z);
  }

  float X(uint32_t object) const { return guest.Float(Translation(object)); }

  // One iteration: `ticks` simulation ticks, then a draw window that draws the objects like the model path (the
  // matrix holds the simulation value when the draw starts).
  void Iteration(int ticks, bool interpolate, double alpha, bool restart, const std::vector<uint32_t>& objects,
                 const std::function<void(int)>& simulate) {
    render.BeginIteration(true, interpolate, alpha, restart);
    render.BeginWindow();
    for (int i = 0; i < ticks; ++i) {
      render.CountTick();
      const bool outer = render.EnterTick();
      simulate(i);
      render.LeaveTick(outer);
    }
    for (const uint32_t object : objects) {
      render.ObjectMatrix(guest.base(), object, object + render_layout::kObjectWorldMatrix);
    }
  }

  void EndWindow() { render.EndWindow(guest.base()); }
};

void TestLerp() {
  std::printf("lerp\n");
  CHECK(Lerp(2.0f, 6.0f, 0.0) == 2.0f);
  CHECK(Lerp(2.0f, 6.0f, 1.0) == 6.0f);
  CHECK(Lerp(2.0f, 6.0f, 0.25) == 3.0f);
  CHECK(Lerp(2.0f, 6.0f, -1.0) == 2.0f);
  CHECK(Lerp(2.0f, 6.0f, 7.0) == 6.0f);
  // The end is exact, not 0.1 + (0.3 - 0.1) * 1.
  CHECK(Lerp(0.1f, 0.3f, 1.0) == 0.3f);
  CHECK(ClampAlpha(std::numeric_limits<double>::quiet_NaN()) == 0.0);
  const Values<3> mid = LerpValues<3>({0, 10, 20}, {10, 20, 40}, 0.5);
  CHECK(mid[0] == 5.0f && mid[1] == 15.0f && mid[2] == 30.0f);
}

void TestTrackBasics() {
  std::printf("track table\n");
  TrackTable<3> table;
  const Values<3> limits = {24, 24, 24};
  // First sight: the current value, restarted.
  auto first = table.Draw(0x1000, 1, 1, 1, {0, 0, 0}, limits, 0.5);
  CHECK(first.restarted && first.value == Values<3>({0, 0, 0}));
  // The next tick: from the previous value toward the current one.
  auto second = table.Draw(0x1000, 1, 2, 2, {10, 0, 0}, limits, 0.0);
  CHECK(!second.restarted && second.value[0] == 0.0f);
  auto half = table.Draw(0x1000, 1, 2, 3, {10, 0, 0}, limits, 0.5);
  CHECK(half.value[0] == 5.0f && half.first_in_frame);
  auto full = table.Draw(0x1000, 1, 2, 4, {10, 0, 0}, limits, 1.0);
  CHECK(full.value[0] == 10.0f);
  // Another tick chains: previous is the last current.
  auto next = table.Draw(0x1000, 1, 3, 5, {14, 0, 0}, limits, 0.5);
  CHECK(!next.restarted && next.value[0] == 12.0f);
  // The same frame again with the same values gives the same value and does not ask to save again.
  auto again = table.Draw(0x1000, 1, 3, 5, {14, 0, 0}, limits, 0.5);
  CHECK(!again.first_in_frame && again.value[0] == 12.0f);
  // Other values for the same object in that frame are another part of it: drawn as they are.
  auto part = table.Draw(0x1000, 1, 3, 5, {999, 0, 0}, limits, 0.5);
  CHECK(!part.first_in_frame && part.value[0] == 999.0f);
}

void TestTrackDiscontinuities() {
  std::printf("track discontinuities\n");
  const Values<3> limits = {24, 24, 24};
  {
    TrackTable<3> table;
    table.Draw(1, 1, 1, 1, {0, 0, 0}, limits, 0.0);
    table.Draw(1, 1, 2, 2, {1, 0, 0}, limits, 0.0);
    // A teleport beyond the limit draws the new position at once, then interpolates from it.
    auto jump = table.Draw(1, 1, 3, 3, {500, 0, 0}, limits, 0.0);
    CHECK(jump.restarted && jump.value[0] == 500.0f);
    auto after = table.Draw(1, 1, 4, 4, {501, 0, 0}, limits, 0.5);
    CHECK(!after.restarted && after.value[0] == 500.5f);
  }
  {
    // A missed tick: the object was not drawn, so it was not there.
    TrackTable<3> table;
    table.Draw(1, 1, 1, 1, {0, 0, 0}, limits, 0.0);
    table.Draw(1, 1, 2, 2, {1, 0, 0}, limits, 0.0);
    auto gap = table.Draw(1, 1, 4, 3, {3, 0, 0}, limits, 0.0);
    CHECK(gap.restarted && gap.value[0] == 3.0f);
  }
  {
    // The same address now holds another kind of object: nothing is drawn between them.
    TrackTable<3> table;
    table.Draw(1, 1, 1, 1, {0, 0, 0}, limits, 0.0);
    table.Draw(1, 1, 2, 2, {1, 0, 0}, limits, 0.0);
    auto reused = table.Draw(1, 2, 3, 3, {2, 0, 0}, limits, 0.0);
    CHECK(reused.restarted && reused.value[0] == 2.0f);
  }
  {
    // NaN and infinity never interpolate.
    TrackTable<3> table;
    table.Draw(1, 1, 1, 1, {0, 0, 0}, limits, 0.0);
    auto nan = table.Draw(1, 1, 2, 2, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, limits, 0.5);
    CHECK(nan.restarted);
    auto inf = table.Draw(1, 1, 3, 3, {std::numeric_limits<float>::infinity(), 0, 0}, limits, 0.5);
    CHECK(inf.restarted);
  }
  {
    // A scene reset forgets everything: the first draw is the current value.
    TrackTable<3> table;
    table.Draw(1, 1, 1, 1, {0, 0, 0}, limits, 0.0);
    table.Draw(1, 1, 2, 2, {1, 0, 0}, limits, 0.0);
    table.Clear();
    auto reset = table.Draw(1, 1, 3, 3, {2, 0, 0}, limits, 0.0);
    CHECK(reset.restarted && reset.value[0] == 2.0f);
  }
}

void TestTrackCapacity() {
  std::printf("track slots are reused\n");
  TrackTable<3> table;
  const Values<3> limits = {24, 24, 24};
  // Far more addresses than slots over time: old ones are dropped, the current ones keep working.
  uint64_t frame = 1;
  uint64_t tick = 1;
  for (uint32_t wave = 0; wave < 20; ++wave) {
    for (uint32_t i = 0; i < 1500; ++i) {
      table.Draw(0x10000 + (wave * 2000 + i) * 4, 1, tick, frame, {0, 0, 0}, limits, 0.0);
    }
    frame += 200;
    ++tick;
  }
  const uint32_t live = 0x10000 + (19 * 2000) * 4;
  table.Draw(live, 1, tick, frame, {0, 0, 0}, limits, 0.0);
  ++tick;
  ++frame;
  const auto moved = table.Draw(live, 1, tick, frame, {2, 0, 0}, limits, 0.5);
  CHECK(!moved.restarted && moved.value[0] == 1.0f);
}

void TestRestoreLog() {
  std::printf("restore log\n");
  Guest guest;
  guest.SetWord(0x100, 0x11111111);
  guest.SetWord(0x200, 0x22222222);
  RenderRestore restore;
  restore.Save(guest.base(), 0x100, 4);
  guest.SetWord(0x100, 0xAAAAAAAA);
  restore.Save(guest.base(), 0x200, 4);
  guest.SetWord(0x200, 0xBBBBBBBB);
  // An address saved twice ends with its first, original bytes.
  restore.Save(guest.base(), 0x100, 4);
  guest.SetWord(0x100, 0xCCCCCCCC);
  CHECK(!restore.empty());

  restore.Suspend(guest.base());
  CHECK(guest.Word(0x100) == 0x11111111);
  CHECK(guest.Word(0x200) == 0x22222222);
  restore.Resume(guest.base());
  CHECK(guest.Word(0x100) == 0xCCCCCCCC);
  CHECK(guest.Word(0x200) == 0xBBBBBBBB);

  restore.Restore(guest.base());
  CHECK(guest.Word(0x100) == 0x11111111);
  CHECK(guest.Word(0x200) == 0x22222222);
  CHECK(restore.empty());
}

void TestObjectDrawnAndRestored() {
  std::printf("objects are drawn between ticks and restored\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };

  // Tick 1 starts the track.
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  CHECK(world.X(0x1000) == 1.0f);
  world.EndWindow();
  // Tick 2: drawn at alpha 0.25 between 1 and 2, then back to the simulation's 2.
  world.Iteration(1, true, 0.25, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.25f));
  CHECK(world.render.restore_pending());
  world.EndWindow();
  CHECK(world.X(0x1000) == 2.0f);
  CHECK(!world.render.restore_pending());
  // An iteration without a tick still draws between the same two states.
  world.Iteration(0, true, 0.75, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.75f));
  world.EndWindow();
  CHECK(world.X(0x1000) == 2.0f);
}

void TestNothingWithoutPacing() {
  std::printf("nothing is written when it is not pacing\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  // Not interpolating: the draws see the simulation's values.
  for (int i = 0; i < 5; ++i) {
    world.Iteration(1, false, 0.5, false, {0x1000}, simulate);
    CHECK(world.X(0x1000) == float(i + 1));
    CHECK(!world.render.restore_pending());
    world.EndWindow();
  }
  // Interpolating with alpha 1: also the latest values.
  for (int i = 0; i < 5; ++i) {
    world.Iteration(1, true, 1.0, false, {0x1000}, simulate);
    CHECK(world.X(0x1000) == float(6 + i));
    CHECK(!world.render.restore_pending());
    world.EndWindow();
  }
  // Not active at all.
  World inactive;
  inactive.Object(0x1000, kSpriteVtable, 3.0f);
  inactive.render.BeginIteration(false, true, 0.5, false);
  inactive.render.BeginWindow();
  inactive.render.ObjectMatrix(inactive.guest.base(), 0x1000, 0x1000 + render_layout::kObjectWorldMatrix);
  CHECK(inactive.X(0x1000) == 3.0f);
  inactive.EndWindow();
}

void TestTeleportAndDeath() {
  std::printf("teleports and respawns are drawn where they are\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  float step = 1.0f;
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + step); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.5f));
  world.EndWindow();
  // The player respawns far away.
  world.Iteration(1, true, 0.5, false, {0x1000}, [&](int) { world.SetPosition(0x1000, 300.0f); });
  CHECK(world.X(0x1000) == 300.0f);
  CHECK(!world.render.restore_pending());
  world.EndWindow();
  // The next ticks interpolate from the new place.
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 300.5f));
  world.EndWindow();
  CHECK(world.X(0x1000) == 301.0f);
}

void TestSmallMovesOnly() {
  std::printf("only ordinary per-tick moves are interpolated\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  float step = 2.0f;
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + step); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 3.0f));
  world.EndWindow();
  // A same-kind object that took the address ten units away is not glided to.
  world.Iteration(1, true, 0.5, false, {0x1000}, [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 10.0f); });
  CHECK(world.X(0x1000) == 14.0f);
  world.EndWindow();
}

void TestSuspendIsBounded() {
  std::printf("suspending the drawn values\n");
  Guest guest;
  guest.SetWord(0x100, 1);
  RenderRestore restore;
  restore.Save(guest.base(), 0x100, 4);
  guest.SetWord(0x100, 2);
  restore.Suspend(guest.base());
  // A save made while suspended is not part of the resume.
  restore.Save(guest.base(), 0x200, 4);
  restore.Resume(guest.base());
  CHECK(guest.Word(0x100) == 2);
  restore.Restore(guest.base());
  CHECK(guest.Word(0x100) == 1);
}

void TestObjectReuse() {
  std::printf("a reused address is never interpolated from its previous object\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.0f));
  world.EndWindow();

  // A model object takes the address next tick, a few units away: still no interpolation across them.
  world.Iteration(1, true, 0.5, false, {0x1000}, [&](int) {
    world.guest.SetWord(0x1000, kModelVtable);
    world.SetPosition(0x1000, 5.0f);
  });
  CHECK(world.X(0x1000) == 5.0f);
  CHECK(!world.render.restore_pending());
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, [&](int) { world.SetPosition(0x1000, 6.0f); });
  CHECK(Near(world.X(0x1000), 5.5f));
  world.EndWindow();

  // The object is gone for a tick (not drawn) and a new one of the same kind appears at the address.
  world.Iteration(1, true, 0.5, false, {}, [&](int) { world.SetPosition(0x1000, 7.0f); });
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, [&](int) { world.SetPosition(0x1000, 8.0f); });
  CHECK(world.X(0x1000) == 8.0f);
  world.EndWindow();
}

void TestSceneReset() {
  std::printf("a restart forgets every transform\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.5f));
  world.EndWindow();
  // New scene: the object is the same address a little way off, and the first frame is the live value.
  world.Iteration(1, true, 0.5, true, {0x1000}, simulate);
  CHECK(world.X(0x1000) == 3.0f);
  CHECK(!world.render.restore_pending());
  world.EndWindow();
}

void TestTickAndLateTick() {
  std::printf("the game's tick sees its own values\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.5f));
  // Drawing inside a tick draws nothing.
  const bool outer = world.render.EnterTick();
  CHECK(!world.render.Drawing());
  world.render.LeaveTick(outer);
  CHECK(world.render.Drawing());
  // The late tick reads the simulation state, then the drawn values come back.
  world.render.SuspendDrawn(world.guest.base());
  CHECK(world.X(0x1000) == 2.0f);
  world.render.ResumeDrawn(world.guest.base());
  CHECK(Near(world.X(0x1000), 1.5f));
  world.EndWindow();
  CHECK(world.X(0x1000) == 2.0f);
}

void TestRepeatedDraws() {
  std::printf("an object drawn twice in a frame\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.5f));
  // The model path draws again with the drawn value still in place.
  world.render.ObjectMatrix(world.guest.base(), 0x1000, 0x1000 + render_layout::kObjectWorldMatrix);
  CHECK(Near(world.X(0x1000), 1.5f));
  // The sprite path rebuilds the matrix from the simulation first.
  world.SetPosition(0x1000, 2.0f);
  world.render.ObjectMatrix(world.guest.base(), 0x1000, 0x1000 + render_layout::kObjectWorldMatrix);
  CHECK(Near(world.X(0x1000), 1.5f));
  world.EndWindow();
  CHECK(world.X(0x1000) == 2.0f);
}

void SetLinear(World& world, uint32_t object, float xx, float yx = 0.0f) {
  const uint32_t at = object + render_layout::kObjectWorldMatrix;
  world.guest.SetFloat(at, xx);
  world.guest.SetFloat(at + 16, yx);
}

void TestRotatingObjects() {
  std::printf("objects whose rotation changes are drawn as built\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  SetLinear(world, 0x1000, 1.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  // A steady orientation interpolates.
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.5f));
  world.EndWindow();
  // A turn between ticks is drawn at the game's own position, and the next steady tick interpolates again.
  SetLinear(world, 0x1000, 0.9f, 0.4f);
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(world.X(0x1000) == 3.0f);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 3.5f));
  world.EndWindow();
  // A slow drift under the tolerance still interpolates.
  SetLinear(world, 0x1000, 0.9f + 0.001f, 0.4f);
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 4.5f));
  world.EndWindow();
  // A mirrored object (facing change) snaps.
  SetLinear(world, 0x1000, -0.9f, 0.4f);
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(world.X(0x1000) == 6.0f);
  world.EndWindow();
}

void TestMultiPartObjects() {
  std::printf("an object drawn as several parts is not collapsed\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  auto simulate = [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); };
  world.Iteration(1, true, 0.0, false, {0x1000}, simulate);
  world.EndWindow();
  world.Iteration(1, true, 0.5, false, {0x1000}, simulate);
  CHECK(Near(world.X(0x1000), 1.5f));
  // A second part with its own matrix keeps that matrix.
  world.SetPosition(0x1000, 20.0f);
  world.render.ObjectMatrix(world.guest.base(), 0x1000, 0x1000 + render_layout::kObjectWorldMatrix);
  CHECK(world.X(0x1000) == 20.0f);
  world.EndWindow();
}

void SetCameraPosition(World& world, float x, float turn = 0.0f) {
  const uint32_t matrix = kCamera + render_layout::kCameraWorldMatrix;
  const float identity[16] = {1, turn, 0, 0, -turn, 1, 0, 0, 0, 0, 1, 0, x, 0, 5, 1};
  for (uint32_t i = 0; i < 16; ++i) {
    world.guest.SetFloat(matrix + 4 * i, identity[i]);
  }
}

float CameraX(World& world) { return world.guest.Float(kCamera + render_layout::kCameraWorldMatrix + 48); }

void TestCamera() {
  std::printf("the camera is drawn between ticks and restored\n");
  World world;
  world.guest.SetWord(kCamera, 0x40000200);
  world.guest.SetWord(kCamera + render_layout::kCameraDirty, 0);
  for (uint32_t i = 0; i < 64; ++i) {
    world.guest.bytes[kFrustumBlock + 8 + i] = uint8_t(i + 1);
  }
  for (uint32_t i = 0; i < 24; ++i) {
    world.guest.bytes[kBounds + i] = uint8_t(0x80 + i);
  }
  int applied = 0;
  auto apply = [&] {
    ++applied;
    // Camera::Apply rewrites the view, the frustum planes and the visible rectangle from the world matrix.
    world.guest.SetFloat(kCamera + 4, CameraX(world));
    world.guest.bytes[kFrustumBlock + 8] = 0xEE;
    world.guest.bytes[kBounds] = 0xEE;
    world.guest.SetWord(kCamera + render_layout::kCameraDirty, 0);
  };

  SetCameraPosition(world, 0.0f);
  world.Iteration(1, true, 0.0, false, {}, [](int) {});
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  CHECK(applied == 0);
  world.EndWindow();
  SetCameraPosition(world, 1.0f);
  world.Iteration(1, true, 0.5, false, {}, [](int) {});
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  CHECK(applied == 1);
  CHECK(Near(CameraX(world), 0.5f));
  CHECK(world.guest.Float(kCamera + 4) == CameraX(world));
  // A second Camera::Apply in the same window leaves it alone.
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  CHECK(applied == 1);
  world.EndWindow();
  CHECK(CameraX(world) == 1.0f);
  CHECK(world.guest.Word(kCamera + render_layout::kCameraDirty) == 0);
  CHECK(world.guest.bytes[kFrustumBlock + 8] == 1);
  CHECK(world.guest.bytes[kBounds] == 0x80);
  CHECK(world.guest.Float(kCamera + 4) == 0.0f);
}

void TestCameraCutsAndTurns() {
  std::printf("camera cuts and turns\n");
  World world;
  world.guest.SetWord(kCamera, 0x40000200);
  int applied = 0;
  auto apply = [&] { ++applied; };
  SetCameraPosition(world, 0.0f);
  world.Iteration(1, true, 0.0, false, {}, [](int) {});
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  world.EndWindow();
  // A cut far away is shown as it is.
  SetCameraPosition(world, 400.0f);
  world.Iteration(1, true, 0.5, false, {}, [](int) {});
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  CHECK(applied == 0 && CameraX(world) == 400.0f);
  world.EndWindow();
  // A turn beyond the limit is a cut too.
  SetCameraPosition(world, 400.0f, 0.9f);
  world.Iteration(1, true, 0.5, false, {}, [](int) {});
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  CHECK(applied == 0);
  world.EndWindow();
  // A small turn is drawn between.
  SetCameraPosition(world, 400.0f, 0.95f);
  world.Iteration(1, true, 0.5, false, {}, [](int) {});
  world.render.CameraApplied(world.guest.base(), kCamera, apply);
  CHECK(applied == 1);
  CHECK(Near(world.guest.Float(kCamera + render_layout::kCameraWorldMatrix + 4), 0.925f));
  world.EndWindow();
  CHECK(Near(world.guest.Float(kCamera + render_layout::kCameraWorldMatrix + 4), 0.95f));
}

void TestEndToEndMotion() {
  std::printf("motion stays even from the pacer to the draw\n");
  for (const int rate : {90, 120, 144, 165, 240}) {
    World world;
    world.Object(0x1000, kSpriteVtable, 0.0f);
    IterationSchedule schedule;
    GameplaySignals signals{true, true, false, 1};
    double now = 1.0;
    const double period = 1.0 / rate;
    for (int i = 0; i < 40; ++i) {
      now += kNativeStep;
      schedule.Next(rate, signals, SimulationClock::time_point{} + std::chrono::nanoseconds(int64_t(now * 1e9)));
    }
    double previous = -1.0;
    double worst = 0.0;
    double lead = 0.0;
    int drawn = 0;
    for (int i = 0; i < 3000; ++i) {
      now += period;
      const IterationPlan plan =
          schedule.Next(rate, signals, SimulationClock::time_point{} + std::chrono::nanoseconds(int64_t(now * 1e9)));
      const int ticks = static_cast<int>(plan.step.steps);
      world.Iteration(ticks, plan.step.interpolate, plan.step.alpha, plan.restarted, {0x1000},
                      [&](int) { world.SetPosition(0x1000, world.X(0x1000) + 1.0f); });
      const double shown = world.X(0x1000);
      world.EndWindow();
      if (i > 20 && plan.step.interpolate) {
        if (previous >= 0.0) {
          worst = std::max(worst, std::fabs((shown - previous) - 60.0 / rate));
        }
        lead = std::max(lead, shown - double(world.X(0x1000)));
        ++drawn;
      }
      previous = shown;
    }
    CHECK(drawn > 2900);
    CHECK(worst < 1e-3);
    CHECK(lead <= 1e-6);
  }
}

void TestSimulationNeverSeesDrawnValues() {
  std::printf("the simulation never reads an interpolated value\n");
  World world;
  world.Object(0x1000, kSpriteVtable, 0.0f);
  world.Object(0x1100, kModelVtable, 50.0f);
  double now = 1.0;
  IterationSchedule schedule;
  GameplaySignals signals{true, true, false, 1};
  for (int i = 0; i < 40; ++i) {
    now += kNativeStep;
    schedule.Next(144, signals, SimulationClock::time_point{} + std::chrono::nanoseconds(int64_t(now * 1e9)));
  }
  float expected = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    now += 1.0 / 144.0;
    const IterationPlan plan =
        schedule.Next(144, signals, SimulationClock::time_point{} + std::chrono::nanoseconds(int64_t(now * 1e9)));
    const int ticks = static_cast<int>(plan.step.steps);
    bool consistent = true;
    world.Iteration(ticks, plan.step.interpolate, plan.step.alpha, plan.restarted, {0x1000, 0x1100}, [&](int) {
      // The tick reads what the last tick left, exactly.
      consistent &= world.X(0x1000) == expected && world.X(0x1100) == expected + 50.0f;
      expected += 1.0f;
      world.SetPosition(0x1000, expected);
      world.SetPosition(0x1100, expected + 50.0f);
    });
    world.EndWindow();
    CHECK(consistent);
    CHECK(world.X(0x1000) == expected);
    CHECK(world.X(0x1100) == expected + 50.0f);
  }
}

void TestWiring() {
  std::printf("visual interpolation wiring\n");
  const std::string source = ReadSource("frame_schedule.cpp");
  CHECK(Has(source, "g.render.ObjectMatrix(base, object, matrix)"));
  CHECK(Has(source, "hcu::g.render.CameraApplied(base, camera"));
  CHECK(Has(source, "hcu::g.render.SuspendDrawn(base)"));
  CHECK(Has(source, "g.render.EndWindow(base)"));
  const std::string header = ReadSource("visual_interpolation.h");
  CHECK(Has(header, "kObjectWorldMatrix = 512"));
  CHECK(Has(header, "kTranslation = 48"));
  CHECK(Has(header, "kCameraWorldMatrix = 68"));
  CHECK(Has(header, "kCameraDirty = 156"));
  CHECK(Has(header, "kFrustumPointer = 0x82CB9714"));
  CHECK(Has(header, "kVisibleBounds = 0x82CB97B0"));
}

}  // namespace

int main() {
  TestLerp();
  TestTrackBasics();
  TestTrackDiscontinuities();
  TestTrackCapacity();
  TestRestoreLog();
  TestObjectDrawnAndRestored();
  TestNothingWithoutPacing();
  TestTeleportAndDeath();
  TestSmallMovesOnly();
  TestSuspendIsBounded();
  TestObjectReuse();
  TestSceneReset();
  TestTickAndLateTick();
  TestRepeatedDraws();
  TestRotatingObjects();
  TestMultiPartObjects();
  TestCamera();
  TestCameraCutsAndTurns();
  TestEndToEndMotion();
  TestSimulationNeverSeesDrawnValues();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
