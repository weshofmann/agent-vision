// Uses the upstream tvterm window adapter; notice in third_party/notices.
#include "window.h"
#include "commands.h"
#define Uses_TEvent
#define Uses_MsgBox
#define Uses_TFrame
#define Uses_TText
#include <tvision/tv.h>
#include <algorithm>
using namespace agentvision;

const tvterm::TVTermConstants TerminalWindow::appConsts = {
    cmCheckTerminalUpdates, cmTerminalUpdated, cmCopySelection, cmCancelSelection,
    cmGrabInput, cmReleaseInput, cmStartSelection, hcInputGrabbed, hcSelecting
};
TerminalWindow::TerminalWindow(const TRect &bounds, tvterm::TerminalController &term,
                               std::shared_ptr<SessionEndpoint> session, std::string name) noexcept :
    TWindowInit(&tvterm::BasicTerminalWindow::initFrame),
    BasicTerminalWindow(bounds, term, appConsts), controller(term),
    endpoint(std::move(session)), label(std::move(name)), displayTitle("Terminal " + label)
{}
bool TerminalWindow::validDisplayTitle(const std::string &text) noexcept
{
    if (text.size() > 48) return false;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t count = 0;
        if (lead < 0x80) { cp = lead; count = 1; }
        else if (lead >= 0xC2 && lead <= 0xDF) { cp = lead & 0x1F; count = 2; }
        else if (lead >= 0xE0 && lead <= 0xEF) { cp = lead & 0x0F; count = 3; }
        else if (lead >= 0xF0 && lead <= 0xF4) { cp = lead & 0x07; count = 4; }
        else return false;
        if (i + count > text.size()) return false;
        for (size_t j = 1; j < count; ++j) {
            const auto tail = static_cast<unsigned char>(text[i + j]);
            if ((tail & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (tail & 0x3F);
        }
        if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
            (count == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 ||
            (cp >= 0x7F && cp <= 0x9F)) return false;
        i += count;
    }
    return true;
}
bool TerminalWindow::setDisplayTitle(const std::string &text)
{
    if (!validDisplayTitle(text)) return false;
    displayTitle = text.empty() ? "Terminal " + label : text;
    if (frame) frame->drawView();
    return true;
}
std::string TerminalWindow::labelFor(uint64_t localId)
{
    return localId < 26 ? std::string(1, char('A' + localId)) : std::to_string(localId + 1);
}
bool TerminalWindow::ownsSession(SessionId id) const noexcept
{
    return endpoint->metadata().id == id;
}
bool TerminalWindow::isLive() const noexcept
{
    auto state = endpoint->metadata().state;
    return !authorityLost && (state == SessionState::Starting || state == SessionState::Running);
}
void TerminalWindow::finish()
{
    if (!finished) {
        controller.finishPresentation(); // UI thread, outside rendering/state callbacks.
        finished = true;
    }
}
std::string TerminalWindow::captionFor(const std::string &label, const SessionMetadata &metadata,
                                      bool closing, bool resizeFailed, bool lost, size_t maxCells)
{
    const std::string defaultTitle = "Terminal " + label;
    auto result = formatCaption(defaultTitle, metadata, closing, resizeFailed, lost, maxCells);
    if (maxCells && result.rfind(defaultTitle + " ", 0) != 0)
        return formatCaption(label, metadata, closing, resizeFailed, lost, maxCells);
    return result;
}
std::string TerminalWindow::formatCaption(const std::string &title, const SessionMetadata &metadata,
                                      bool closing, bool resizeFailed, bool lost, size_t maxCells)
{
    std::string state = "[";
    switch (metadata.state) {
    case SessionState::Starting: state += lost ? "backend lost" : "starting"; break;
    case SessionState::Running: state += lost ? "backend lost" : closing ? "closing" : "live"; break;
    case SessionState::Lost: state += "backend lost"; break;
    case SessionState::Closed: state += "closed"; break;
    case SessionState::Exited:
        if (metadata.status.kind == ExitKind::Exit)
            state += "exited " + std::to_string(metadata.status.value);
        else if (metadata.status.kind == ExitKind::Signal) {
            state += "signal " + std::to_string(metadata.status.value);
            if (metadata.status.coreDump) state += "; core dump";
        } else state += "status unavailable";
        break;
    }
    state += "]";
    if (metadata.state == SessionState::Exited || metadata.state == SessionState::Closed) {
        switch (metadata.drainReason) {
        case DrainReason::ByteCap: state += " [tail: byte cap]"; break;
        case DrainReason::TimeCap: state += " [tail: time cap]"; break;
        case DrainReason::CreditCap: state += " [tail: credit cap]"; break;
        case DrainReason::IOError: state += " [tail: I/O error]"; break;
        case DrainReason::ExplicitClose: state += " [tail: close]"; break;
        default: break;
        }
    }
    if (lost && (metadata.state == SessionState::Exited || metadata.state == SessionState::Closed))
        state += " [backend lost]";
    if (resizeFailed) state += " [resize failed]";
    if (!maxCells) return title + " " + state;
    if (TText::width(state.c_str()) > maxCells) {
        // A narrow frame cannot carry tail/resize annotations. Preserve the
        // authoritative completion and the later contact-loss fact first.
        if (lost && metadata.state == SessionState::Exited) {
            if (metadata.status.kind == ExitKind::Exit)
                state = "[exit " + std::to_string(metadata.status.value) + " lost]";
            else if (metadata.status.kind == ExitKind::Signal)
                state = "[sig " + std::to_string(metadata.status.value) + " lost]";
            else
                state = "[status? lost]";
        } else if (lost && metadata.state == SessionState::Closed)
            state = "[closed lost]";
    }
    const size_t stateWidth = TText::width(state.c_str());
    if (stateWidth >= maxCells) return state;
    const size_t titleLimit = maxCells - stateWidth - 1;
    size_t offset = 0, width = 0;
    while (offset < title.size()) {
        size_t next = offset, cellWidth = 0;
        if (!TText::next(title.c_str(), next, cellWidth) || width + cellWidth > titleLimit)
            break;
        offset = next;
        width += cellWidth;
    }
    return offset ? title.substr(0, offset) + " " + state : state;
}
std::string TerminalWindow::displayCaption(size_t maxCells) const
{
    if (displayTitle == "Terminal " + label)
        return captionFor(label, endpoint->metadata(), closing != 0,
                          resizeFailed, authorityLost, maxCells);
    return formatCaption(displayTitle, endpoint->metadata(), closing != 0,
                      resizeFailed, authorityLost, maxCells);
}
const char *TerminalWindow::getTitle(short maxCells)
{
    caption = displayCaption(maxCells > 0 ? size_t(maxCells) : 0);
    if (helpCtx == hcInputGrabbed) caption += " (Input Grab)";
    return caption.c_str();
}
void TerminalWindow::handleEvent(TEvent &event)
{
    ++eventDepth;
    if (event.what == evBroadcast) {
        if (event.message.command == cmStopPresentations) finish();
        if (event.message.command == cmCoreEvent && event.message.infoPtr) {
            const auto &core = *static_cast<const ConnectionEvent *>(event.message.infoPtr);
            if (core.kind == ConnectionEvent::Kind::Lost) {
                authorityLost = true; // Separate contact authority from any known exit result.
                closing = 0; // The failed connection can no longer complete this UI correlation.
            }
            if (core.session == endpoint->metadata().id) {
                if (core.kind == ConnectionEvent::Kind::Closed && closing && core.request == closing)
                    closeCompleted = true;
                if (core.kind == ConnectionEvent::Kind::RequestError && core.errorCode == 9)
                    resizeFailed = true;
                if (core.kind == ConnectionEvent::Kind::RequestError && core.request == closing)
                    closing = 0;
                if (frame) frame->drawView();
            }
        }
        if (event.message.command == cmCheckTerminalUpdates) {
            auto state = endpoint->metadata().state;
            if (state == SessionState::Exited || state == SessionState::Lost ||
                state == SessionState::Closed) finish();
            if (closeCompleted && eventDepth == 1 && !(this->state & (sfDragging | sfModal))) {
                finish();
                --eventDepth; // No scope guard or member access may follow deletion.
                TWindow::close();
                return;
            }
        }
        if (event.message.command == cmCountLive && isLive())
            ++*static_cast<size_t *>(event.message.infoPtr);
        if (event.message.command == cmCountViews)
            ++*static_cast<size_t *>(event.message.infoPtr);
        if (event.message.command == cmFindSession && event.message.infoPtr) {
            auto &query = *static_cast<TerminalSessionQuery *>(event.message.infoPtr);
            if (ownsSession(query.id)) query.found = true;
        }
    }
    // TerminalController itself suppresses emitted process input after End/Lost.
    // Keep selection commands and local navigation flowing through TerminalView.
    BasicTerminalWindow::handleEvent(event);
    --eventDepth;
}
void TerminalWindow::close()
{
    if (closing && !authorityLost) return;
    if (isLive() && messageBox(mfConfirmation | mfYesButton | mfNoButton,
                              "Terminate live terminal %s and close it?", label.c_str()) != cmYes)
        return;
    auto state = endpoint->metadata().state;
    if (authorityLost || state == SessionState::Lost || state == SessionState::Closed) {
        // An explicit local dismissal follows the same safe completion boundary
        // as a correlated Closed; close() may itself run on a nested view stack.
        closeCompleted = true;
        finish();
        return;
    }
    closing = endpoint->requestClose();
    if (frame) frame->drawView();
}
