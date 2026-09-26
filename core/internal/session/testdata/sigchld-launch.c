/* Synthetic startup harness: deliberately poison inherited SIGCHLD, then apply
 * the frontend's bounded pre-exec normalization. No Go handlers are replaced. */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
int main(int argc,char **argv) {
 struct sigaction action,readback;
 if(argc<2) return 90;
 memset(&action,0,sizeof action);sigemptyset(&action.sa_mask);
 action.sa_handler=SIG_IGN;action.sa_flags=SA_NOCLDWAIT;
 if(sigaction(SIGCHLD,&action,0)) return 91;
 action.sa_handler=SIG_DFL;action.sa_flags=0;
 if(sigaction(SIGCHLD,&action,0)||sigaction(SIGCHLD,0,&readback)) return 92;
 if(readback.sa_handler!=SIG_DFL||(readback.sa_flags&SA_NOCLDWAIT)) return 93;
 puts("NORMALIZED_SIGCHLD");fflush(stdout);
 execv(argv[1],argv+1);return 94;
}
