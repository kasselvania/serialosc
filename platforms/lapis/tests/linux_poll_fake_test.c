#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
static int fake_poll(struct pollfd *, nfds_t, int);
#define poll fake_poll
#include "linux.c"
#undef poll
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); exit(1); } } while (0)
static int status; static short events;
static int fake_poll(struct pollfd *fd, nfds_t n, int ms) { (void)n; (void)ms; fd->revents=events; return status; }
int monome_get_fd(monome_t *m) { (void)m; return 123; }
int main(void) {
 monome_t m={0}; short bad[]={POLLERR,POLLHUP,POLLNVAL,POLLIN|POLLHUP}; unsigned int i;
 status=-1; CHECK(monome_platform_wait_for_input(&m,25)==-1);
 status=0; CHECK(monome_platform_wait_for_input(&m,25)==1);
 status=1; events=POLLIN; CHECK(monome_platform_wait_for_input(&m,25)==0);
 for(i=0;i<sizeof(bad)/sizeof(*bad);++i) { events=bad[i]; CHECK(monome_platform_wait_for_input(&m,25)==-1); }
 events=0; CHECK(monome_platform_wait_for_input(&m,25)==1);
 puts("patched Linux poll fake checks passed"); return 0;
}
