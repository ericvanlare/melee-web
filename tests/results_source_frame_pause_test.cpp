#include "results_source_pad_schedule.hpp"

#include <cassert>

int main()
{
    using Schedule = melee_web::ResultsSourceFramePauseSchedule;
    using Boundary = Schedule::Boundary;

    Schedule schedule;
    assert(schedule.all_consumed());
    assert(schedule.enqueue(560));
    assert(schedule.enqueue(570));
    assert(!schedule.enqueue(570));
    assert(!schedule.enqueue(8192));
    assert(schedule.size() == 2);

    assert(schedule.before_tick(559) == Boundary::waiting);
    assert(schedule.before_tick(560) == Boundary::due);
    assert(schedule.started());
    assert(!schedule.all_consumed());
    assert(schedule.before_tick(569) == Boundary::waiting);
    assert(schedule.before_tick(570) == Boundary::due);
    assert(schedule.all_consumed());

    schedule.clear();
    assert(schedule.all_consumed());
    assert(schedule.size() == 0);
    assert(schedule.enqueue(400));
    assert(schedule.before_tick(401) == Boundary::missed);
    assert(schedule.before_tick(401) == Boundary::missed);

    schedule.clear();
    for (unsigned frame = 0; frame < Schedule::capacity; ++frame)
        assert(schedule.enqueue(frame));
    assert(schedule.full());
    assert(!schedule.enqueue(Schedule::capacity));
}
