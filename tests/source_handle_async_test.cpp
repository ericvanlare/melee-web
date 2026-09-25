#include "source_handle_context.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

using namespace melee_web::source_handle;

namespace {

constexpr Address kCallback{0x80017A80};

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void initialize(Context& result, Address arena_lo, Address arena_hi)
{
    const std::uint32_t base = 0x20000000;
    const auto status = result.initialize(
        {Address(base), Address(base + 8), Address(base + 0x638),
         Address(base + 0x69c)},
        {arena_lo, arena_hi});
    expect(status == Status::ok, "context initialization failed");
}

const HandleView& view(const Snapshot& snapshot, Address identity)
{
    for (const auto& entry : snapshot.active) {
        if (entry.identity == identity) return entry;
    }
    std::cerr << "missing handle view\n";
    std::exit(1);
}

void test_ram_chunks_and_chain()
{
    Context state;
    initialize(state, Address(0x81200000), Address(0x81400000));
    const auto heap = state.new_handle(Address(0x81200000), Address(0x81400000));
    expect(heap.status == Status::ok, "heap creation failed");
    const auto first = state.allocate(heap.handle, 32);
    const auto large = state.allocate(heap.handle, 0x19020);
    const auto last = state.allocate(heap.handle, 32);
    expect(first.status == Status::ok && large.status == Status::ok &&
               last.status == Status::ok, "allocation setup failed");
    expect(state.free_payload(heap.handle, first.payload) == Status::ok,
           "hole creation failed");

    const auto begin = state.compact_begin(heap.handle, kCallback, 4);
    expect(begin.status == Status::compact_started && begin.started &&
               begin.state.phase == CompactPhase::awaiting_callback &&
               begin.state.move.handle == Address{},
           "RAM compaction did not start");
    const auto first_callback = state.compact_callback_transition(
        begin.state.callback_generation);
    expect(first_callback.status == Status::compact_started &&
               first_callback.state.phase == CompactPhase::waiting_ram_alarm &&
               first_callback.state.move.handle == large.handle &&
               first_callback.state.move.source == large.payload &&
               first_callback.state.move.destination == Address(0x81200000) &&
               first_callback.state.move.size == 0x19020 &&
               first_callback.state.move.offset == 0,
           "initial callback/move state is not source-shaped");
    expect(view(state.snapshot(), large.handle).lo == Address(0x81200000),
           "logical handle move was not published at start");

    const auto generation = first_callback.state.move.generation;
    const auto wrong = state.compact_ram_alarm_chunk(generation + 1);
    expect(wrong.status == Status::invalid_generation &&
               state.compact_state().move.offset == 0,
           "stale alarm was accepted");

    const auto first_chunk = state.compact_ram_alarm_chunk(generation);
    expect(first_chunk.status == Status::compact_in_progress &&
               first_chunk.copied == 0x19000 &&
               state.compact_state().move.offset == 0x19000,
           "RAM chunk bound or progress is wrong");

    const auto second_chunk = state.compact_ram_alarm_chunk(generation);
    expect(second_chunk.status == Status::compact_in_progress &&
               second_chunk.transfer_complete &&
               !second_chunk.callback_invoked &&
               second_chunk.state.phase == CompactPhase::awaiting_callback &&
               second_chunk.state.callback_handle == last.handle,
           "completed move did not expose the next source callback");

    const auto second_callback = state.compact_callback_transition(
        second_chunk.state.callback_generation);
    expect(second_callback.status == Status::compact_started &&
               second_callback.state.phase == CompactPhase::waiting_ram_alarm &&
               second_callback.state.move.handle == last.handle &&
               second_callback.state.move.source == last.payload &&
               second_callback.state.move.destination == Address(0x81200000 + 0x19020),
           "next source callback did not start the next move");
    const auto transfer_done = state.compact_ram_alarm_chunk(
        second_callback.state.move.generation);
    expect(transfer_done.status == Status::compact_in_progress &&
               transfer_done.transfer_complete &&
               transfer_done.state.phase == CompactPhase::awaiting_completion,
           "final RAM transfer did not expose completion callback");
    const auto final = state.compact_callback_transition(
        transfer_done.state.callback_generation);
    expect(final.status == Status::compact_complete && !final.transfer_complete &&
               final.callback_invoked && final.state.phase == CompactPhase::complete,
           "final RAM callback did not complete the chain");
    expect(view(state.snapshot(), last.handle).lo == Address(0x81200000 + 0x19020),
           "second logical move has the wrong destination");
}

void test_devcom_and_rejection_order()
{
    Context state;
    initialize(state, Address(0x10000000), Address(0x10200000));
    const auto heap = state.new_handle(Address(0x10000000), Address(0x10200000));
    const auto first = state.allocate(heap.handle, 32);
    const auto second = state.allocate(heap.handle, 32);
    expect(first.status == Status::ok && second.status == Status::ok,
           "DevCom setup failed");
    expect(state.free_payload(heap.handle, first.payload) == Status::ok,
           "DevCom hole creation failed");

    const auto begin = state.compact_begin(heap.handle, kCallback, 4);
    expect(begin.status == Status::compact_started &&
               begin.state.phase == CompactPhase::awaiting_callback,
           "low destination did not queue DevCom transfer");
    const auto reentrant = state.compact_begin(heap.handle, kCallback, 4);
    expect(reentrant.status == Status::reentrant_compaction,
           "reentrant compaction was accepted");
    const auto callback = state.compact_callback_transition(
        begin.state.callback_generation);
    expect(callback.status == Status::compact_started &&
               callback.state.phase == CompactPhase::waiting_devcom &&
               callback.state.move.transfer == TransferKind::devcom_1b,
           "low destination callback did not queue DevCom transfer");
    const auto stale = state.compact_devcom_complete(callback.state.move.generation + 1);
    expect(stale.status == Status::invalid_generation,
           "stale DevCom completion was accepted");
    const auto transfer_done = state.compact_devcom_complete(
        callback.state.move.generation);
    expect(transfer_done.status == Status::compact_in_progress &&
               transfer_done.transfer_complete &&
               transfer_done.state.phase == CompactPhase::awaiting_completion,
           "DevCom completion did not expose final callback in order");
    const auto done = state.compact_callback_transition(
        transfer_done.state.callback_generation);
    expect(done.status == Status::compact_complete && done.callback_invoked &&
               !done.transfer_complete,
           "DevCom final callback did not complete in order");

    Context cancelled_state;
    initialize(cancelled_state, Address(0x10000000), Address(0x10200000));
    const auto cancelled_heap = cancelled_state.new_handle(
        Address(0x10000000), Address(0x10200000));
    const auto cancelled_first = cancelled_state.allocate(cancelled_heap.handle, 32);
    const auto cancelled_second = cancelled_state.allocate(cancelled_heap.handle, 32);
    expect(cancelled_state.free_payload(cancelled_heap.handle,
                                        cancelled_first.payload) == Status::ok,
           "cancel setup failed");
    const auto cancelled_begin = cancelled_state.compact_begin(
        cancelled_heap.handle, kCallback, 4);
    const auto cancelled_callback = cancelled_state.compact_callback_transition(
        cancelled_begin.state.callback_generation);
    const auto cancelled = cancelled_state.compact_devcom_complete(
        cancelled_callback.state.move.generation, true);
    expect(cancelled.status == Status::cancelled &&
               cancelled.state.phase == CompactPhase::rejected,
           "cancelled DevCom callback was not rejected");
    expect(cancelled_state.compact_devcom_complete(
               cancelled_callback.state.move.generation).status ==
               Status::invalid_transition,
           "rejected compaction accepted a later completion");
    expect(cancelled_state.allocate(cancelled_heap.handle, 32).status ==
               Status::mutation_blocked,
           "rejected compaction allowed allocation");
    expect(cancelled_state.free_payload(cancelled_heap.handle,
               cancelled_callback.state.move.destination) == Status::mutation_blocked,
           "rejected compaction allowed release of a relocated handle");
    expect(cancelled_state.destroy(cancelled_heap.handle) == Status::mutation_blocked,
           "rejected compaction allowed heap destruction");
    (void)cancelled_second;
}

void test_missing_callback()
{
    Context state;
    initialize(state, Address(0x81200000), Address(0x81400000));
    const auto heap = state.new_handle(Address(0x81200000), Address(0x81400000));
    const auto first = state.allocate(heap.handle, 32);
    const auto second = state.allocate(heap.handle, 32);
    expect(state.free_payload(heap.handle, first.payload) == Status::ok,
           "missing callback setup failed");
    const auto before = state.snapshot();
    const auto missing = state.compact_begin(heap.handle, Address{}, 4);
    expect(missing.status == Status::missing_callback && !missing.started &&
               view(state.snapshot(), second.handle).lo == view(before, second.handle).lo,
           "missing callback mutated source placement");
}

void test_lifetime_and_mutation_guards()
{
    Context state;
    initialize(state, Address(0x81200000), Address(0x81400000));
    const auto heap = state.new_handle(Address(0x81200000), Address(0x81400000));
    const auto first = state.allocate(heap.handle, 32);
    const auto second = state.allocate(heap.handle, 32);
    expect(state.free_payload(heap.handle, first.payload) == Status::ok,
           "lifetime setup failed");
    const auto begin = state.compact_begin(heap.handle, kCallback, 4);
    const auto callback = state.compact_callback_transition(
        begin.state.callback_generation);
    expect(callback.status == Status::compact_started,
           "lifetime callback setup failed");
    const auto old_generation = callback.state.move.generation;
    expect(state.allocate(heap.handle, 32).status == Status::mutation_blocked &&
               state.free_payload(heap.handle, second.payload) ==
                   Status::mutation_blocked &&
               state.destroy(heap.handle) == Status::mutation_blocked,
           "active compacting heap accepted mutation");
    expect(state.initialize(
               {Address(0x20000000), Address(0x20000008), Address(0x20000638),
                Address(0x2000069c)},
               {Address(0x81200000), Address(0x81400000)}) ==
               Status::mutation_blocked,
           "reinitialize discarded active compaction");

    state.clear();
    initialize(state, Address(0x81200000), Address(0x81400000));
    const auto new_heap = state.new_handle(Address(0x81200000), Address(0x81400000));
    const auto new_first = state.allocate(new_heap.handle, 32);
    const auto new_second = state.allocate(new_heap.handle, 32);
    expect(state.free_payload(new_heap.handle, new_first.payload) == Status::ok,
           "post-clear setup failed");
    const auto new_begin = state.compact_begin(new_heap.handle, kCallback, 4);
    expect(new_begin.state.callback_generation != begin.state.callback_generation,
           "callback generation was reused across clear/reinitialize");
    expect(state.compact_callback_transition(old_generation).status ==
               Status::invalid_generation,
           "stale callback token crossed context lifetime");
    (void) new_second;
}

} // namespace

int main()
{
    test_ram_chunks_and_chain();
    test_devcom_and_rejection_order();
    test_missing_callback();
    test_lifetime_and_mutation_guards();
    return 0;
}
