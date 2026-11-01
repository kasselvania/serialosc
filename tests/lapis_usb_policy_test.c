/* Pure selector: no tty, sockets or sysfs accesses. Checks survive NDEBUG. */
#include <serialosc/lapis_usb_policy.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void)
{
 sosc_lapis_usb_policy_t p;
 const char *bad[] = {"", "true", "yes", "01", " 1", "1 ", "2", "-1"};
 const char *serials[] = {"", "*", "a b", "a/b", "a:b", "a\nb", "a\rb", "a?"};
 const char *vids[] = {"", "403", "00403", "0x40", " 403", "zzzz", "0403\n"};
 size_t i;
 char longserial[131];
 CHECK(!sosc_lapis_usb_policy_parse(&p, NULL, NULL, NULL, NULL, NULL));
 CHECK(p.mode == SOSC_LAPIS_USB_LEGACY);
 CHECK(!sosc_lapis_usb_matches(&p, "0403", "6001", "m123", "00"));
 CHECK(!sosc_lapis_usb_policy_parse(&p, "0", NULL, NULL, NULL, NULL));
 for (i=0; i<sizeof(bad)/sizeof(*bad); ++i) {
  CHECK(sosc_lapis_usb_policy_parse(&p, bad[i], "0403", "6001", "m123", "00") < 0);
  CHECK(p.mode == SOSC_LAPIS_USB_INVALID);
 }
 CHECK(sosc_lapis_usb_policy_parse(&p, "1", NULL, "6001", "m123", "00") < 0);
 CHECK(sosc_lapis_usb_policy_parse(&p, "1", "0403", NULL, "m123", "00") < 0);
 CHECK(sosc_lapis_usb_policy_parse(&p, "1", "0403", "6001", NULL, "00") < 0);
 CHECK(sosc_lapis_usb_policy_parse(&p, "1", "0403", "6001", "m123", NULL) < 0);
 CHECK(sosc_lapis_usb_policy_parse(&p, "1", "0403", "6001", "m123", "0") < 0);
 CHECK(sosc_lapis_usb_policy_parse(&p, NULL, "0403", NULL, NULL, NULL) < 0);
 CHECK(sosc_lapis_usb_policy_parse(&p, "0", "0403", "6001", "m123", "00") < 0);
 for (i=0; i<sizeof(serials)/sizeof(*serials); ++i)
  CHECK(sosc_lapis_usb_policy_parse(&p, "1", "0403", "6001", serials[i], "00") < 0);
 for (i=0; i<sizeof(vids)/sizeof(*vids); ++i)
  CHECK(sosc_lapis_usb_policy_parse(&p, "1", vids[i], "6001", "m123", "00") < 0);
 memset(longserial, 'a', sizeof(longserial)); longserial[130] = 0;
 CHECK(sosc_lapis_usb_policy_parse(&p, "1", "0403", "6001", longserial, "00") < 0);
 longserial[128] = 0;
 CHECK(!sosc_lapis_usb_policy_parse(&p, "1", "0403", "6001", longserial, "00"));
 CHECK(!sosc_lapis_usb_policy_parse(&p, "1", "04aB", "60fF", "m123._-", "0A"));
 CHECK(p.mode == SOSC_LAPIS_USB_STRICT);
 CHECK(sosc_lapis_usb_matches(&p, "04Ab", "60FF", "m123._-", "0a"));
 CHECK(!sosc_lapis_usb_matches(&p, "04Ab", "60FF", "M123._-", "0a"));
 CHECK(!sosc_lapis_usb_matches(&p, "04Ab", "60FF", "m123._-x", "0a"));
 CHECK(!sosc_lapis_usb_matches(&p, "04Ab", "60FF", "m123._-", "0b"));
 CHECK(!sosc_lapis_usb_matches(&p, "04Ac", "60FF", "m123._-", "0a"));
 CHECK(!sosc_lapis_usb_matches(&p, "04Ab", "60FE", "m123._-", "0a"));
 CHECK(!sosc_lapis_usb_matches(&p, NULL, "60FF", "m123._-", "0a"));
 puts("pure USB selector checks passed");
 return 0;
}
