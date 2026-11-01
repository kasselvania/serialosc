/* Includes patched source from PRIVATE copy via compiler -I. No real transport. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
static int fake_clock_gettime(clockid_t id, struct timespec *ts);
#define clock_gettime fake_clock_gettime
#include "mext.c"
#undef clock_gettime
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); exit(1); } } while (0)
monome_rotspec_t rotspec[4];
static int mode, writes, closes, reads, waits, opens, clock_failure;
static int64_t now;
static unsigned char queue[256];
static size_t len, pos;
static int fake_clock_gettime(clockid_t id, struct timespec *ts) {
 (void)id;
 if (clock_failure) { errno = EIO; return -1; }
 ts->tv_sec = now / 1000; ts->tv_nsec = now % 1000 * 1000000;
 return 0;
}
void *m_calloc(size_t n, size_t s) { return calloc(n, s); }
void m_free(void *p) { free(p); }
int monome_platform_open(monome_t *m, const monome_devmap_t *d, const char *p) {
 (void)m; (void)d; (void)p; ++opens; return mode == 8 ? 1 : 0;
}
int monome_platform_close(monome_t *m) { (void)m; ++closes; return 0; }
ssize_t monome_platform_write(monome_t *m, const uint8_t *b, size_t n) {
 (void)m; (void)b; ++writes;
 if (mode == 1) return 0;
 if (mode == 2) { errno = EIO; return -1; }
 if (mode == 10) return n - 1;
 return n;
}
int monome_platform_wait_for_input(monome_t *m, uint_t ms) {
 (void)m; ++waits;
 if (mode == 3) { errno = EIO; return -1; }
 if (mode == 0 || mode == 7) { now += ms; return 1; }
 return 0;
}
ssize_t monome_platform_read(monome_t *m, uint8_t *b, size_t n) {
 (void)m; ++reads;
 if (mode == 6) return -1;
 if (mode == 5) { /* endless irrelevant system responses; no elapsed clock */
  memset(b, 0, n); return n;
 }
 if (pos == len) return 0;
 if (n > len-pos) { if (mode == 9) return 0; n = len-pos; }
 memcpy(b, queue+pos, n); pos += n; return n;
}
static int call_open(monome_t *m, ...) {
 va_list args; monome_devmap_t map = {.friendly = "fake"}; int result;
 va_start(args, m); result = m->open(m, "not-a-tty", "fake", &map, args); va_end(args);
 return result;
}
static void reset(int which) {
 mode=which; writes=closes=reads=waits=opens=clock_failure=0; now=0; len=pos=0;
}
static void responses(int grid) {
 queue[len++]=0; queue[len++]=SS_ENCODER; queue[len++]=2;
 queue[len++]=1; memset(queue+len, 'x', 32); len+=32;
 if (grid) { queue[len++]=3; queue[len++]=0; queue[len++]=0; }
}
int main(void) {
 monome_t *m; int i;
 for (i=0; i<=9; ++i) {
  reset(i); m=monome_protocol_mext_new(); CHECK(m);
  if (i==4 || i==9) responses(i==4);
  CHECK(call_open(m) == (i==4 ? 0 : -1));
  CHECK(opens==1); CHECK(writes<=36); CHECK(reads<=1536); CHECK(waits<=12);
  if (i==4) { CHECK(closes==0); CHECK(!MEXT_T(m)->need_responses); }
  else CHECK(closes==(i==8 ? 0 : 1));
  if (i==0 || i==7) { CHECK(now==3000); CHECK(writes==36); CHECK(errno==ETIMEDOUT); }
  if (i==1 || i==2) CHECK(writes==1);
  if (i==5) CHECK(waits==12); /* retry limit, even frozen clock + busy peer */
  if (i==9) CHECK(MEXT_T(m)->need_responses & MEXT_NEED_GRID_SIZE);
  m->free(m);
 }
 reset(10); m=monome_protocol_mext_new();
 CHECK(mext_led_ring_all(m,0,0)==-1); CHECK(writes==1); m->free(m);
 reset(5); m=monome_protocol_mext_new();
 { monome_event_t e={0}; CHECK(mext_next_event(m,&e)==0); CHECK(reads<=130); }
 m->free(m);
 reset(0); clock_failure=1; m=monome_protocol_mext_new();
 CHECK(call_open(m)==-1); CHECK(closes==1); CHECK(!writes); m->free(m);
 puts("patched mext fake transport checks passed");
 return 0;
}
