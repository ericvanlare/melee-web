#include "animation_fixture.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

using namespace animation_test;
using melee_web::AnimationPose;
using melee_web::HsdAnimation;

namespace {
void near(float actual, float expected, const char* message)
{
    check(std::isfinite(actual) && std::fabs(actual - expected) <= 0.00001f, message);
}
std::vector<AnimationPose> bind_pose(std::size_t count = 2)
{
    return std::vector<AnimationPose>(count, {{.25f, .5f, .75f}, {1, 2, 3}, {1, 1, 1}, 1});
}

void interpolation_and_seek()
{
    auto data = Fixture().animation();
    HsdAnimation animation(data, bind_pose());
    animation.request(0);
    animation.advance();
    near(animation.frame(), 0, "FIRST_PLAY evaluates exactly the requested frame");
    near(animation.pose()[1].rotation[0], 0, "linear curve starts at its first value");
    check(animation.pose()[0].flags == 1 && animation.pose()[1].flags == 9,
          "CLASSICAL_SCALE applies only to nodes with channels, retaining skeletal bits");
    near(animation.pose()[0].rotation[0], .25f, "empty node retains bind pose");
    near(animation.pose()[1].rotation[1], .5f, "untracked axes retain bind pose");
    animation.request(5);
    animation.advance();
    near(animation.pose()[1].rotation[0], 5, "seeking uses original FObj interpolation");
    animation.advance();
    near(animation.frame(), 6, "subsequent evaluation advances one frame");
    for (unsigned i = 0; i < 4; ++i) animation.advance();
    check(animation.finished(), "nonlooping original AObj stops at the clip end");
    near(animation.pose()[1].rotation[0], 10, "last linear value survives stop");
    animation.advance();
    near(animation.frame(), 10, "stopped animation does not silently restart");
    animation.request(0); animation.advance();
    near(animation.pose()[1].rotation[0], 0, "explicit rewind resets FObj state");
    rejects([&] { animation.request(11); });
    rejects([&] { animation.request(std::numeric_limits<float>::quiet_NaN()); });
}

void original_hermite_and_constant()
{
    for (const unsigned opcode : {3U, 4U}) {
        Fixture fixture(segment(opcode, 2, 6, 8));
        auto data = fixture.animation();
        HsdAnimation animation(data, bind_pose());
        animation.request(2); animation.advance();
        // Hermite with zero tangents at t/T=1/4: 2 + 4*(3/16 - 2/64).
        near(animation.pose()[1].rotation[0], 2.625f, "original spline.c Hermite basis matches analytic curve");
    }
    Fixture slopes(segment(4, 0, 8, 8, 1, 1));
    HsdAnimation smooth(slopes.animation(), bind_pose());
    smooth.request(2); smooth.advance();
    near(smooth.pose()[1].rotation[0], 2, "nonzero Hermite tangents preserve a straight line");
    Fixture constant(segment(1, 2, 6, 8));
    HsdAnimation step(constant.animation(), bind_pose());
    step.request(7); step.advance();
    near(step.pose()[1].rotation[0], 2, "constant segment holds before the key boundary");
    step.request(8); step.advance();
    near(step.pose()[1].rotation[0], 6, "constant segment changes at the key boundary");
    Bytes key{6}; little_float(key, 7.5f);
    HsdAnimation keyed(Fixture(key).animation(), bind_pose());
    keyed.request(0); keyed.advance();
    near(keyed.pose()[1].rotation[0], 7.5f, "single KEY datum launches explicitly without an interpolation pair");
}

void source_flags_and_scale()
{
    Fixture fixture;
    put32(fixture.data, Fixture::root + 4, 0x20000000U);
    HsdAnimation loop(fixture.animation(), bind_pose());
    loop.request(9); loop.advance(); loop.advance();
    near(loop.frame(), 0, "original AOBJ_LOOP uses end-frame modulo");
    near(loop.pose()[1].rotation[0], 0, "loop rewinds the original channel state");
    check(!loop.finished(), "looping AObj remains active");
    put32(fixture.data, Fixture::root + 4, 0x10000000U);
    HsdAnimation disabled(fixture.animation(), bind_pose());
    disabled.request(5); disabled.advance();
    near(disabled.pose()[1].rotation[0], .25f, "AOBJ_NO_UPDATE advances without changing pose");
    const auto tiny = segment(1, -.0001f, -.0001f, 10);
    Fixture scale(tiny, 8);
    put32(scale.data, Fixture::root, 0);
    auto bind = bind_pose(); bind[0].flags = bind[1].flags = 9;
    HsdAnimation scaling(scale.animation(), bind);
    scaling.request(0); scaling.advance();
    near(scaling.pose()[1].scale[0], .001f, "ordinary JObj scale callback clamps tiny negative scale positive");
    check(scaling.pose()[0].flags == 9 && scaling.pose()[1].flags == 1,
          "nonclassical attachment changes only animated nodes");
}

void inspection_loop_boundary()
{
    HsdAnimation animation(Fixture().animation(), bind_pose());
    animation.request(0); animation.advance();
    for (unsigned tick = 1; tick <= 20; ++tick) {
        melee_web::advance_inspection_loop(animation);
        near(animation.frame(), float(tick % 10), "ten-frame inspection loop repeats every ten clock ticks");
        near(animation.pose()[1].rotation[0], float(tick % 10),
             "viewer evaluates the original frame-zero pose on the wrap tick");
        check(!animation.finished(), "inspection loop leaves the newly requested source clip active");
    }
}

void signed_stream_and_startframe()
{
    Fixture fixture({0x11, 0, 250, 10, 0, 250}); // signed -1536 in little endian, divided by 1024
    fixture.data[Fixture::track + 5] = 0x2a;
    HsdAnimation signed_value(fixture.animation(), bind_pose());
    signed_value.request(0); signed_value.advance();
    near(signed_value.pose()[1].rotation[0], -1.5f, "original parser handles signed little-endian fixed point");
    fixture = Fixture();
    fixture.data[Fixture::track + 2] = fixture.data[Fixture::track + 3] = 255;
    HsdAnimation delayed(fixture.animation(), bind_pose());
    delayed.request(0); delayed.advance();
    near(delayed.pose()[1].rotation[0], .25f, "signed negative start frame defers channel updates");
    delayed.advance();
    near(delayed.pose()[1].rotation[0], 0, "deferred channel starts when original time reaches zero");
}

void rejected_construction_and_failed_evaluation()
{
    const auto data = Fixture().animation();
    rejects([&] { HsdAnimation wrong_count(data, bind_pose(1)); });
    auto bind = bind_pose(); bind[1].scale[0] = std::numeric_limits<float>::infinity();
    rejects([&] { HsdAnimation nonfinite(data, bind); });
    for (const auto flags : {0x20000U, 0x200000U, 0x400000U}) {
        bind = bind_pose(); bind[1].flags |= flags;
        rejects([&] { HsdAnimation unsupported_callbacks(data, bind); });
    }
    auto corrupt = data; corrupt.tracks[0].bytes = {4, 0};
    rejects([&] { HsdAnimation invalid_stream(corrupt, bind_pose()); });
    Fixture overflow(segment(2, -std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 10));
    HsdAnimation animation(overflow.animation(), bind_pose());
    animation.request(5);
    rejects([&] { animation.advance(); });
    near(animation.pose()[1].rotation[0], .25f, "failed evaluation does not publish a partial pose");
    rejects([&] { animation.advance(); });
}

void local_clip(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    Bytes bytes{std::istreambuf_iterator<char>(file), {}};
    melee_web::DatArchive archive(bytes);
    const auto& symbol = archive.public_symbols().at(0);
    melee_web::DatAnimation data(archive, symbol.data_offset);
    HsdAnimation animation(data, bind_pose(data.node_counts.size()));
    animation.request(0); animation.advance();
    const std::vector<AnimationPose> initial(animation.pose().begin(), animation.pose().end());
    const auto final_frame = static_cast<unsigned>(std::ceil(data.end_frame));
    const auto sample_frame = std::max(1U, final_frame / 2);
    for (unsigned frame = 1; frame <= final_frame; ++frame) {
        animation.advance();
        if (frame == sample_frame) {
            unsigned changed = 0;
            for (std::size_t n = 0; n < animation.pose().size(); ++n)
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (initial[n].rotation[axis] != animation.pose()[n].rotation[axis] ||
                        initial[n].translation[axis] != animation.pose()[n].translation[axis]) ++changed;
            check(changed > 0, "real clip changes ordinary joint channels");
            std::cout << symbol.name << ": " << data.node_counts.size() << " nodes, " << data.tracks.size()
                      << " tracks; " << changed << " changed channel axes at frame " << sample_frame << '\n';
        }
    }
    check(animation.finished(), "real nonlooping clip reaches its original end frame");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        interpolation_and_seek();
        original_hermite_and_constant();
        source_flags_and_scale();
        inspection_loop_boundary();
        signed_stream_and_startframe();
        rejected_construction_and_failed_evaluation();
        for (int i = 1; i < argc; ++i) local_clip(argv[i]);
        std::cout << "Original HSD animation trace: passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
