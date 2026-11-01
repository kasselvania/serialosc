#include <serialosc/lapis_usb_policy.h>
#include <stdlib.h>
#include <string.h>

static int hex_field(const char *s, size_t width, unsigned int *value)
{
 unsigned int n = 0, digit;
 size_t i;
 if (!s || strlen(s) != width) return 0;
 for (i = 0; i < width; ++i) {
  unsigned char c = (unsigned char)s[i];
  if (c >= '0' && c <= '9') digit = c - '0';
  else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
  else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
  else return 0;
  n = (n << 4) | digit;
 }
 *value = n;
 return 1;
}

static int serial_valid(const char *s)
{
 size_t i, n;
 if (!s || !(n = strlen(s)) || n > SOSC_LAPIS_SERIAL_MAX) return 0;
 for (i = 0; i < n; ++i) {
  unsigned char c = (unsigned char)s[i];
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
   return 0;
 }
 return 1;
}

int sosc_lapis_usb_policy_parse(sosc_lapis_usb_policy_t *p,
 const char *strict, const char *vid, const char *pid,
 const char *serial, const char *interface_number)
{
 if (!p) return -1;
 memset(p, 0, sizeof(*p));
 p->mode = SOSC_LAPIS_USB_INVALID;
 if (!strict || !strcmp(strict, "0")) {
  if (vid || pid || serial || interface_number) return -1;
  p->mode = SOSC_LAPIS_USB_LEGACY;
  return 0;
 }
 if (strcmp(strict, "1") ||
     !hex_field(vid, 4, &p->vid) || !hex_field(pid, 4, &p->pid) ||
     !hex_field(interface_number, 2, &p->interface_number) ||
     !serial_valid(serial)) return -1;
 memcpy(p->serial, serial, strlen(serial) + 1);
 p->mode = SOSC_LAPIS_USB_STRICT;
 return 0;
}

int sosc_lapis_usb_policy_from_env(sosc_lapis_usb_policy_t *p)
{
 return sosc_lapis_usb_policy_parse(p, getenv("LAPIS_SERIALOSC_STRICT"),
  getenv("LAPIS_SERIALOSC_USB_VID"), getenv("LAPIS_SERIALOSC_USB_PID"),
  getenv("LAPIS_SERIALOSC_USB_SERIAL"), getenv("LAPIS_SERIALOSC_USB_INTERFACE"));
}

int sosc_lapis_usb_matches(const sosc_lapis_usb_policy_t *p,
 const char *vid, const char *pid, const char *serial, const char *interface_number)
{
 unsigned int v, d, i;
 return p && p->mode == SOSC_LAPIS_USB_STRICT &&
  hex_field(vid, 4, &v) && hex_field(pid, 4, &d) &&
  hex_field(interface_number, 2, &i) && serial_valid(serial) &&
  p->vid == v && p->pid == d && p->interface_number == i &&
  !strcmp(p->serial, serial);
}
