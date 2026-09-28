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
#define Uses_TDialog
#define Uses_TListBox
#define Uses_TScrollBar
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TText
#include <tvision/tv.h>
#include <tvterm/vtermemu.h>
#include <algorithm>
#include <cstring>

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
TerminalWindow *AgentVisionApp::findWindow(const std::string &id)
{
    if (!deskTop || id.empty()) return nullptr;
    auto *view = deskTop->firstThat([](TView *candidate, void *context) -> Boolean {
        auto *terminal = dynamic_cast<TerminalWindow *>(candidate);
        return terminal && terminal->viewId() == *static_cast<const std::string *>(context);
    }, const_cast<std::string *>(&id));
    return static_cast<TerminalWindow *>(view);
}
std::vector<WindowListRow> AgentVisionApp::windowRows()
{
    std::vector<WindowListRow> rows;
    if (!deskTop) return rows;
    deskTop->forEach([](TView *view, void *context) {
        auto *terminal = dynamic_cast<TerminalWindow *>(view);
        auto &items = *static_cast<std::vector<WindowListRow> *>(context);
        if (terminal && items.size() < 16)
            // TListViewer consumes one of the 32 listbox cells at the left.
            items.push_back({terminal->viewId(), terminal->listCaption(31)});
    }, &rows);
    return rows;
}
void AgentVisionApp::renameTerminal()
{
    auto *selected = dynamic_cast<TerminalWindow *>(deskTop ? deskTop->current : nullptr);
    if (!selected) return;
    const std::string id = selected->viewId();
    char value[49] {};
    std::strncpy(value, selected->title().c_str(), sizeof(value) - 1);
    TRect bounds(0, 0, 38, 8);
    bounds.move((size.x - 38) / 2, (size.y - 8) / 2);
    if (inputBoxRect(bounds, "Rename terminal", "Title", value, sizeof(value)) != cmOK)
        return;
    if (!TerminalWindow::validDisplayTitle(value)) {
        messageBox("Title must be at most 48 UTF-8 bytes, without control characters.",
                   mfError | mfOKButton);
        return;
    }
    // Nested modal service can retire this view. Resolve the immutable ID anew.
    if (auto *target = findWindow(id)) target->setDisplayTitle(value);
}
class WindowListBox final : public TListBox {
    WindowListModel &model;
public:
    WindowListBox(const TRect &bounds, TScrollBar *scroll, WindowListModel &model) :
        TListBox(bounds, 1, scroll), model(model) {}
    void getText(char *dest, short item, short maxLen) override {
        if (item < 0 || size_t(item) >= model.size()) { *dest = '\0'; return; }
        std::strncpy(dest, model.rows()[item].caption.c_str(), maxLen);
        dest[maxLen] = '\0';
    }
    void refresh(std::vector<WindowListRow> rows) {
        model.select(size_t(focused));
        const auto &old = model.rows();
        if (old.size() == rows.size() &&
            std::equal(old.begin(), old.end(), rows.begin(),
                       [](const WindowListRow &a, const WindowListRow &b) {
                           return a.id == b.id && a.caption == b.caption;
                       })) return;
        model.replace(std::move(rows));
        setRange(short(model.size()));
        if (model.size()) focusItem(short(model.selectedIndex()));
        drawView();
    }
};
class WindowListDialog final : public TDialog {
    AgentVisionApp &app;
    WindowListModel model;
    WindowListBox *list;
public:
    explicit WindowListDialog(AgentVisionApp &app) :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 38, 12), "Window List"), app(app) {
        options |= ofCentered;
        auto *scroll = new TScrollBar(TRect(34, 1, 35, 9));
        insert(scroll);
        list = new WindowListBox(TRect(2, 1, 34, 9), scroll, model);
        insert(list);
        insert(new TButton(TRect(7, 9, 17, 11), "~S~witch", cmOK, bfDefault));
        insert(new TButton(TRect(20, 9, 30, 11), "Cancel", cmCancel, bfNormal));
        refresh();
        list->select();
    }
    void refresh() { list->refresh(app.windowRows()); }
    std::string chosenId() {
        model.select(size_t(list->focused));
        return model.selectedId();
    }
    void handleEvent(TEvent &event) override {
        const bool update = event.what == evBroadcast &&
            (event.message.command == cmCoreEvent ||
             event.message.command == cmCheckTerminalUpdates ||
             event.message.command == cmTerminalUpdated);
        TDialog::handleEvent(event);
        if (update) refresh();
    }
};
void AgentVisionApp::openWindowList()
{
    auto *dialog = new WindowListDialog(*this);
    const auto result = execView(dialog);
    const std::string id = result == cmOK ? dialog->chosenId() : std::string();
    TObject::destroy(dialog);
    if (auto *target = findWindow(id)) target->select();
}
void AgentVisionApp::showWorkbenchHelp()
{
    auto *dialog = new TDialog(TRect(0, 0, 38, 13), "Workbench Help");
    dialog->options |= ofCentered;
    dialog->insert(new TStaticText(TRect(2, 1, 36, 10),
        "Ctrl-B opens all controls.\n"
        "Terminal: New, Close, Rename.\n"
        "Windows: List, Next, Previous.\n"
        "Move/Resize, Maximize/Restore.\n"
        "Text: Select, Paste.\n"
        "Wheel/scrollbar scroll.\n"
        "Input: Grab, Release.\n"
        "Alt-End releases; Tab to shell.\n"
        "Suspend/Quit: top menu."));
    dialog->insert(new TButton(TRect(14, 10, 24, 12), "OK", cmOK, bfDefault));
    execView(dialog);
    TObject::destroy(dialog);
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
    {
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
    }
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
    } else if (event.what == evCommand && event.message.command == cmRenameTerminal) {
        renameTerminal();
        clearEvent(event);
    } else if (event.what == evCommand && event.message.command == cmWindowList) {
        openWindowList();
        clearEvent(event);
    } else if (event.what == evCommand && event.message.command == cmWorkbenchHelp) {
        showWorkbenchHelp();
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
    TMenuItem &groups =
        (*new TSubMenu("~T~erminal", kbNoKey, hcMenu) +
            *new TMenuItem("~N~ew", cmNewTerminal, kbNoKey) +
            *new TMenuItem("~C~lose", cmClose, kbNoKey) +
            *new TMenuItem("~R~ename", cmRenameTerminal, kbNoKey)) +
        (*new TSubMenu("~W~indows", kbNoKey, hcMenu) +
            *new TMenuItem("~L~ist", cmWindowList, kbNoKey) +
            *new TMenuItem("~N~ext", cmNext, kbNoKey) +
            *new TMenuItem("~P~revious", cmPrev, kbNoKey) +
            *new TMenuItem("~M~ove/Resize", cmResize, kbNoKey) +
            *new TMenuItem("Maximi~z~e/Restore", cmZoom, kbNoKey)) +
        (*new TSubMenu("Te~x~t", kbNoKey, hcMenu) +
            *new TMenuItem("~S~elect", cmStartSelection, kbNoKey) +
            *new TMenuItem("~P~aste", cmPaste, kbNoKey)) +
        (*new TSubMenu("~I~nput", kbNoKey, hcMenu) +
            *new TMenuItem("~G~rab", cmGrabInput, kbNoKey) +
            *new TMenuItem("~R~elease", cmReleaseInput, kbNoKey));
    // Keep the static type TMenuItem here: TSubMenu + TMenuItem appends inside
    // the last submenu, whereas TMenuItem + TMenuItem extends the top level.
    TMenuItem &items = groups +
        *new TMenuItem("~H~elp", cmWorkbenchHelp, kbNoKey) +
        *new TMenuItem("~S~uspend", cmDosShell, kbNoKey) +
        *new TMenuItem("~Q~uit", cmQuit, kbNoKey);
    auto *popup = new CenteredMenu(new TMenu(items));
    if (ushort command = execView(popup)) {
        TEvent event {};
        event.what = evCommand;
        event.message.command = command;
        putEvent(event);
    }
    TObject::destroy(popup);
}
