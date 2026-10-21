#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "guest_memory.h"

namespace hcu {

// Guest layout the renderer reads. Matrices are row-major with the translation in row 3.
namespace render_layout {

constexpr uint32_t kObjectWorldMatrix = 512;
constexpr uint32_t kTranslation = 48;
constexpr uint32_t kCameraWorldMatrix = 68;
constexpr uint32_t kCameraDirty = 156;
// The bytes Camera::Apply writes in the camera: its view matrix and scratch.
constexpr uint32_t kCameraSize = 160;
// Camera::Apply also writes four frustum planes behind this pointer and the visible world rectangle, which gameplay
// reads for on-screen checks.
constexpr uint32_t kFrustumPointer = 0x82CB9714;
constexpr uint32_t kFrustumPlanes = 8;
constexpr uint32_t kFrustumPlanesSize = 64;
constexpr uint32_t kVisibleBounds = 0x82CB97B0;
constexpr uint32_t kVisibleBoundsSize = 24;

}  // namespace render_layout

// Where Camera::Apply keeps the globals the renderer reads; tests point them into a small buffer.
struct RenderAddresses {
  uint32_t frustum_pointer = render_layout::kFrustumPointer;
  uint32_t visible_bounds = render_layout::kVisibleBounds;
};

// Per tick moves beyond these are cuts or teleports and are not interpolated. Movement is under 2 units per tick; the
// limit also bounds how far a reused address can be from the object it held.
inline constexpr float kMaxJump = 8.0f;
inline constexpr float kMaxTurn = 0.25f;
// An object whose rotation or scale changes between ticks is drawn as the game built it: its translation includes
// the offset of its pivot, which only follows a rotation that is interpolated with it.
inline constexpr float kObjectLinearTolerance = 0.002f;

template <size_t N>
using Values = std::array<float, N>;

inline double ClampAlpha(double alpha) {
  return alpha > 0.0 ? std::min(alpha, 1.0) : 0.0;
}

inline float Lerp(float from, float to, double alpha) {
  const double a = ClampAlpha(alpha);
  return a >= 1.0 ? to : static_cast<float>(from + (to - from) * a);
}

template <size_t N>
Values<N> LerpValues(const Values<N>& from, const Values<N>& to, double alpha) {
  Values<N> out;
  for (size_t i = 0; i < N; ++i) {
    out[i] = Lerp(from[i], to[i], alpha);
  }
  return out;
}

template <size_t N>
bool Jumps(const Values<N>& from, const Values<N>& to, const Values<N>& limits) {
  for (size_t i = 0; i < N; ++i) {
    if (!(std::fabs(to[i] - from[i]) <= limits[i])) {
      return true;
    }
  }
  return false;
}

// The render transforms of the objects drawn in the last two simulation ticks, keyed by guest address.
//
// A transform is interpolated only between two consecutive ticks of one object: an address first seen, seen again
// after a missed tick, holding another type, or moved beyond the limits starts over from its current value. Slots
// of objects not drawn for a while are reused.
template <size_t N>
class TrackTable {
 public:
  static constexpr size_t kCapacity = 4096;
  static constexpr size_t kMaxProbe = 48;
  static constexpr uint64_t kForgetAfter = 120;

  struct Result {
    Values<N> value;
    // First draw of this transform in the frame: the caller saves what it overwrites only then.
    bool first_in_frame = true;
    bool restarted = false;
  };

  Result Draw(uint32_t key, uint32_t type, uint64_t tick, uint64_t frame, const Values<N>& current,
              const Values<N>& limits, double alpha) {
    Entry* entry = Find(key, frame);
    if (entry == nullptr) {
      return {current, true, true};
    }
    if (entry->epoch != epoch_ || entry->key != key) {
      *entry = Entry{key, type, epoch_, tick, frame, 1, current, current, current, current};
      return {current, true, true};
    }
    // The same object drawn again in a frame with other values is several parts, not one transform.
    if (entry->frame == frame && entry->drawn && entry->tick == tick && current != entry->seen) {
      return {current, false, false};
    }
    Result result;
    if (entry->tick != tick) {
      const bool next = tick == entry->tick + 1 && entry->type == type && !Jumps(entry->curr, current, limits);
      entry->prev = next ? entry->curr : current;
      entry->curr = current;
      entry->tick = tick;
      entry->type = type;
      result.restarted = !next;
    }
    if (entry->frame == frame && entry->drawn) {
      result.value = entry->render;
      result.first_in_frame = false;
      return result;
    }
    entry->frame = frame;
    entry->drawn = true;
    entry->seen = current;
    entry->render = LerpValues(entry->prev, entry->curr, alpha);
    result.value = entry->render;
    return result;
  }

  void Clear() { ++epoch_; }

 private:
  struct Entry {
    uint32_t key = 0;
    uint32_t type = 0;
    uint32_t epoch = 0;
    uint64_t tick = 0;
    uint64_t frame = 0;
    uint32_t drawn = 0;
    Values<N> prev{};
    Values<N> curr{};
    Values<N> render{};
    Values<N> seen{};
  };

  static size_t Slot(uint32_t key) { return ((key * 2654435761u) >> 12) & (kCapacity - 1); }

  Entry* Find(uint32_t key, uint64_t frame) {
    Entry* reusable = nullptr;
    const size_t start = Slot(key);
    for (size_t i = 0; i < kMaxProbe; ++i) {
      Entry& candidate = entries_[(start + i) & (kCapacity - 1)];
      if (candidate.epoch != epoch_) {
        return reusable ? reusable : &candidate;
      }
      if (candidate.key == key) {
        return &candidate;
      }
      if (!reusable && frame > candidate.frame + kForgetAfter) {
        reusable = &candidate;
      }
    }
    return reusable;
  }

  std::vector<Entry> entries_ = std::vector<Entry>(kCapacity);
  uint32_t epoch_ = 1;
};

// Memory changed for drawing, written back once the draw ends. `base` is the start of the memory the addresses are
// offsets into.
class RenderRestore {
 public:
  void Save(const uint8_t* base, uint32_t addr, uint32_t size) {
    entries_.push_back({addr, size, static_cast<uint32_t>(bytes_.size())});
    bytes_.insert(bytes_.end(), base + addr, base + addr + size);
  }

  // Newest first, so an address saved twice ends with its first, original bytes.
  void Restore(uint8_t* base) {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
      std::memcpy(base + it->addr, bytes_.data() + it->offset, it->size);
    }
    entries_.clear();
    bytes_.clear();
  }

  // Puts the original bytes back while game code that must not see the drawn values runs, then Resume() puts the
  // drawn values back.
  void Suspend(uint8_t* base) {
    suspended_ = entries_.size();
    held_.resize(bytes_.size());
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
      std::memcpy(held_.data() + it->offset, base + it->addr, it->size);
      std::memcpy(base + it->addr, bytes_.data() + it->offset, it->size);
    }
  }

  void Resume(uint8_t* base) {
    for (size_t i = 0; i < suspended_ && i < entries_.size(); ++i) {
      std::memcpy(base + entries_[i].addr, held_.data() + entries_[i].offset, entries_[i].size);
    }
    suspended_ = 0;
  }

  bool empty() const { return entries_.empty(); }

 private:
  struct Entry {
    uint32_t addr;
    uint32_t size;
    uint32_t offset;
  };
  std::vector<Entry> entries_;
  std::vector<uint8_t> bytes_;
  std::vector<uint8_t> held_;
  size_t suspended_ = 0;
};

inline constexpr std::array<uint32_t, 12> kAffineElements = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};

// Draws gameplay between its 60 Hz states: object translations and the stage camera are replaced while a draw runs
// and put back when it ends, so the simulation never sees an interpolated value.
class RenderInterpolator {
 public:
  using CameraValues = Values<12>;
  using ObjectValues = Values<12>;
  static constexpr size_t kLinearElements = 9;

  // Start of a main-loop iteration. `restart` forgets every transform seen before.
  void BeginIteration(bool active, bool interpolate, double alpha, bool restart) {
    if (restart) {
      Forget();
    }
    active_ = active;
    interpolate_ = active && interpolate;
    alpha_ = ClampAlpha(alpha);
  }

  void Forget() {
    objects_.Clear();
    cameras_.Clear();
  }

  // One simulation tick of the stage scene.
  void CountTick() { ++tick_; }

  void BeginWindow() {
    if (window_depth_++ == 0) {
      ++frame_;
    }
  }

  void EndWindow(uint8_t* base) {
    if (window_depth_ > 0 && --window_depth_ == 0) {
      restore_.Restore(base);
    }
  }

  // The game's own tick and late tick read the state the simulation owns.
  bool EnterTick() {
    const bool outer = in_tick_;
    in_tick_ = true;
    return outer;
  }
  void LeaveTick(bool outer) { in_tick_ = outer; }
  void SuspendDrawn(uint8_t* base) { restore_.Suspend(base); }
  void ResumeDrawn(uint8_t* base) { restore_.Resume(base); }

  bool Drawing() const { return interpolate_ && window_depth_ > 0 && !in_tick_; }

  // After an object's world matrix is built (or about to be read): replaces its translation with the drawn one.
  void ObjectMatrix(uint8_t* base, uint32_t object, uint32_t matrix) {
    if (!Drawing() || object == 0) {
      return;
    }
    const uint32_t translation = matrix + render_layout::kTranslation;
    ObjectValues current;
    for (size_t i = 0; i < kAffineElements.size(); ++i) {
      current[i] = LoadFloat(base, matrix + 4 * kAffineElements[i]);
    }
    constexpr ObjectValues kLimits = {
        kObjectLinearTolerance, kObjectLinearTolerance, kObjectLinearTolerance, kObjectLinearTolerance,
        kObjectLinearTolerance, kObjectLinearTolerance, kObjectLinearTolerance, kObjectLinearTolerance,
        kObjectLinearTolerance, kMaxJump,               kMaxJump,               kMaxJump};
    const uint32_t type = LoadBE32(base, object);
    const auto drawn = objects_.Draw(object, type, tick_, frame_, current, kLimits, alpha_);
    bool moved = false;
    for (size_t i = kLinearElements; i < current.size(); ++i) {
      moved |= drawn.value[i] != current[i];
    }
    if (!moved) {
      return;
    }
    if (drawn.first_in_frame) {
      restore_.Save(base, translation, 12);
    }
    for (uint32_t i = 0; i < 3; ++i) {
      StoreFloat(base, translation + 4 * i, drawn.value[kLinearElements + i]);
    }
  }

  // After Camera::Apply: draws from the interpolated world matrix. `apply` runs Camera::Apply again for it.
  template <class Apply>
  void CameraApplied(uint8_t* base, uint32_t camera, Apply&& apply) {
    if (!Drawing() || camera == 0) {
      return;
    }
    const uint32_t world = camera + render_layout::kCameraWorldMatrix;
    CameraValues current;
    for (size_t i = 0; i < kAffineElements.size(); ++i) {
      current[i] = LoadFloat(base, world + 4 * kAffineElements[i]);
    }
    constexpr CameraValues kLimits = {kMaxTurn, kMaxTurn, kMaxTurn, kMaxTurn, kMaxTurn, kMaxTurn,
                                      kMaxTurn, kMaxTurn, kMaxTurn, kMaxJump, kMaxJump, kMaxJump};
    const auto drawn = cameras_.Draw(camera, LoadBE32(base, camera), tick_, frame_, current, kLimits, alpha_);
    if (!drawn.first_in_frame || drawn.value == current) {
      return;
    }
    restore_.Save(base, camera, render_layout::kCameraSize);
    if (const uint32_t frustum = LoadBE32(base, addresses_.frustum_pointer)) {
      restore_.Save(base, frustum + render_layout::kFrustumPlanes, render_layout::kFrustumPlanesSize);
    }
    restore_.Save(base, addresses_.visible_bounds, render_layout::kVisibleBoundsSize);
    for (size_t i = 0; i < kAffineElements.size(); ++i) {
      StoreFloat(base, world + 4 * kAffineElements[i], drawn.value[i]);
    }
    StoreBE32(base, camera + render_layout::kCameraDirty, 1);
    apply();
  }

  void SetAddresses(const RenderAddresses& addresses) { addresses_ = addresses; }

  uint64_t tick() const { return tick_; }
  uint64_t frame() const { return frame_; }
  bool restore_pending() const { return !restore_.empty(); }

 private:
  static float LoadFloat(const uint8_t* base, uint32_t addr) {
    return std::bit_cast<float>(LoadBE32(base, addr));
  }
  static void StoreFloat(uint8_t* base, uint32_t addr, float value) {
    StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
  }

  TrackTable<12> objects_;
  TrackTable<12> cameras_;
  RenderRestore restore_;
  RenderAddresses addresses_;
  bool active_ = false;
  bool interpolate_ = false;
  double alpha_ = 1.0;
  uint64_t tick_ = 0;
  uint64_t frame_ = 0;
  int window_depth_ = 0;
  bool in_tick_ = false;
};

}  // namespace hcu
