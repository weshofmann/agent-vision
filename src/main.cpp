#include "app.h"
#include <signal.h>
#include <cstdio>
int main()
{
    // C++ owns only the direct core child; Go exclusively owns terminal shells.
    // Inherited SIG_IGN/NOCLDWAIT would defeat core-child status observation.
    struct sigaction children {};
    children.sa_handler = SIG_DFL;
    sigemptyset(&children.sa_mask);
    if (sigaction(SIGCHLD, &children, nullptr) != 0) {
        std::fputs("Cannot initialize direct-child ownership\n", stderr);
        return 1;
    }
    AgentVisionApp application;
    if (application.ready) application.run();
    application.shutDown();
    if (application.cleanupTargetMissed())
        std::fputs("Frontend presentation cleanup exceeded its 100ms target.\n", stderr);
    if (!application.failure().empty())
        std::fprintf(stderr, "%s\n", application.failure().c_str());
    else if (!application.cleanupDiagnostic().empty())
        std::fprintf(stderr, "%s\n", application.cleanupDiagnostic().c_str());
    return application.ready ? 0 : 1;
}
