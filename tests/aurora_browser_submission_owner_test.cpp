#include "browser_submission_owner.hpp"

#include <cassert>
#include <atomic>
#include <cstdint>
#include <iostream>

int main() {
  using Book = aurora::gfx::detail::BrowserSubmissionOwnerBook<1>;
  constexpr uint32_t kRegistered = 1;
  constexpr uint32_t kCompleted = 2;
  constexpr uint32_t kFailed = 3;
  constexpr uint32_t kImmediate = 4;
  Book owners{kRegistered, kCompleted, kFailed, kImmediate};
  std::atomic_uint64_t generation{7};

  owners.publish_registration(0, 1, 7, 0x11, 0x13);
  owners.publish_registration(0, 2, 8, 0x21, 0x23);
  generation.store(8);
  const auto oldGeneration = owners.complete_callback(0, 1, 7, generation, 41, true);
  assert(oldGeneration == aurora::gfx::detail::BrowserOwnerCallbackOutcome::Stale);
  auto current = owners.snapshot(0);
  assert(current.registrationSequence == 2);
  assert(current.capturedGeneration == 8);
  assert(current.callbackSequence == 0);
  assert(current.futureId == 0);
  assert(current.state == kRegistered);

  // A current-generation callback for the wrong registration is an owner
  // mismatch, not a stale observation. It must not mutate the live record.
  const auto wrongRegistration = owners.complete_callback(0, 99, 8, generation, 42, true);
  assert(wrongRegistration == aurora::gfx::detail::BrowserOwnerCallbackOutcome::OwnershipMismatch);
  current = owners.snapshot(0);
  assert(current.registrationSequence == 2);
  assert(current.callbackSequence == 0);
  assert(current.futureId == 0);
  assert(current.state == kRegistered);

  // AllowSpontaneous may complete before OnSubmittedWorkDone returns Future.id.
  owners.publish_registration(0, 3, 8, 0x31, 0x33);
  const auto spontaneous = owners.complete_callback(0, 3, 8, generation, 43, true);
  assert(spontaneous == aurora::gfx::detail::BrowserOwnerCallbackOutcome::Completed);
  current = owners.snapshot(0);
  assert(current.state == kCompleted);
  assert(current.callbackSequence == 43);
  assert(current.futureId == 0);
  owners.publish_future_if_current(0, 3, 8, generation, 77);
  current = owners.snapshot(0);
  assert(current.state == kCompleted);
  assert(current.callbackSequence == 43);
  assert(current.futureId == 77);

  // A duplicate callback for the same current-generation registration is an
  // ownership mismatch and leaves the completed record unchanged.
  const auto duplicate = owners.complete_callback(0, 3, 8, generation, 44, true);
  assert(duplicate == aurora::gfx::detail::BrowserOwnerCallbackOutcome::OwnershipMismatch);
  current = owners.snapshot(0);
  assert(current.state == kCompleted);
  assert(current.callbackSequence == 43);
  assert(current.futureId == 77);

  // A failed current-generation callback is terminal and records its callback
  // sequence before the existing fatal/release path consumes it.
  owners.publish_registration(0, 4, 8, 0x41, 0x43);
  const auto failed = owners.complete_callback(0, 4, 8, generation, 45, false);
  assert(failed == aurora::gfx::detail::BrowserOwnerCallbackOutcome::Failed);
  current = owners.snapshot(0);
  assert(current.state == kFailed);
  assert(current.callbackSequence == 45);

  // An immediate-release owner has no registered callback; a callback in the
  // same generation must fail closed as a current owner mismatch.
  owners.mark_immediate(0, 8);
  const auto immediate = owners.complete_callback(0, 5, 8, generation, 46, true);
  assert(immediate == aurora::gfx::detail::BrowserOwnerCallbackOutcome::OwnershipMismatch);
  current = owners.snapshot(0);
  assert(current.state == kImmediate);
  assert(current.callbackSequence == 0);

  // A late callback and Future publication from a prior generation cannot
  // mutate a reused slot. The current registration remains untouched.
  owners.publish_registration(0, 6, 9, 0x51, 0x53);
  generation.store(9);
  const auto late = owners.complete_callback(0, 4, 8, generation, 47, true);
  assert(late == aurora::gfx::detail::BrowserOwnerCallbackOutcome::Stale);
  owners.publish_future_if_current(0, 4, 8, generation, 88);
  current = owners.snapshot(0);
  assert(current.registrationSequence == 6);
  assert(current.capturedGeneration == 9);
  assert(current.callbackSequence == 0);
  assert(current.futureId == 0);
  assert(current.state == kRegistered);

  // The current registration can still complete normally after stale events.
  const auto final = owners.complete_callback(0, 6, 9, generation, 48, true);
  assert(final == aurora::gfx::detail::BrowserOwnerCallbackOutcome::Completed);
  current = owners.snapshot(0);
  assert(current.state == kCompleted);
  assert(current.callbackSequence == 48);
  std::cout << "submission owner publication: PASS\n";
  return 0;
}
