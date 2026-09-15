#include "retail_draw_clock.hpp"
#include "source_frame_sequence.hpp"
#include <cassert>
#include <vector>

int main() {
    // Original startup measurements. Later batch positions are independent
    // validation observations, never arguments to the clock model.
    melee_web::RetailDrawClock clock{8100000,8108100,3388044,6732987,2};
    auto boundaries = clock.boundaries(6965);
    std::vector<unsigned> skipped;
    for (unsigned i=0;i<boundaries.size();++i) if(!boundaries[i]) skipped.push_back(i);
    assert((skipped == std::vector<unsigned>{1590,2591,3592,4593,5594,6595}));
    auto shifted = clock; shifted.first_vi_poll += 1000000;
    assert(shifted.boundaries(6965) != boundaries);
    for (int delta=-11;delta<=11;++delta) {
        auto quantized=clock; quantized.next_pad+=delta;
        assert(quantized.boundaries(6965)==boundaries);
    }
    auto unsupported=clock; unsupported.vi_period=8100000;
    bool rejected=false;
    try { (void)unsupported.boundaries(6965); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected);
    // A source batch may span browser callbacks. No intervening draw is
    // manufactured; callback counters remain accurate and final draw is kept.
    melee_web::SourceFrameSequence sequence;
    unsigned draws=0;
    auto present=[&] { ++draws; return true; };
    sequence.did_step(false); sequence.finish(present);
    assert(sequence.pending() && draws==0);
    sequence.begin_callback(); sequence.before_step(present);
    sequence.did_step(true); sequence.finish(present);
    assert(draws==1 && sequence.steps()==1 && sequence.draws()==1);
    assert(!sequence.pending());
}
