// Uses the upstream tvterm window adapter; notice in third_party/notices.
#include "window.h"
#include "commands.h"
#define Uses_TEvent
#define Uses_MsgBox
#define Uses_TFrame
#include <tvision/tv.h>
using namespace agentvision;

const tvterm::TVTermConstants TerminalWindow::appConsts = {
    cmCheckTerminalUpdates, cmTerminalUpdated, cmCopySelection, cmCancelSelection,
    cmGrabInput, cmReleaseInput, cmStartSelection, hcInputGrabbed, hcSelecting
};
TerminalWindow::TerminalWindow(const TRect &bounds, tvterm::TerminalController &term,
                               std::shared_ptr<SessionEndpoint> session, char name) noexcept :
    TWindowInit(&tvterm::BasicTerminalWindow::initFrame),
    BasicTerminalWindow(bounds, term, appConsts), controller(term),
    endpoint(std::move(session)), label(name)
{}
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
std::string TerminalWindow::captionFor(char label, const SessionMetadata &metadata,
                                      bool closing, bool resizeFailed)
{
    std::string text = std::string("Terminal ") + label + " [";
    switch (metadata.state) {
    case SessionState::Starting: text += "starting"; break;
    case SessionState::Running: text += closing ? "closing" : "live"; break;
    case SessionState::Lost: text += "backend lost"; break;
    case SessionState::Closed: text += "closed"; break;
    case SessionState::Exited:
        if (metadata.status.kind == ExitKind::Exit)
            text += "exited " + std::to_string(metadata.status.value);
        else if (metadata.status.kind == ExitKind::Signal) {
            text += "signal " + std::to_string(metadata.status.value);
            if (metadata.status.coreDump) text += "; core dump";
        } else text += "status unavailable";
        break;
    }
    text += "]";
    if (metadata.state == SessionState::Exited || metadata.state == SessionState::Closed) {
        switch (metadata.drainReason) {
        case DrainReason::ByteCap: text += " [tail: byte cap]"; break;
        case DrainReason::TimeCap: text += " [tail: time cap]"; break;
        case DrainReason::CreditCap: text += " [tail: credit cap]"; break;
        case DrainReason::IOError: text += " [tail: I/O error]"; break;
        case DrainReason::ExplicitClose: text += " [tail: close]"; break;
        default: break;
        }
    }
    if (resizeFailed) text += " [resize failed]";
    return text;
}
const char *TerminalWindow::getTitle(short)
{
    caption = captionFor(label, endpoint->metadata(), closing != 0, resizeFailed);
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
                              "Terminate live terminal %c and close it?", label) != cmYes)
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
