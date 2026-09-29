#pragma once
#include <tvision/tv.h>
enum : ushort {
    cmGrabInput = 100, cmReleaseInput, cmStartSelection,
    cmCheckTerminalUpdates = 2000, cmTerminalUpdated, cmCountLive,
    cmCopySelection, cmCancelSelection, cmCoreEvent, cmStopPresentations
};
enum : ushort { hcMenu = 1000, hcInputGrabbed, hcSelecting };
