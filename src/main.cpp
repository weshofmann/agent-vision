#include "app.h"
#include <signal.h>
#include <cstdio>
int main()
{
    // The application owns waitpid; inherited SIG_IGN/NOCLDWAIT defeats statuses.
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
    return application.ready ? 0 : 1;
}
