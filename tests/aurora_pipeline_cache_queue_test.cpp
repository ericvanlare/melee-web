#include "pipeline_cache_queue.hpp"

#include <cassert>
#include <cstdint>
#include <list>
#include <utility>
#include <vector>

struct Write {
  uint8_t type;
  uint64_t hash;
  uint32_t version;
  std::vector<uint8_t> config;
  uint32_t firstFrameUsed;
};

struct Traits {
  struct Key {
    uint8_t type;
    uint64_t hash;
    bool operator==(const Key&) const = default;
  };
  struct Hash {
    size_t operator()(const Key& key) const noexcept {
      return static_cast<size_t>(key.hash ^ (uint64_t(key.type) << 56));
    }
  };
  static Key key(const Write& write) { return {.type = write.type, .hash = write.hash}; }
  static size_t bytes(const Write& write) { return write.config.size(); }
  static void merge(Write& existing, Write incoming) {
    existing.version = incoming.version;
    existing.config = std::move(incoming.config);
    if (incoming.firstFrameUsed < existing.firstFrameUsed) {
      existing.firstFrameUsed = incoming.firstFrameUsed;
    }
  }
};

using Queue = aurora::gfx::detail::CoalescingWriteQueue<Write, Traits, 2, 8>;

int main() {
  Queue queue;
  assert(queue.enqueue({1, 10, 1, {1, 2, 3}, 40}) == Queue::EnqueueResult::Added);
  assert(queue.enqueue({2, 10, 1, {4, 5}, 30}) == Queue::EnqueueResult::Added);
  assert(queue.size() == 2);
  assert(queue.bytes() == 5);

  // Coalescing keeps the original position, updates the newest payload, and
  // retains the earliest frame-use value.
  assert(queue.enqueue({1, 10, 2, {6, 7, 8, 9}, 12}) == Queue::EnqueueResult::Coalesced);
  assert(queue.size() == 2);
  assert(queue.bytes() == 6);

  std::list<Write> batch;
  queue.take(batch);
  assert(batch.size() == 2);
  auto first = batch.begin();
  assert(first->type == 1 && first->hash == 10 && first->version == 2);
  assert(first->firstFrameUsed == 12 && first->config.size() == 4 && first->config[0] == 6);
  auto second = std::next(first);
  assert(second->type == 2 && second->hash == 10);
  assert(!queue.pending() && queue.bytes() == 0);

  Queue rowBound;
  assert(rowBound.enqueue({1, 1, 1, {1}, 1}) == Queue::EnqueueResult::Added);
  assert(rowBound.enqueue({1, 2, 1, {2}, 2}) == Queue::EnqueueResult::Added);
  assert(rowBound.enqueue({1, 3, 1, {3}, 3}) == Queue::EnqueueResult::Overflow);
  assert(rowBound.overflowed() && rowBound.size() == 2);

  Queue byteBound;
  assert(byteBound.enqueue({1, 1, 1, {1, 2, 3, 4, 5, 6, 7}, 1}) == Queue::EnqueueResult::Added);
  assert(byteBound.enqueue({1, 1, 2, {8, 9}, 0}) == Queue::EnqueueResult::Coalesced);
  assert(byteBound.bytes() == 2);
  assert(byteBound.enqueue({1, 2, 1, {1, 2, 3, 4, 5, 6, 7}, 2}) == Queue::EnqueueResult::Overflow);
  assert(byteBound.overflowed() && byteBound.size() == 1 && byteBound.bytes() == 2);
  // An oversized replacement must leave the previous row and accounting intact.
  assert(byteBound.enqueue({1, 1, 3, {0, 1, 2, 3, 4, 5, 6, 7, 8}, 0}) == Queue::EnqueueResult::Overflow);
  std::list<Write> retained;
  byteBound.take(retained);
  assert(retained.size() == 1 && retained.front().version == 2);
  assert(retained.front().config == std::vector<uint8_t>({8, 9}));
  assert(retained.front().firstFrameUsed == 0);
  assert(byteBound.overflowed() && !byteBound.pending() && byteBound.bytes() == 0);
  byteBound.clear();
  assert(!byteBound.overflowed());
  assert(byteBound.enqueue({1, 1, 1, {1}, 1}) == Queue::EnqueueResult::Added);
}
