#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
static volatile sig_atomic_t resized;
static void winch(int sig) { (void)sig; resized=1; }
int main(int argc,char **argv) {
 if(argc!=1 || !getenv("SHELL") || strcmp(argv[0],getenv("SHELL"))) return 97;
 for(int fd=3;fd<1024;fd++) if(fcntl(fd,F_GETFD)!=-1) return 96;
 struct winsize w; char b[64]; struct sigaction a; memset(&a,0,sizeof a); a.sa_handler=winch; sigaction(SIGWINCH,&a,0);
 if (!isatty(0)||!isatty(1)||!isatty(2)||getsid(0)!=getpid()||getpgrp()!=getpid()||tcgetpgrp(0)!=getpid()) return 99;
 ioctl(0,TIOCGWINSZ,&w);printf("TTY_OK %d %d\n",w.ws_row,w.ws_col);fflush(stdout);
 while(!resized) pause();
 ioctl(0,TIOCGWINSZ,&w);printf("WINCH %d %d\n",w.ws_row,w.ws_col);fflush(stdout);
 if (!fgets(b,sizeof b,stdin)||strcmp(b,"exit\n")) return 98;
 return 17;
}
