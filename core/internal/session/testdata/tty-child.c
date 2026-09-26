#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
static volatile sig_atomic_t resized;
static void winch(int sig) { (void)sig; resized=1; }
int main(int argc,char **argv) {
 if(argc!=1 || !getenv("SHELL") || strcmp(argv[0],getenv("SHELL"))) return 97;
 for(int fd=3;fd<1024;fd++) {
  if(fcntl(fd,F_GETFD)!=-1 || errno!=EBADF) return 96;
 }
 struct winsize w; char b[64]; struct sigaction action;
 sigset_t blocked,previous,current,wait_mask;
 memset(&action,0,sizeof action);action.sa_handler=winch;
 if(sigemptyset(&action.sa_mask)||sigaction(SIGWINCH,&action,0)) return 90;
 /* Keep the flag check and pending resize under a blocked signal. Only
  * sigsuspend atomically unmasks SIGWINCH and waits, eliminating the gap. */
 if(sigemptyset(&blocked)||sigaddset(&blocked,SIGWINCH)||sigprocmask(SIG_BLOCK,&blocked,&previous)) return 91;
 if(sigprocmask(SIG_SETMASK,0,&current)||sigismember(&current,SIGWINCH)!=1) return 92;
 wait_mask=previous;
 if(sigdelset(&wait_mask,SIGWINCH)) return 93;
 if(!isatty(0)||!isatty(1)||!isatty(2)||getsid(0)!=getpid()||getpgrp()!=getpid()||tcgetpgrp(0)!=getpid()) return 99;
 if(ioctl(0,TIOCGWINSZ,&w)) return 94;
 if(printf("TTY_OK %d %d\n",w.ws_row,w.ws_col)<0 || fflush(stdout)) return 95;
 while(!resized) {
  if(sigsuspend(&wait_mask)!=-1 || errno!=EINTR) return 89;
 }
 if(sigprocmask(SIG_SETMASK,&previous,0)) return 88;
 if(ioctl(0,TIOCGWINSZ,&w)) return 94;
 if(printf("WINCH %d %d\n",w.ws_row,w.ws_col)<0 || fflush(stdout)) return 95;
 if(!fgets(b,sizeof b,stdin)||strcmp(b,"exit\n")) return 98;
 return 17;
}
