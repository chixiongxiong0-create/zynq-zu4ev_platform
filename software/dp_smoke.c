#define _POSIX_C_SOURCE 200809L
#include "dp_display.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
static volatile sig_atomic_t stop;
static void signal_stop(int signo) { (void)signo; stop=1; }
int main(void)
{
    signal(SIGINT,signal_stop);signal(SIGTERM,signal_stop);
    struct dp_display *d=dp_display_open();
    if(!d) {fprintf(stderr,"DP smoke open: %s\n",strerror(errno));return 1;}
    puts("DP black primary enabled; waiting for SIGINT/SIGTERM");
    while(!stop) {
        struct pollfd p={.fd=dp_display_fd(d),.events=POLLIN};
        int r=poll(&p,1,1000);
        if(r<0 && errno!=EINTR) {perror("poll");break;}
        if(r>0 && p.revents&(POLLERR|POLLHUP|POLLNVAL))break;
        if(r>0 && p.revents&POLLIN) {
            int retired;
            r=dp_display_dispatch(d,&retired);
            if(r<0) {fprintf(stderr,"DP event error %d\n",r);break;}
        }
    }
    int r=dp_display_close(d);
    if(r)fprintf(stderr,"DP disable failed %d\n",r);
    return r?1:0;
}
