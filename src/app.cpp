// Application menu/status behavior adapted from pinned tvterm's MIT example.
// Full notice retained at third_party/notices/tvterm.COPYRIGHT.
#include "app.h"
#include "window.h"
#include "commands.h"
#include "ipc_session.h"
#include <chrono>
#include <memory>
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
#define Uses_TEventQueue
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
    auto started = agentvision::CoreConnection::startSibling([this] { localStopped(); });
    connection = std::move(started.connection);
    if (!connection) {
        startupFailure = "Cannot start Go core (launch error " +
            std::to_string(static_cast<int>(started.error)) + ").";
        return;
    }
    agentvision::ConnectionEvent event;
    if (!await(agentvision::ConnectionEvent::Kind::Ready, event)) return;
    ready = addTerminal(first, 'A') && addTerminal(second, 'B');
    if (!ready) connection->shutdown();
}
bool AgentVisionApp::await(agentvision::ConnectionEvent::Kind kind,
                           agentvision::ConnectionEvent &event, agentvision::RequestId request)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        serviceCleanup();
        if (!connection->pollEvent(event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (event.kind == kind && (!request || event.request == request)) return true;
        if (event.kind == agentvision::ConnectionEvent::Kind::Lost ||
            (event.kind == agentvision::ConnectionEvent::Kind::RequestError && event.request == request)) {
            startupFailure = "Go core startup failed (contact " +
                std::to_string(static_cast<int>(event.contactError)) + ", error " +
                std::to_string(event.errorCode) + ").";
            return false;
        }
        message(this, evBroadcast, cmCoreEvent, &event);
    }
    startupFailure = "Go core startup response deadline expired.";
    connection->cancelLocal();
    serviceCleanup();
    return false;
}
bool AgentVisionApp::addTerminal(const TRect &bounds, char label)
{
    auto size = TerminalWindow::viewSize(bounds);
    auto request = connection->createSession(uint16_t(size.y), uint16_t(size.x));
    agentvision::ConnectionEvent created;
    if (!request || !await(agentvision::ConnectionEvent::Kind::Created, created, request)) {
        if (startupFailure.empty()) startupFailure = "Go core refused terminal creation.";
        return false;
    }
    auto endpoint = connection->endpoint(created.session);
    tvterm::VTermEmulatorFactory factory;
    auto transport = std::unique_ptr<tvterm::SessionTransport>(
        new agentvision::IpcSessionTransport(connection, endpoint));
    auto *controller = tvterm::TerminalController::createWithTransport(size, factory, std::move(transport));
    if (!controller) {
        startupFailure = "Cannot create terminal presentation.";
        return false;
    }
    insertWindow(new TerminalWindow(bounds, *controller, std::move(endpoint), label));
    return true;
}
void AgentVisionApp::stopPresentations()
{
    if (deskTop) message(this, evBroadcast, cmStopPresentations, nullptr);
    if (!suspended) {
        suspend(); // Actual outer-terminal restoration, before lifecycle acknowledgement.
        suspended = true;
    }
}
void AgentVisionApp::acknowledgeCleanup()
{
    std::lock_guard<std::mutex> lock(cleanupMutex);
    cleanupAcknowledged = true;
    cleanupChanged.notify_all();
}
void AgentVisionApp::localStopped()
{
    // start() can fail synchronously before assigning connection. No self-wait.
    if (std::this_thread::get_id() == uiThread) {
        stopPresentations();
        acknowledgeCleanup();
        return;
    }
    std::unique_lock<std::mutex> lock(cleanupMutex);
    cleanupRequested = true;
    TEventQueue::wakeUp();
    if (!cleanupChanged.wait_for(lock, std::chrono::milliseconds(100),
                                 [this] { return cleanupAcknowledged; })) {
        cleanupTargetMiss = true;
        // Safety barrier: missed target is not proof that the terminal restored.
        // getEvent services finite local work; never escalate before its real ack.
        cleanupChanged.wait(lock, [this] { return cleanupAcknowledged; });
    }
}
void AgentVisionApp::serviceCleanup()
{
    {
        std::lock_guard<std::mutex> lock(cleanupMutex);
        if (!cleanupRequested || cleanupAcknowledged) return;
    }
    stopPresentations();
    acknowledgeCleanup();
    // Ack gate is released before join: lifecycle may now escalate its owned child.
    if (connection) connection->join();
    if (ready && !stopped) {
        resume();
        suspended = false;
        redraw(); // Retained views and any active confirmation remain owned by TV.
    }
}
void AgentVisionApp::getEvent(TEvent &event)
{
    serviceCleanup();
    TApplication::getEvent(event);
    serviceCleanup(); // Covers nested modal loops and continuous input, not just idle.
}
void AgentVisionApp::shutDown()
{
    stopped = true;
    if (connection) connection->shutdown();
    stopPresentations();
    TApplication::shutDown();
    acknowledgeCleanup();
    if (connection) {
        auto completion = connection->join();
        if (!completion.graceful)
            cleanupFailure = "Go core cleanup was not graceful (contact " +
                std::to_string(static_cast<int>(completion.contactError)) + ", child kind " +
                std::to_string(static_cast<int>(completion.core.kind)) + ").";
    }
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
    agentvision::ConnectionEvent event;
    while (connection && connection->pollEvent(event))
        message(this, evBroadcast, cmCoreEvent, &event);
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
