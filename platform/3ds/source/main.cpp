#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "aa3ds_platform.h"
#include "md5.h"

namespace {

constexpr aa3ds::Color kRimColor{0.72f, 0.82f, 0.92f, 0.9f};
constexpr aa3ds::Color kCycleColor{0.05f, 0.85f, 1.0f, 1.0f};
constexpr std::size_t kMaximumTrailSegments = 256;

bool RunCoreProbe() {
    constexpr char kProbeText[] = "armagetron-3ds-core";
    md5_state_t state{};
    md5_byte_t digest[16]{};
    md5_init(&state);
    md5_append(
        &state,
        reinterpret_cast<const md5_byte_t*>(kProbeText),
        static_cast<int>(std::strlen(kProbeText)));
    md5_finish(&state, digest);

    unsigned combined = 0;
    for (md5_byte_t byte : digest) {
        combined |= byte;
    }
    return combined != 0;
}

void TurnLeft(tCoord& direction) {
    direction = direction.Turn(0.0f, 1.0f);
}

void TurnRight(tCoord& direction) {
    direction = direction.Turn(0.0f, -1.0f);
}

}

int main() {
    if (!aa3ds::Initialize()) {
        return 1;
    }

    const Result romfsResult = romfsInit();
    const bool cStickServiceReady = R_SUCCEEDED(irrstInit());
    const bool coreProbePassed = RunCoreProbe();

    std::array<aa3ds::Segment, 4> rim{{
        {tCoord(-100.0f, -70.0f), tCoord(100.0f, -70.0f), kRimColor},
        {tCoord(100.0f, -70.0f), tCoord(100.0f, 70.0f), kRimColor},
        {tCoord(100.0f, 70.0f), tCoord(-100.0f, 70.0f), kRimColor},
        {tCoord(-100.0f, 70.0f), tCoord(-100.0f, -70.0f), kRimColor},
    }};
    std::array<aa3ds::Segment, kMaximumTrailSegments> trail;
    std::array<aa3ds::CycleMarker, 1> cycles{{
        {
            tCoord(-60.0f, 0.0f),
            tCoord(1.0f, 0.0f),
            kCycleColor,
            true
        },
    }};

    std::size_t trailCount = 0;
    std::size_t trailWriteIndex = 0;
    bool menuMode = false;
    int frameCounter = 0;

    while (aa3ds::MainLoop()) {
        const aa3ds::InputFrame input = aa3ds::PollInput();
        if (input.down & KEY_START) {
            break;
        }
        if (input.down & KEY_A) {
            menuMode = !menuMode;
        }
        if (input.down & (KEY_DLEFT | KEY_CPAD_LEFT)) {
            TurnLeft(cycles[0].direction);
        }
        if (input.down & (KEY_DRIGHT | KEY_CPAD_RIGHT)) {
            TurnRight(cycles[0].direction);
        }

        const float speed = (input.held & KEY_B) ? 0.18f : 0.48f;
        const tCoord previous = cycles[0].position;
        cycles[0].position += speed * cycles[0].direction;

        if (std::fabs(cycles[0].position.x) > 96.0f ||
            std::fabs(cycles[0].position.y) > 66.0f) {
            TurnRight(cycles[0].direction);
            cycles[0].position = previous;
        }

        if ((frameCounter++ % 4) == 0) {
            trail[trailWriteIndex] = aa3ds::Segment{
                previous,
                cycles[0].position,
                kCycleColor
            };
            trailWriteIndex = (trailWriteIndex + 1) % trail.size();
            trailCount = std::min(trailCount + 1, trail.size());
        }

        const aa3ds::MapView map{
            tCoord(-100.0f, -70.0f),
            tCoord(100.0f, 70.0f),
            rim.data(),
            rim.size(),
            trail.data(),
            trailCount,
            cycles.data(),
            cycles.size()
        };

        aa3ds::BeginFrame();
        aa3ds::DrawTopArena(map, coreProbePassed);
        if (menuMode) {
            aa3ds::DrawBottomMenuBackground(input);
        } else {
            aa3ds::DrawBottomMap(map);
        }
        aa3ds::EndFrame();
    }

    if (cStickServiceReady) {
        irrstExit();
    }
    if (R_SUCCEEDED(romfsResult)) {
        romfsExit();
    }
    aa3ds::Shutdown();
    return 0;
}
