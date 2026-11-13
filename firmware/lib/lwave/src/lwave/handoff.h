// L-wave firmware — lock-free handoff between the control loop and the audio
// interrupt. Neither side ever waits for the other.
#pragma once
#include <atomic>
#include <stdint.h>

namespace lw {

/// Triple buffer: one writer publishes whole snapshots, one reader always gets
/// the newest complete one. Safe between loop() and an interrupt, and between
/// two threads in the simulator.
template <typename T>
class TripleBuffer {
 public:
  TripleBuffer() : back_(0), middle_(1), front_(2) {}

  /// Writer: the buffer to fill. Stays valid until publish().
  T& writeBuffer() { return slots_[back_]; }

  /// Writer: makes the filled buffer the newest snapshot.
  void publish() {
    back_ = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel) & kIndexMask;
  }

  /// Reader: the newest snapshot. Swaps in a newer one if the writer published since the last call.
  const T& read() {
    if (middle_.load(std::memory_order_acquire) & kFresh) {
      front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndexMask;
    }
    return slots_[front_];
  }

 private:
  static constexpr int kFresh = 4;      // set in middle_ when it holds an unread snapshot
  static constexpr int kIndexMask = 3;
  T slots_[3];
  int back_;                 // writer-owned slot index
  std::atomic<int> middle_;  // shared: slot index | kFresh
  int front_;                // reader-owned slot index
};

/// Single-producer single-consumer ring buffer of plain values.
/// Capacity is N - 1 items. push() fails (returns false) when full.
template <typename T, int N>
class SpscQueue {
 public:
  /// Producer: adds an item. False if the queue is full (the item is dropped).
  bool push(const T& item) {
    int h = head_.load(std::memory_order_relaxed);
    int next = (h + 1) % N;
    if (next == tail_.load(std::memory_order_acquire)) return false;
    items_[h] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }

  /// Consumer: takes the oldest item. False if the queue is empty.
  bool pop(T& out) {
    int t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;
    out = items_[t];
    tail_.store((t + 1) % N, std::memory_order_release);
    return true;
  }

 private:
  T items_[N];
  std::atomic<int> head_{0};
  std::atomic<int> tail_{0};
};

}  // namespace lw
