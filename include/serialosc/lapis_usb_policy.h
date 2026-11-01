/* Pure, fail-closed Linux candidate policy. No device I/O. */
#ifndef SERIALOSC_LAPIS_USB_POLICY_H
#define SERIALOSC_LAPIS_USB_POLICY_H

#define SOSC_LAPIS_SERIAL_MAX 128

typedef enum {
 SOSC_LAPIS_USB_INVALID = -1,
 SOSC_LAPIS_USB_LEGACY = 0,
 SOSC_LAPIS_USB_STRICT = 1
} sosc_lapis_usb_mode_t;

typedef struct {
 sosc_lapis_usb_mode_t mode;
 unsigned int vid, pid, interface_number;
 char serial[SOSC_LAPIS_SERIAL_MAX + 1];
} sosc_lapis_usb_policy_t;

/* NULL means absent; empty values are present and malformed. */
int sosc_lapis_usb_policy_parse(sosc_lapis_usb_policy_t *policy,
 const char *strict, const char *vid, const char *pid,
 const char *serial, const char *interface_number);
int sosc_lapis_usb_policy_from_env(sosc_lapis_usb_policy_t *policy);
/* Candidate fields must come from the same tty's USB ancestry, not labels. */
int sosc_lapis_usb_matches(const sosc_lapis_usb_policy_t *policy,
 const char *vid, const char *pid, const char *serial,
 const char *interface_number);
/* Linux: sysfs metadata only; no tty open. Requires a canonical /dev/tty* path.
 * Returns 1 only for an exact strict match. This narrows, not eliminates, TOCTOU. */
int sosc_lapis_usb_validate_devnode(const sosc_lapis_usb_policy_t *policy,
 const char *devnode);
/* Injectable sysfs root for synthetic-filesystem tests; production uses /sys. */
int sosc_lapis_usb_validate_at(const sosc_lapis_usb_policy_t *policy,
 const char *sysfs_root, const char *devnode);
#endif
