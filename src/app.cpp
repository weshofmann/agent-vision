// Application menu/status behavior adapted from pinned tvterm's MIT example.
// Full notice retained at third_party/notices/tvterm.COPYRIGHT.
#include "app.h"
#include "window.h"
#include "commands.h"
#include "ipc_session.h"
#include <chrono>
#include <memory>
#include <utility>
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
    const bool created = addTerminal(first, 'A') && addTerminal(second, 'B');
    {
        std::lock_guard<std::mutex> lock(cleanupMutex);
        // Serialize the handoff to runtime with the callback's cleanup boundary.
        ready = created && startupAdoption.mayAdopt();
        if (created && !ready) startupFailure = "Go core startup failed after contact cleanup.";
    }
    if (!ready) connection->shutdown();
}
bool AgentVisionApp::startupAllowed()
{
    std::lock_guard<std::mutex> lock(cleanupMutex);
    if (startupAdoption.mayAdopt()) return true;
    startupFailure = "Go core startup failed after contact cleanup.";
    return false;
}
bool AgentVisionApp::await(agentvision::ConnectionEvent::Kind kind,
                           agentvision::ConnectionEvent &event, agentvision::RequestId request)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        serviceCleanup();
        if (!startupAllowed()) return false;
        if (!connection->pollEvent(event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (event.kind == kind && (!request || event.request == request))
            return startupAllowed();
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
    std::unique_lock<std::mutex> adoption(cleanupMutex);
    if (!startupAdoption.mayAdopt()) {
        startupFailure = "Go core startup failed after contact cleanup.";
        return false;
    }
    // Presentation adoption is local, finite work. Holding only this UI gate
    // makes it precede cleanup beginning; no socket operation or core join occurs.
    tvterm::VTermEmulatorFactory factory;
    auto transport = std::unique_ptr<tvterm::SessionTransport>(
        new agentvision::IpcSessionTransport(connection, endpoint));
    auto *controller = tvterm::TerminalController::createWithTransport(size, factory, std::move(transport));
    if (!controller) {
        startupFailure = "Cannot create terminal presentation.";
        return false;
    }
    insertWindow(new TerminalWindow(bounds, *controller, std::move(endpoint),
                                    std::string(1, label)));
    return true;
}
void AgentVisionApp::newTerminal()
{
    if (!connection || !ready || stopped || admissionClosed) return;
    size_t views = 0;
    message(this, evBroadcast, cmCountViews, &views);
    if (views + pendingCreates.size() + preparedCreates.size() >= 16) {
        messageBox("Terminal capacity is 16 windows; close one to create another.", mfError | mfOKButton);
        return;
    }
    const auto id = nextViewId++;
    TPoint available = deskTop->size;
    const int width = available.x * 3 / 4, height = available.y * 3 / 4;
    const int x = int((id * 3) % unsigned(available.x - width + 1));
    const int y = int((id * 2) % unsigned(available.y - height + 1));
    TRect bounds(x, y, x + width, y + height);
    auto size = TerminalWindow::viewSize(bounds);
    // Create is locally admitted and queued by CoreConnection; no reply wait on the UI thread.
    auto request = connection->createSession(uint16_t(size.y), uint16_t(size.x));
    if (!request) {
        messageBox("Terminal admission is unavailable.", mfError | mfOKButton);
        return;
    }
    {
        std::lock_guard<std::mutex> gate(cleanupMutex);
        if (startupAdoption.mayAdopt() && !admissionClosed && !stopped) {
            pendingCreates.emplace(request, PendingCreate{bounds, TerminalWindow::labelFor(id)});
            return;
        }
    }
    // A late Created has no pending owner and is closed by serviceCoreEvents if still authoritative.
}
bool AgentVisionApp::mayInsertTerminal()
{
    return TopView() == this && deskTop &&
           !(deskTop->current && (deskTop->current->state & sfDragging)) &&
           canMoveFocus();
}
void AgentVisionApp::clearCreates()
{
    std::vector<PreparedCreate> local;
    {
        std::lock_guard<std::mutex> gate(cleanupMutex);
        pendingCreates.clear();
        local.swap(preparedCreates);
    }
    for (auto &item : local)
        item.controller->shutDown(); // Local cancellation only; no shared connection close.
}
void AgentVisionApp::adoptPrepared()
{
    if (!mayInsertTerminal()) return;
    while (!preparedCreates.empty() && mayInsertTerminal()) {
        auto item = std::move(preparedCreates.front());
        preparedCreates.erase(preparedCreates.begin());
        bool inserted = false;
        bool attempted = false;
        {
            std::lock_guard<std::mutex> gate(cleanupMutex);
            if (startupAdoption.mayAdopt() && !admissionClosed && !stopped) {
                attempted = true;
                inserted = insertWindow(new TerminalWindow(item.bounds, *item.controller,
                                           item.endpoint, std::move(item.label))) != nullptr;
            }
        }
        if (!inserted) {
            // insertWindow destroys a rejected view; without an attempt, ownership stayed here.
            if (!attempted) item.controller->shutDown();
            connection->closeSession(item.endpoint->metadata().id);
            if (attempted) creationFailure = "Terminal presentation is unavailable.";
        }
    }
}
void AgentVisionApp::serviceCoreEvents()
{
    if (servicingCoreEvents || !connection) return;
    servicingCoreEvents = true;
    struct Guard { bool &flag; ~Guard() { flag = false; } } guard{servicingCoreEvents};
    agentvision::ConnectionEvent event;
    for (unsigned i = 0; i < 4 && connection->pollEvent(event); ++i) {
        if (event.kind == agentvision::ConnectionEvent::Kind::Created) {
            auto pending = pendingCreates.find(event.request);
            if (pending == pendingCreates.end()) {
                TerminalSessionQuery query{event.session, false};
                message(this, evBroadcast, cmFindSession, &query);
                bool prepared = false;
                for (const auto &item : preparedCreates)
                    prepared |= item.endpoint->metadata().id == event.session;
                if (!query.found && !prepared)
                    connection->closeSession(event.session);
            } else {
                auto item = std::move(pending->second);
                pendingCreates.erase(pending);
                auto endpoint = connection->endpoint(event.session);
                bool allowed;
                {
                    std::lock_guard<std::mutex> gate(cleanupMutex);
                    allowed = startupAdoption.mayAdopt() && !admissionClosed && !stopped;
                    if (allowed && endpoint) {
                        auto size = TerminalWindow::viewSize(item.bounds);
                        tvterm::VTermEmulatorFactory factory;
                        auto transport = std::unique_ptr<tvterm::SessionTransport>(
                            new agentvision::IpcSessionTransport(connection, endpoint));
                        auto *controller = tvterm::TerminalController::createWithTransport(
                            size, factory, std::move(transport));
                        if (controller)
                            preparedCreates.push_back({item.bounds, std::move(item.label),
                                                       endpoint, controller});
                        else
                            allowed = false;
                    }
                }
                if (!allowed || !endpoint) {
                    connection->closeSession(event.session);
                    if (allowed && !endpoint)
                        creationFailure = "Terminal presentation is unavailable.";
                    else if (!allowed && endpoint && !admissionClosed && !stopped)
                        creationFailure = "Terminal presentation is unavailable.";
                }
            }
        } else if (event.kind == agentvision::ConnectionEvent::Kind::RequestError) {
            auto pending = pendingCreates.find(event.request);
            if (pending != pendingCreates.end()) {
                pendingCreates.erase(pending);
                creationFailure = event.errorCode == 5 ? "Terminal capacity is 16 sessions." :
                    event.errorCode == 6 ? "Cannot launch terminal shell." :
                    "Terminal creation failed.";
            }
        } else if (event.kind == agentvision::ConnectionEvent::Kind::Lost) {
            clearCreates();
        }
        message(this, evBroadcast, cmCoreEvent, &event);
    }
    adoptPrepared();
    message(this, evBroadcast, cmCheckTerminalUpdates, nullptr);
    if (!creationFailure.empty() && TopView() == this && mayInsertTerminal()) {
        auto failure = std::move(creationFailure);
        creationFailure.clear();
        messageBox(mfError | mfOKButton, "%s", failure.c_str());
    }
}
void AgentVisionApp::stopPresentations()
{
    clearCreates();
    if (deskTop) message(this, evBroadcast, cmStopPresentations, nullptr);
    if (!suspended) {
        suspend(); // Actual outer-terminal restoration, before lifecycle acknowledgement.
        suspended = true;
    }
}
void AgentVisionApp::acknowledgeCleanup()
{
    std::lock_guard<std::mutex> lock(cleanupMutex);
    startupAdoption.cleanupBegun();
    cleanupTarget.acknowledge(agentvision::RestorationTarget::Clock::now());
    cleanupAcknowledged = true;
    cleanupChanged.notify_all();
}
void AgentVisionApp::localStopped()
{
    const auto entry = agentvision::RestorationTarget::Clock::now();
    // start() can fail synchronously before assigning connection. No self-wait.
    if (std::this_thread::get_id() == uiThread) {
        {
            std::lock_guard<std::mutex> lock(cleanupMutex);
            startupAdoption.cleanupBegun();
            cleanupTarget.begin(entry);
        }
        stopPresentations();
        acknowledgeCleanup();
        std::lock_guard<std::mutex> lock(cleanupMutex);
        cleanupTargetMiss = cleanupTarget.missed();
        return;
    }
    std::unique_lock<std::mutex> lock(cleanupMutex);
    startupAdoption.cleanupBegun();
    cleanupTarget.begin(entry); // Includes time awaiting this mutex; no target reset.
    cleanupRequested = true;
    TEventQueue::wakeUp();
    const bool timedOut = !cleanupChanged.wait_until(lock, cleanupTarget.deadline(),
                                 [this] { return cleanupAcknowledged; });
    if (timedOut) {
        cleanupTargetMiss = true;
        // Safety barrier: missed target is not proof that the terminal restored.
        // getEvent services finite local work; never escalate before its real ack.
        cleanupChanged.wait(lock, [this] { return cleanupAcknowledged; });
    }
    // Predicate success may occur after the deadline. Judge the first real proof,
    // not wait_until's return value or the time this waiter gets scheduled again.
    cleanupTargetMiss = cleanupTarget.missed(timedOut);
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
    serviceCoreEvents();
    TApplication::getEvent(event);
    serviceCleanup(); // Covers nested modal loops and continuous input, not just idle.
    serviceCoreEvents();
}
void AgentVisionApp::shutDown()
{
    {
        std::lock_guard<std::mutex> gate(cleanupMutex);
        admissionClosed = true;
        startupAdoption.cleanupBegun();
    }
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
    serviceCoreEvents();
    TApplication::idle();
}
void AgentVisionApp::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);
    if (event.what == evCommand && event.message.command == cmMenu) {
        openMenu();
        clearEvent(event);
    } else if (event.what == evCommand && event.message.command == cmNewTerminal) {
        newTerminal();
        clearEvent(event);
    }
}
Boolean AgentVisionApp::valid(ushort command)
{
    if (command == cmQuit) {
        size_t count = 0;
        message(this, evBroadcast, cmCountLive, &count);
        count += pendingCreates.size() + preparedCreates.size();
        const bool confirmed = !count || messageBox(mfConfirmation | mfYesButton | mfNoButton,
            "Terminate %zu live/starting terminal(s) and quit?", count) == cmYes;
        if (confirmed) {
            std::lock_guard<std::mutex> gate(cleanupMutex);
            admissionClosed = true;
            startupAdoption.cleanupBegun();
        }
        return confirmed;
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
        *new TMenuItem("New Term", cmNewTerminal, 'N', hcNoContext, "~N~") +
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
