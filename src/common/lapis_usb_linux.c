/* Linux sysfs-only revalidation. Never open the supplied tty pathname. */
#define _XOPEN_SOURCE 700
#include <serialosc/lapis_usb_policy.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static int field(const char *dir, const char *name, char *out, size_t size)
{
 char path[PATH_MAX];
 FILE *fp;
 size_t n;
 int c;
 if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return 0;
 fp = fopen(path, "r");
 if (!fp) return 0;
 n = fread(out, 1, size - 1, fp);
 c = fgetc(fp);
 if (ferror(fp) || c != EOF || memchr(out, 0, n)) { fclose(fp); return 0; }
 fclose(fp);
 if (n && out[n-1] == '\n') --n;
 out[n] = 0;
 return n > 0;
}

static int present(const char *dir, const char *name)
{
 char path[PATH_MAX];
 struct stat st;
 return snprintf(path, sizeof(path), "%s/%s", dir, name) < (int)sizeof(path) &&
  !lstat(path, &st);
}

int sosc_lapis_usb_validate_at(const sosc_lapis_usb_policy_t *p,
 const char *root, const char *devnode)
{
 char path[PATH_MAX], resolved[PATH_MAX], canonical_root[PATH_MAX];
 char vid[8], pid[8], serial[SOSC_LAPIS_SERIAL_MAX + 2], iface[8];
 char *slash;
 size_t n;
 int have_interface = 0, depth;
 struct stat st;
 if (!p || p->mode != SOSC_LAPIS_USB_STRICT || !root || !devnode ||
     strncmp(devnode, "/dev/tty", 8) || !devnode[8] ||
     strchr(devnode + 5, '/')) return 0;
 /* Only canonical device names; /dev/serial/by-id is not a trust boundary. */
 if (!realpath(root, canonical_root)) return 0;
 if (snprintf(path, sizeof(path), "%s/class/tty/%s/device", canonical_root,
              devnode + 5) >= (int)sizeof(path) || !realpath(path, resolved)) return 0;
 n = strlen(canonical_root);
 if (strncmp(resolved, canonical_root, n) || resolved[n] != '/') return 0;
 for (depth = 0; depth < 32 && strlen(resolved) > n; ++depth) {
  if (stat(resolved, &st) || !S_ISDIR(st.st_mode)) return 0;
  if (!have_interface && present(resolved, "bInterfaceNumber")) {
   if (!field(resolved, "bInterfaceNumber", iface, sizeof(iface))) return 0;
   have_interface = 1;
  }
  if (present(resolved, "idVendor") || present(resolved, "idProduct") ||
      present(resolved, "serial")) {
   /* First enclosing USB device is authoritative; don't match an upstream hub. */
   return have_interface && field(resolved, "idVendor", vid, sizeof(vid)) &&
    field(resolved, "idProduct", pid, sizeof(pid)) &&
    field(resolved, "serial", serial, sizeof(serial)) &&
    sosc_lapis_usb_matches(p, vid, pid, serial, iface);
  }
  if (!(slash = strrchr(resolved, '/'))) return 0;
  *slash = 0;
 }
 return 0;
}

int sosc_lapis_usb_validate_devnode(const sosc_lapis_usb_policy_t *p,
 const char *devnode)
{
 struct stat st;
 char path[PATH_MAX], device_number[64], *end;
 unsigned long major_number, minor_number;
 if (!devnode || lstat(devnode, &st) || !S_ISCHR(st.st_mode) ||
     strncmp(devnode, "/dev/tty", 8) || strchr(devnode + 5, '/')) return 0;
 if (snprintf(path, sizeof(path), "/sys/class/tty/%s", devnode + 5) >= (int)sizeof(path) ||
     !field(path, "dev", device_number, sizeof(device_number))) return 0;
 major_number = strtoul(device_number, &end, 10);
 if (end == device_number || *end++ != ':') return 0;
 minor_number = strtoul(end, &end, 10);
 if (*end || major_number != major(st.st_rdev) || minor_number != minor(st.st_rdev)) return 0;
 return sosc_lapis_usb_validate_at(p, "/sys", devnode);
}
