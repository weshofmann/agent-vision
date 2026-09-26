/* Compile-only wait instrumentation. Includes the actual native fixture rather
 * than a separate implementation of its SIGWINCH waiting algorithm. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t resized;
static int hook_entered;
static void entered_wait(int sig) {
 (void)sig;
 static const char marker[]="WAIT_ENTERED\n";
 if(write(STDOUT_FILENO,marker,sizeof marker-1)!=(ssize_t)(sizeof marker-1)) _exit(80);
}
static void before_wait(void) {
 if(hook_entered) return;
 hook_entered=1;
 const char *mode=getenv("WAIT_TEST_MODE");
 if(mode && !strcmp(mode,"before")) {
  char gate[32];
  if(puts("BEFORE_WAIT")==EOF || fflush(stdout)) _exit(81);
  while(!fgets(gate,sizeof gate,stdin)) {if(errno!=EINTR) _exit(82);clearerr(stdin);}
  if(strcmp(gate,"continue\n")) _exit(82);
 } else if(raise(SIGUSR1)) _exit(83);
}
static int fixture_pause(void) __attribute__((unused));
static int fixture_pause(void) {
 before_wait();
 if(!resized) _exit(88);
 if(puts("SIGNAL_BEFORE_WAIT")==EOF || fflush(stdout)) _exit(89);
 return pause();
}
static int fixture_suspend(const sigset_t *mask) __attribute__((unused));
static int fixture_suspend(const sigset_t *mask) {
 before_wait();
 sigset_t wait_mask=*mask;
 if(sigdelset(&wait_mask,SIGUSR1)) _exit(84);
 return sigsuspend(&wait_mask);
}
#define pause fixture_pause
#define sigsuspend fixture_suspend
#define main fixture_main
#include "tty-child.c"
#undef main
#undef pause
#undef sigsuspend

int main(int argc,char **argv) {
 struct sigaction action; sigset_t blocked,previous;
 memset(&action,0,sizeof action);action.sa_handler=entered_wait;
 if(sigemptyset(&action.sa_mask)||sigaction(SIGUSR1,&action,0)) return 85;
 if(sigemptyset(&blocked)||sigaddset(&blocked,SIGUSR1)||sigprocmask(SIG_BLOCK,&blocked,&previous)) return 86;
 int status=fixture_main(argc,argv);
 if(sigprocmask(SIG_SETMASK,&previous,0)) return 87;
 return status;
}
