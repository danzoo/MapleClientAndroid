//  Where the frame's time actually goes.
//
//  The multicore plan ranks its work by costs that were estimated rather than measured, and the two
//  biggest candidates for what to build next (moving map loading off the render thread, and running
//  entity physics on several cores) are only worth their risk if the numbers behind them are real.
//  This is the instrument that decides that: a handful of named spans around the parts of the frame,
//  a few counters around the texture atlas, and one line of output every so often.
//
//  Everything here is written from the render thread only, which is what lets it be plain statics
//  instead of anything synchronised. The decode worker keeps its own tally and the frame's pump
//  hands it over once a frame, so that the one thread that has to keep up with the picture is still
//  the only one writing here.
#pragma once

#include <cstdint>

namespace ms {

// One measured part of the frame. Adding one means adding it before Count and giving it a name in
// Perf.cpp; where it is reported is decided there, not by this order.
enum class PerfSection : uint8_t {
    // Game::update(), split by the thing it asks for.
    WindowUpdate,
    StageUpdate,
    UIUpdate,
    NetProcess,
    // Game::draw(), split the same way. DrawEnd is the batch submission, not the swap: that happens
    // after this returns and is what the frame interval already accounts for.
    DrawBegin,
    StageDraw,
    UIDraw,
    DrawEnd,
    // Stage::load(), which is the one cost that is not per-frame and is the reason a portal feels
    // like a punch. Reported as its own line because a mean over a window of frames would hide it.
    MapLoad,
    // The part of Stage::update() that walks entities through the terrain - every update call that
    // takes the Physics reference. Reported inside StageUpdate, because whether it is a large share
    // of it is exactly the question the plan answers without asking: splitting that across cores
    // cannot buy back more than it measures.
    PhysicsUpdate,
    // The per-frame pump: sprites the decode worker finished, copied into their bordered frame and
    // handed to the driver. Split out from the draw sections because it is the only part of the
    // upload path that is left on this thread once inflating moved off it.
    AtlasPump,
    // The two halves of moving one object, which add up to it: the walk of the map's foothold tree
    // that decides which platform the object is on, and everything after that - the forces, the
    // collision checks and the pieces of the displacement. A spike that belongs to neither is a
    // spike in whoever asked for the move.
    FootholdUpdate,
    MoveApply,
    Count
};

// Something counted rather than timed.
enum class PerfCounter : uint8_t {
    // Sprites decoded and uploaded into an atlas during the window, and what that cost in texels.
    AtlasUploads,
    AtlasBytes,
    // Times an atlas was repacked from empty, which throws away every cached offset in it.
    AtlasWipes,
    // Scene atlas occupancy, sampled at each upload and averaged, in tenths of a percent.
    AtlasOccupancy,
    // How many objects Physics moved during the window. A cost that is small with four objects on
    // screen is not the same thing as a cost that is small with forty, and only one of those says
    // there is nothing left to win by spreading the work out.
    ObjectsMoved,
    // What the decode worker did during the window, in microseconds and pixels. It reports itself
    // because it no longer runs on the thread the frame is timed on: if the frame is fast and this
    // is high, the worker is doing its job; if the frame is fast and this is zero, nothing new
    // arrived and the numbers say nothing either way.
    WorkerDecodeUs,
    WorkerDecodePixels,
    // Sprites asked for but not yet in the atlas, sampled once a frame. A number that only grows is
    // the worker falling behind, which no frame timing would show as a hitch.
    AtlasPending,
    // Quads left out of a frame because their picture had not arrived yet. This is the artifact the
    // async upload pays for, so it is counted rather than guessed at: a few hundred over the second
    // it takes a map to fill its atlas is pop-in, the same number holding steady every window is a
    // screen flickering.
    AtlasSkips,
    // Frames held back instead of shown, because they were drawn over the top of art that had not
    // arrived. This is what the pop-in is paid for, so it is counted: held frames are the loading
    // pause, and a number that never comes down is a canvas that never arrives.
    AtlasHeld,
    Count
};

// Times one section for as long as it is in scope.
class PerfScope {
public:
    explicit PerfScope(PerfSection section);
    ~PerfScope();

    PerfScope(const PerfScope &) = delete;
    PerfScope &operator=(const PerfScope &) = delete;

private:
    PerfSection section_;
};

void perf_count(PerfCounter counter, int64_t amount);

// Rolls the window. Called once a frame with the interval that just elapsed, in microseconds; it
// reports and resets every Constants::PERF_LOG_FRAMES frames.
void perf_frame(int64_t elapsed_us);
}  // namespace ms
