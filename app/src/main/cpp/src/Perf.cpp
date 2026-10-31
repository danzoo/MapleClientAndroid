//  See Perf.h. The output is one line every Constants::PERF_LOG_FRAMES frames, under its own tag so
//  that it can be read straight off adb:
//
//    FramePerf  120f 2.00s | upd win 0.00/0.00 stage 0.54/7.66 physics 0.46/7.57 fh 0.30/1.20
//          move 0.16/6.40 ui 0.27/0.52 net 0.02/0.64 | drw begin 0.05/0.20 draw 1.03/1.77
//          uidraw 1.53/2.78 end 12.95/16.90 pump 0.40/4.10 | frame 16.67 max 18.84 slow 0
//          | mapload x2 210.4ms | atlas up 345 2.1MB wipes 1 occ 62% moved 21.9/f
//          | decode 412.5ms 8.3Mpx pend 6
//
//  Every timing is mean/worst in milliseconds over the window. The worst number is the one that
//  matters: a section that averages 0.5ms but peaks at 40ms is a hitch, and an average says nothing
//  about whether splitting it across cores would help.
#include "Perf.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "Configuration.h"
#include "Constants.h"

namespace ms {
namespace {
using clock = std::chrono::steady_clock;

struct Span {
    int64_t total_us = 0;
    int64_t max_us = 0;
    uint32_t hits = 0;
};

constexpr size_t SECTION_COUNT = static_cast<size_t>(PerfSection::Count);
constexpr size_t COUNTER_COUNT = static_cast<size_t>(PerfCounter::Count);

Span spans[SECTION_COUNT];
int64_t starts[SECTION_COUNT];
int64_t counters[COUNTER_COUNT];

Span &span(PerfSection section) {
    return spans[static_cast<size_t>(section)];
}

// The window, as distinct from the sections: what the loop itself saw.
uint32_t window_frames = 0;
int64_t window_us = 0;
int64_t window_max_us = 0;
uint32_t window_slow = 0;

// A frame slower than this dropped at least one picture, which is what the count is for.
constexpr int64_t SLOW_FRAME_US = 20000;

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch())
        .count();
}

const char *const kSectionNames[SECTION_COUNT] = {
    "win", "stage", "ui", "net", "begin", "draw", "uidraw", "end", "mapload", "physics", "pump",
    "fh", "move"
};

void append(char *out, size_t &at, size_t capacity, const char *format, ...) {
    if (at >= capacity) {
        return;
    }

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(out + at, capacity - at, format, args);
    va_end(args);

    if (written > 0) {
        at += static_cast<size_t>(written);
    }
}

// One section as "<name> mean/max". A section that never ran in the window reports as 0/0 rather
// than being left out, so the columns stay in the same order from line to line.
void append_span(char *out, size_t &at, size_t capacity, PerfSection section, uint32_t frames) {
    const Span &measured = span(section);
    const double mean = frames ? static_cast<double>(measured.total_us) / frames / 1000.0 : 0.0;
    const double worst = static_cast<double>(measured.max_us) / 1000.0;

    append(out, at, capacity, " %s %.2f/%.2f", kSectionNames[static_cast<size_t>(section)], mean,
           worst);
}

void report(uint32_t frames, int64_t elapsed_us) {
    constexpr size_t CAPACITY = 768;
    char line[CAPACITY];
    size_t at = 0;

    append(line, at, CAPACITY, "%uf %.2fs | upd", frames, static_cast<double>(elapsed_us) / 1e6);

    for (PerfSection s : {PerfSection::WindowUpdate, PerfSection::StageUpdate,
                          PerfSection::PhysicsUpdate, PerfSection::FootholdUpdate,
                          PerfSection::MoveApply, PerfSection::UIUpdate,
                          PerfSection::NetProcess}) {
        append_span(line, at, CAPACITY, s, frames);
    }

    append(line, at, CAPACITY, " | drw");

    for (PerfSection s : {PerfSection::DrawBegin, PerfSection::StageDraw, PerfSection::UIDraw,
                          PerfSection::DrawEnd, PerfSection::AtlasPump}) {
        append_span(line, at, CAPACITY, s, frames);
    }

    append(line, at, CAPACITY, " | frame %.2f max %.2f slow %u",
           static_cast<double>(window_us) / frames / 1000.0,
           static_cast<double>(window_max_us) / 1000.0, window_slow);

    // Reported on its own because it is not a per-frame cost: dividing it by the window would turn
    // one 200ms load in two seconds of frames into 100ms, which describes nothing that happened.
    const Span &load = span(PerfSection::MapLoad);
    if (load.hits > 0) {
        append(line, at, CAPACITY, " | mapload x%u %.1fms", load.hits,
               static_cast<double>(load.total_us) / 1000.0);
    }

    append(line, at, CAPACITY, " | atlas up %lld %.1fMB wipes %lld occ %.0f%%",
           static_cast<long long>(counters[static_cast<size_t>(PerfCounter::AtlasUploads)]),
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::AtlasBytes)])
               / (1024.0 * 1024.0),
           static_cast<long long>(counters[static_cast<size_t>(PerfCounter::AtlasWipes)]),
           counters[static_cast<size_t>(PerfCounter::AtlasUploads)]
               ? static_cast<double>(counters[static_cast<size_t>(PerfCounter::AtlasOccupancy)])
                     / counters[static_cast<size_t>(PerfCounter::AtlasUploads)] / 10.0
               : 0.0);

    append(line, at, CAPACITY, " skip %.0f/f held %.0f/f moved %.1f/f",
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::AtlasSkips)]) / frames,
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::AtlasHeld)]) / frames,
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::ObjectsMoved)]) / frames);

    // The worker's own numbers, next to the pump that consumes them: together they say whether the
    // inflating really left the frame or merely moved somewhere else that still has to keep up.
    append(line, at, CAPACITY, " | decode %.1fms %.1fMpx pend %.0f",
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::WorkerDecodeUs)]) / 1000.0,
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::WorkerDecodePixels)])
               / (1024.0 * 1024.0),
           static_cast<double>(counters[static_cast<size_t>(PerfCounter::AtlasPending)]) / frames);

#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "FramePerf", "%s", line);
#else
    std::printf("%s\n", line);
#endif

    for (Span &measured : spans) {
        measured = Span();
    }
    for (int64_t &value : counters) {
        value = 0;
    }

    window_frames = 0;
    window_us = 0;
    window_max_us = 0;
    window_slow = 0;
}
}  // namespace

PerfScope::PerfScope(PerfSection section) : section_(section) {
    if (Configuration::get().get_show_fps()) {
        starts[static_cast<size_t>(section)] = now_us();
    }
}

PerfScope::~PerfScope() {
    if (!Configuration::get().get_show_fps()) {
        return;
    }

    Span &measured = span(section_);
    const int64_t took = now_us() - starts[static_cast<size_t>(section_)];

    measured.total_us += took;
    measured.hits++;

    if (took > measured.max_us) {
        measured.max_us = took;
    }
}

void perf_count(PerfCounter counter, int64_t amount) {
    // No check of whether the report is on: this is called once per object the terrain moves, and a
    // setting read there would be charged to the very measurement it guards. perf_frame() keeps the
    // totals from accumulating while nobody is looking.
    counters[static_cast<size_t>(counter)] += amount;
}

void perf_frame(int64_t elapsed_us) {
    if (!Configuration::get().get_show_fps()) {
        for (int64_t &value : counters) {
            value = 0;
        }

        window_frames = 0;
        window_us = 0;
        window_max_us = 0;
        window_slow = 0;
        return;
    }

    window_frames++;
    window_us += elapsed_us;

    if (elapsed_us > window_max_us) {
        window_max_us = elapsed_us;
    }
    if (elapsed_us > SLOW_FRAME_US) {
        window_slow++;
    }

    if (window_frames >= Constants::PERF_LOG_FRAMES) {
        report(window_frames, window_us);
    }
}
}  // namespace ms
