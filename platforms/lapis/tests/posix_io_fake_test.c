/* Patched POSIX transport with all system I/O replaced by software fakes. */
#define _GNU_SOURCE
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
static ssize_t fake_read(int fd, void *b, size_t n);
static ssize_t fake_write(int fd, const void *b, size_t n);
static int fake_clock_gettime(clockid_t id, struct timespec *ts);
static int fake_open(const char *, int, ...);
static int fake_close(int);
static int fake_tcgetattr(int, struct termios *);
static int fake_tcsetattr(int, int, const struct termios *);
static int fake_tcflush(int, int);
#define open fake_open
#define close fake_close
#define tcgetattr fake_tcgetattr
#define tcsetattr fake_tcsetattr
#define tcflush fake_tcflush
#define read fake_read
#define write fake_write
#define clock_gettime fake_clock_gettime
#include "posix.c"
#undef open
#undef close
#undef tcgetattr
#undef tcsetattr
#undef tcflush
#undef read
#undef write
#undef clock_gettime
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); exit(1); } } while (0)
static int mode, calls, waits, clock_bad;
static int line_failure, line_closes, line_sets, line_flushes;
static struct termios captured;
static int fake_open(const char *p, int flags, ...) {
 (void)p; CHECK(flags & O_NONBLOCK); return 123;
}
static int fake_close(int fd) { CHECK(fd==123); ++line_closes; return 0; }
static int fake_tcgetattr(int fd, struct termios *t) {
 CHECK(fd==123); if(line_failure==1) { errno=EIO; return -1; }
 memset(t,0,sizeof(*t));
 t->c_iflag=IXON|IXOFF|IXANY|ICRNL;
 t->c_cflag=HUPCL|CS7|PARENB|CSTOPB;
#ifdef CRTSCTS
 t->c_cflag|=CRTSCTS;
#endif
 return 0;
}
static int fake_tcsetattr(int fd, int action, const struct termios *t) {
 CHECK(fd==123); CHECK(action==TCSANOW); ++line_sets; captured=*t;
 if(line_failure==2) { errno=EIO; return -1; } return 0;
}
static int fake_tcflush(int fd, int action) {
 CHECK(fd==123); CHECK(action==TCIOFLUSH); ++line_flushes;
 if(line_failure==3) { errno=EIO; return -1; } return 0;
}
static int64_t now;
static ssize_t fake_read(int fd, void *b, size_t n) {
 (void)fd; ++calls;
 if (mode==0) return 0;
 if (mode==1) { errno=EINTR; return -1; }
 if (mode==2 || mode==5) { errno=EAGAIN; return -1; }
 if (mode==3) { errno=EIO; return -1; }
 if (mode==4) { ((char *)b)[0]=42; return n ? 1 : 0; }
 return -1;
}
static ssize_t fake_write(int fd, const void *b, size_t n) { (void)fd; (void)b; return n; }
static int fake_clock_gettime(clockid_t id, struct timespec *ts) {
 (void)id; if(clock_bad) return -1;
 ts->tv_sec=now/1000; ts->tv_nsec=(now%1000)*1000000; return 0;
}
int monome_platform_wait_for_input(monome_t *m, uint_t ms) {
 (void)m; ++waits;
 if (mode==5) { now+=ms; return 1; }
 return 0;
}
int main(void) {
 monome_t m={0}; uint8_t b[512]; int i; ssize_t n;
 for(i=0;i<=5;++i) {
  mode=i; calls=waits=clock_bad=0; now=0;
  n=monome_platform_read(&m,b,sizeof(b));
  CHECK(calls<=256); CHECK(waits<=256);
  if(i==0) { CHECK(n==0); CHECK(calls==1); }
  if(i==1 || i==2) { CHECK(n==0); CHECK(calls==256); }
  if(i==3) CHECK(n==-1);
  if(i==4) { CHECK(n==256); CHECK(b[0]==42); }
  if(i==5) { CHECK(n==0); CHECK(now==25); CHECK(calls==1); }
 }
 clock_bad=1; CHECK(monome_platform_read(&m,b,1)==-1);
 for(i=0;i<=3;++i) {
  monome_devmap_t map={0};
  line_failure=i; line_closes=line_sets=line_flushes=0;
  CHECK(monome_platform_open(&m,&map,"not-a-tty")==(i ? 1 : 0));
  CHECK(line_closes==(i ? 1 : 0));
  if(i==1) CHECK(!line_sets && !line_flushes);
  if(i==2) CHECK(line_sets==1 && !line_flushes);
  if(i==0) {
   CHECK(m.fd==123); CHECK(line_sets==1 && line_flushes==1);
   CHECK(!(captured.c_iflag & (IXON|IXOFF|IXANY)));
#ifdef CRTSCTS
   CHECK(!(captured.c_cflag & CRTSCTS));
#endif
   CHECK(captured.c_cflag & HUPCL);
   CHECK((captured.c_cflag & CSIZE)==CS8);
   CHECK((captured.c_cflag & (CLOCAL|CREAD))==(CLOCAL|CREAD));
   CHECK(!(captured.c_cflag & (PARENB|CSTOPB)));
   CHECK(cfgetispeed(&captured)==B115200 && cfgetospeed(&captured)==B115200);
  }
 }
 puts("patched POSIX finite-read fake checks passed"); return 0;
}
