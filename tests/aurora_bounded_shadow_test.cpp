// This template is completed by test_aurora_bounded_shadow.py with ArrayRef and
// ByteBuffer extracted verbatim from the patched internal.hpp.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aurora {
// Test-only name lookup intercepts the extracted class's allocator calls.
// Production ByteBuffer and its public API remain unchanged by this seam.
static bool fail_allocation = false;
static void* malloc(size_t size) { return fail_allocation ? nullptr : std::malloc(size); }
static void* calloc(size_t count, size_t size) { return fail_allocation ? nullptr : std::calloc(count, size); }
static void* realloc(void* pointer, size_t size) { return fail_allocation ? nullptr : std::realloc(pointer, size); }
@AURORA_BYTEBUFFER_CLASSES@
}

using aurora::ByteBuffer;

[[noreturn]] static void fail(const char* message) {
  std::fprintf(stderr, "FAIL: %s\n", message);
  std::exit(1);
}

static void expect(bool condition, const char* message) {
  if (!condition) {
    fail(message);
  }
}

static size_t aligned(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

static void append_aligned(ByteBuffer& buffer, const uint8_t* data, size_t size, size_t alignment) {
  const size_t begin = buffer.size();
  const size_t aligned_begin = aligned(begin, alignment);
  if (aligned_begin > begin) {
    buffer.append_zeroes(aligned_begin - begin);
  }
  buffer.append(data, size);
}

template <typename Function>
static void expect_abort(Function&& function, const char* message) {
  const pid_t pid = fork();
  expect(pid >= 0, "fork failed");
  if (pid == 0) {
    const rlimit no_core{0, 0};
    setrlimit(RLIMIT_CORE, &no_core);
    function();
    std::_Exit(0);
  }
  int status = 0;
  expect(waitpid(pid, &status, 0) == pid, "waitpid failed");
  expect(WIFSIGNALED(status), message);
  expect(WTERMSIG(status) == SIGABRT, "fail-closed path used an unexpected signal");
}

struct Slot {
  ByteBuffer bytes = ByteBuffer::bounded(64);
  size_t previous_used = 0;
};

struct Packet {
  ByteBuffer bytes;
};

static void prepare_slot(Slot& slot) {
  if (slot.previous_used != 0) {
    std::memset(slot.bytes.data(), 0, slot.previous_used);
  }
  slot.bytes.clear();
}

static void record_slot(Slot& slot, uint8_t seed) {
  prepare_slot(slot);
  const uint8_t* old_pointer = slot.bytes.data();
  const size_t old_capacity = slot.bytes.capacity();
  Packet packet{.bytes = std::move(slot.bytes)};
  const uint8_t first[] = {seed, static_cast<uint8_t>(seed + 1), static_cast<uint8_t>(seed + 2)};
  append_aligned(packet.bytes, first, sizeof(first), 4);
  const uint8_t second[] = {static_cast<uint8_t>(seed + 3), static_cast<uint8_t>(seed + 4)};
  append_aligned(packet.bytes, second, sizeof(second), 8);
  expect(packet.bytes.size() == 10, "aligned range size mismatch");
  expect(packet.bytes.data()[0] == seed, "first range changed after owned growth");
  expect(packet.bytes.data()[3] == 0, "4-byte alignment padding not zero");
  for (size_t i = 5; i < 8; ++i) {
    expect(packet.bytes.data()[i] == 0, "8-byte alignment padding not zero");
  }
  slot.previous_used = packet.bytes.size();
  slot.bytes = std::move(packet.bytes);
  expect(slot.bytes.max_capacity() == 64, "bounded maximum was lost on move-back");
  expect(slot.bytes.size() == slot.previous_used, "slot ownership did not retain logical size");
  if (old_pointer != nullptr) {
    expect(slot.bytes.data() == old_pointer, "smaller subsequent frame changed the owned pointer");
    expect(slot.bytes.capacity() == old_capacity, "smaller subsequent frame changed capacity");
  }
}

static void run_two_slot_independence_and_reuse() {
  Slot slots[2];
  record_slot(slots[0], 10);
  const uint8_t* slot_zero_pointer = slots[0].bytes.data();
  const size_t slot_zero_capacity = slots[0].bytes.capacity();
  record_slot(slots[1], 40);
  expect(slots[0].bytes.data() == slot_zero_pointer, "slot one changed slot zero pointer");
  expect(slots[0].bytes.capacity() == slot_zero_capacity, "slot one changed slot zero capacity");
  expect(slots[0].bytes.data()[0] == 10, "slot zero was overwritten by slot one");
  expect(slots[1].bytes.data()[0] == 40, "slot one was overwritten by slot zero");

  // A genuinely smaller frame reuses the same owned allocation and leaves the
  // other pending slot independent.
  prepare_slot(slots[0]);
  Packet packet{.bytes = std::move(slots[0].bytes)};
  const uint8_t one[] = {99};
  packet.bytes.append(one, sizeof(one));
  slots[0].previous_used = packet.bytes.size();
  slots[0].bytes = std::move(packet.bytes);
  expect(slots[0].bytes.data() == slot_zero_pointer, "smaller frame reallocated slot zero");
  expect(slots[0].bytes.capacity() == slot_zero_capacity, "smaller frame changed slot zero capacity");
  expect(slots[1].bytes.data()[0] == 40, "slot zero reuse changed pending slot one");
}

static void run_poisoned_slack_and_alignment() {
  ByteBuffer buffer = ByteBuffer::bounded(64);
  const uint8_t first[] = {9, 8, 7, 6};
  buffer.append(first, sizeof(first));
  // 4 -> 7 grows the allocation to 8, leaving one byte of real slack.
  const uint8_t next[] = {5, 4, 3};
  buffer.append(next, sizeof(next));
  expect(buffer.capacity() == 8, "test setup did not create growth slack");
  std::memset(buffer.data() + buffer.size(), 0xA5, buffer.capacity() - buffer.size());
  const size_t before = buffer.size();
  buffer.append_zeroes(1);
  expect(buffer.size() == before + 1, "zero append changed logical size incorrectly");
  expect(buffer.data()[before] == 0, "zero append exposed poisoned growth slack");

  ByteBuffer aligned_tail = ByteBuffer::bounded(64);
  const uint8_t five[] = {1, 2, 3, 4, 5};
  aligned_tail.append(five, sizeof(five));
  const size_t used = aligned_tail.size();
  const size_t padded = aligned(used, 4);
  aligned_tail.reserve_extra(padded - used);
  std::memset(aligned_tail.data() + used, 0, padded - used);
  expect(aligned_tail.size() == used, "aligned tail changed logical size");
  expect(aligned_tail.capacity() >= padded, "aligned tail was not reserved");
  for (size_t i = used; i < padded; ++i) {
    expect(aligned_tail.data()[i] == 0, "aligned tail is not zero");
  }
}

static void run_bounded_growth_and_clone() {
  ByteBuffer buffer = ByteBuffer::bounded(10);
  const uint8_t four[] = {1, 2, 3, 4};
  buffer.append(four, sizeof(four));
  const uint8_t one = 5;
  buffer.append(&one, 1);
  expect(buffer.capacity() == 8, "bounded growth did not double below the limit");
  const uint8_t three[] = {6, 7, 8};
  buffer.append(three, sizeof(three));
  expect(buffer.size() == 8, "bounded growth setup size mismatch");
  const uint8_t ninth = 9;
  buffer.append(&ninth, 1);
  expect(buffer.capacity() == 10, "growth near the limit did not jump to the limit");
  expect(buffer.max_capacity() == 10, "growth changed the authoritative limit");

  ByteBuffer clone = buffer.clone();
  expect(clone.max_capacity() == 10, "clone lost the bounded maximum");
  expect(clone.size() == buffer.size(), "clone size mismatch");
  expect(std::memcmp(clone.data(), buffer.data(), buffer.size()) == 0, "clone bytes changed");

  ByteBuffer empty = ByteBuffer::bounded(8);
  empty.append(nullptr, 0);
  expect(empty.empty(), "zero-sized append changed an empty buffer");
}

static void run_texture_padding() {
  ByteBuffer texture = ByteBuffer::bounded(512);
  constexpr size_t bytes_per_row = 3;
  constexpr size_t rows = 2;
  constexpr size_t copy_row = 256;
  texture.append_zeroes(copy_row * rows);
  const uint8_t row0[] = {1, 2, 3};
  const uint8_t row1[] = {4, 5, 6};
  std::memcpy(texture.data(), row0, sizeof(row0));
  std::memcpy(texture.data() + copy_row, row1, sizeof(row1));
  expect(texture.data()[0] == 1 && texture.data()[2] == 3, "row zero data changed");
  expect(texture.data()[copy_row] == 4 && texture.data()[copy_row + 2] == 6, "row one data changed");
  for (size_t i = bytes_per_row; i < copy_row; ++i) {
    expect(texture.data()[i] == 0, "row zero padding is not zero");
    expect(texture.data()[copy_row + i] == 0, "row one padding is not zero");
  }
}

static void run_authoritative_maxima() {
  // These are the existing Aurora per-stream ceilings, not newly guessed
  // lower limits. The refusal checks pass max+1 before any allocation.
  constexpr std::array<size_t, 5> maxima = {
    @STAGING_MAXIMA@
  };
  for (const size_t maximum : maxima) {
    ByteBuffer buffer = ByteBuffer::bounded(maximum);
    expect(buffer.max_capacity() == maximum, "authoritative maximum was not retained");
    const uint8_t value = 0xAB;
    buffer.append(&value, 1);
    expect(buffer.data()[0] == value, "small append failed under authoritative maximum");
    expect_abort([maximum] {
      ByteBuffer overflow = ByteBuffer::bounded(maximum);
      overflow.append(nullptr, maximum + 1);
    }, "authoritative maximum accepted an oversized append");
  }
}

static void run_size_addition_overflow() {
  expect_abort([] {
    uint8_t byte = 0;
    ByteBuffer view(&byte, std::numeric_limits<size_t>::max());
    view.append(&byte, 1);
    // The first byte fits in the external view. The second append must be
    // rejected by checked_size_add before pointer arithmetic or copying.
    view.append(&byte, std::numeric_limits<size_t>::max());
  }, "SIZE_MAX length addition did not fail closed");
}

static void run_allocation_failures() {
  expect_abort([] {
    aurora::fail_allocation = true;
    ByteBuffer buffer = ByteBuffer::bounded(8);
    const uint8_t byte = 1;
    buffer.append(&byte, 1);
  }, "malloc failure did not fail closed");
  expect_abort([] {
    aurora::fail_allocation = true;
    ByteBuffer buffer = ByteBuffer::bounded(8);
    buffer.append_zeroes(1);
  }, "calloc failure did not fail closed");
  expect_abort([] {
    ByteBuffer buffer = ByteBuffer::bounded(8);
    const uint8_t byte = 1;
    buffer.append(&byte, 1);
    aurora::fail_allocation = true;
    buffer.append(&byte, 1);
  }, "realloc failure did not fail closed");
}

int main() {
  run_two_slot_independence_and_reuse();
  run_poisoned_slack_and_alignment();
  run_bounded_growth_and_clone();
  run_texture_padding();
  run_authoritative_maxima();
  run_size_addition_overflow();
  run_allocation_failures();
  std::puts("bounded staging shadows: PASS");
  return 0;
}
