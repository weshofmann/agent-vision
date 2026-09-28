#pragma once
#include <cstdint>
#include <tvision/tv.h>
enum : ushort {
    cmGrabInput = 100, cmReleaseInput, cmStartSelection,
    cmCheckTerminalUpdates = 2000, cmTerminalUpdated, cmCountLive,
    cmCopySelection, cmCancelSelection, cmCoreEvent, cmStopPresentations,
    cmNewTerminal, cmCountViews, cmFindSession
};
enum : ushort { hcMenu = 1000, hcInputGrabbed, hcSelecting };
struct TerminalSessionQuery { uint64_t id; bool found; };
