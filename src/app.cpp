// Application menu/status behavior adapted from pinned tvterm's MIT example.
// Full notice retained at third_party/notices/tvterm.COPYRIGHT.
#include "app.h"
#include "window.h"
#include "commands.h"
#define Uses_TMenuPopup
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TSubMenu
#define Uses_TStatusLine
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_TEvent
#define Uses_MsgBox
#include <tvision/tv.h>
#include <tvterm/vtermemu.h>

AgentVisionApp::AgentVisionApp() :
    TProgInit(&initStatusLine, nullptr, &initDeskTop)
{
    for (ushort cmd : TerminalWindow::appConsts.focusedCmds()) disableCommand(cmd);
    disableCommand(cmPaste);
    TPoint available = deskTop->size;
    if (available.x < 40 || available.y < 12) {
        messageBox("Use a terminal of at least 40 columns and 14 rows.", mfError | mfOKButton);
        return;
    }
    int width = available.x * 3 / 4, height = available.y * 3 / 4;
    TRect first(0, 0, width, height);
    TRect second(available.x / 8, available.y / 8,
                 available.x / 8 + width, available.y / 8 + height);
    ready = addTerminal(first, 'A') && addTerminal(second, 'B');
}
static void onTermError(const char *reason)
{
    messageBox(mfError | mfOKButton, "Cannot create terminal: %s", reason);
}
bool AgentVisionApp::addTerminal(const TRect &bounds, char label)
{
    tvterm::VTermEmulatorFactory factory;
    auto *controller = tvterm::TerminalController::create(
        TerminalWindow::viewSize(bounds), factory, onTermError);
    if (!controller) return false;
    insertWindow(new TerminalWindow(bounds, *controller, label));
    return true;
}
TDeskTop *AgentVisionApp::initDeskTop(TRect bounds)
{
    bounds.a.y += 1;
    return new TDeskTop(bounds);
}
TStatusLine *AgentVisionApp::initStatusLine(TRect r)
{
    r.b.y = r.a.y + 1;
    auto *line = new TStatusLine(r,
        *new TStatusDef(hcDragging, hcDragging) +
            *new TStatusItem("~Arrows~ Move", kbNoKey, 0) +
            *new TStatusItem("~Shift-Arrows~ Resize", kbNoKey, 0) +
            *new TStatusItem("~Ctrl~ Faster", kbNoKey, 0) +
            *new TStatusItem("~Enter~ Done", kbNoKey, 0) +
            *new TStatusItem("~Esc~ Cancel", kbNoKey, 0) +
        *new TStatusDef(hcInputGrabbed, hcInputGrabbed) +
            *new TStatusItem("~Alt-End~ Release Input", kbAltEnd, cmReleaseInput) +
        *new TStatusDef(hcSelecting, hcSelecting) +
            *new TStatusItem("~Ctrl-C~ Copy", kbCtrlC, cmCopySelection) +
            *new TStatusItem("~Esc~ Cancel", kbEsc, cmCancelSelection) +
            *new TStatusItem(nullptr, kbCtrlB, cmMenu) +
        *new TStatusDef(hcMenu, hcMenu) +
            *new TStatusItem("~Esc~ Close Menu", kbNoKey, 0) +
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~Ctrl-B~ Open Menu", kbCtrlB, cmMenu));
    line->growMode = gfGrowHiX;
    return line;
}
void AgentVisionApp::idle()
{
    TApplication::idle();
    message(this, evBroadcast, cmCheckTerminalUpdates, nullptr);
}
void AgentVisionApp::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);
    if (event.what == evCommand && event.message.command == cmMenu) {
        openMenu();
        clearEvent(event);
    }
}
Boolean AgentVisionApp::valid(ushort command)
{
    if (command == cmQuit) {
        size_t count = 0;
        message(this, evBroadcast, cmCountLive, &count);
        return !count || messageBox(mfConfirmation | mfYesButton | mfNoButton,
            "Terminate %zu live terminal(s) and quit?", count) == cmYes;
    }
    return TApplication::valid(command);
}
// Same centered-popup mechanism as upstream; no custom menu/event framework.
class CenteredMenu final : public TMenuPopup {
public:
    explicit CenteredMenu(TMenu *menu) : TMenuPopup(TRect(0, 0, 0, 0), menu)
        { options |= ofCentered; helpCtx = hcMenu; }
    void calcBounds(TRect &bounds, TPoint) override {
        bounds = TRect((owner->size.x-size.x)/2, (owner->size.y-size.y)/2,
                      (owner->size.x+size.x)/2, (owner->size.y+size.y)/2);
    }
};
void AgentVisionApp::openMenu()
{
    TMenuItem &items =
        *new TMenuItem("Close Term", cmClose, 'W', hcNoContext, "~W~") + newLine() +
        *new TMenuItem("Next Term", cmNext, kbTab, hcNoContext, "~Tab~") +
        *new TMenuItem("Previous Term", cmPrev, kbShiftTab, hcNoContext, "~Shift-Tab~") +
        *new TMenuItem("Resize/Move", cmResize, 'R', hcNoContext, "~R~") +
        *new TMenuItem("Maximize/Restore", cmZoom, 'F', hcNoContext, "~F~") + newLine() +
        *new TMenuItem("Select Text", cmStartSelection, 'S', hcNoContext, "~S~") +
        *new TMenuItem("Paste", cmPaste, 'P', hcNoContext, "~P~") + newLine() +
        (*new TSubMenu("~M~ore...", kbNoKey, hcMenu) +
            *new TMenuItem("~G~rab Input", cmGrabInput, kbNoKey)) +
        *new TMenuItem("Suspend", cmDosShell, 'U', hcNoContext, "~U~") +
        *new TMenuItem("Exit", cmQuit, 'Q', hcNoContext, "~Q~");
    auto *popup = new CenteredMenu(new TMenu(items));
    if (ushort command = execView(popup)) {
        TEvent event {};
        event.what = evCommand;
        event.message.command = command;
        putEvent(event);
    }
    TObject::destroy(popup);
}
