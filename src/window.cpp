// Uses the upstream tvterm window adapter; notice in third_party/notices.
#include "window.h"
#include "commands.h"
#define Uses_TEvent
#define Uses_MsgBox
#include <tvision/tv.h>
#include <sys/wait.h>

const tvterm::TVTermConstants TerminalWindow::appConsts = {
    cmCheckTerminalUpdates, cmTerminalUpdated, cmCopySelection, cmCancelSelection,
    cmGrabInput, cmReleaseInput, cmStartSelection, hcInputGrabbed, hcSelecting
};
TerminalWindow::TerminalWindow(const TRect &bounds, tvterm::TerminalController &term,
                               char name) noexcept :
    TWindowInit(&tvterm::BasicTerminalWindow::initFrame),
    BasicTerminalWindow(bounds, term, appConsts), controller(term), label(name)
{}
bool TerminalWindow::isLive() const noexcept
{
    return !completed && controller.childWaitStatus() == -1;
}
void TerminalWindow::finish()
{
    if (!completed) {
        controller.finish(); // Outside rendering/state callbacks, on UI thread.
        completed = true;
    }
}
const char *TerminalWindow::getTitle(short)
{
    caption = std::string("Terminal ") + label;
    int status = controller.childWaitStatus(); // macOS wait macros require lvalue.
    if (status >= 0 && WIFEXITED(status))
        caption += " [exited " + std::to_string(WEXITSTATUS(status)) + "]";
    else if (status >= 0 && WIFSIGNALED(status))
        caption += " [signal " + std::to_string(WTERMSIG(status)) + "]";
    else if (status == -2)
        caption += " [status unavailable]";
    else
        caption += " [pid " + std::to_string(controller.childPid()) + "]";
    if (helpCtx == hcInputGrabbed) caption += " (Input Grab)";
    return caption.c_str();
}
void TerminalWindow::handleEvent(TEvent &event)
{
    if (event.what == evBroadcast && event.message.command == cmCheckTerminalUpdates &&
        controller.clientIsDisconnected())
        finish(); // Must happen BEFORE BasicTerminalWindow renders under lockState.
    if (event.what == evBroadcast && event.message.command == cmCountLive && isLive())
        ++*static_cast<size_t *>(event.message.infoPtr);
    if (event.what == evKeyDown && !isLive() && !(state & (sfDragging | sfModal))) {
        clearEvent(event); // Retained exited output; never destroy/fall through.
        return;
    }
    BasicTerminalWindow::handleEvent(event);
}
void TerminalWindow::close()
{
    if (isLive() && messageBox(mfConfirmation | mfYesButton | mfNoButton,
                              "Terminate live terminal %c and close it?", label) != cmYes)
        return;
    finish();
    TWindow::close();
}
