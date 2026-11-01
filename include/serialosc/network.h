/* SPDX-License-Identifier: ISC */
#ifndef SERIALOSC_NETWORK_H
#define SERIALOSC_NETWORK_H

#include <lo/lo.h>

/* Both the supervisor and worker must use this constructor.
 * Default: exactly lo_server_new(port, error_handler), including NULL semantics.
 * SOSC_LAPIS_LOOPBACK_ONLY: UDP/IPv4 bound to exactly 127.0.0.1; NULL requests
 * an OS-assigned ephemeral port. Requires the private liblo patch under
 * platforms/lapis/patches. An absent/broken patch fails closed: the real socket
 * address is checked before returning. Caller owns the returned lo_server.
 */
lo_server sosc_network_server_new(const char *port, lo_err_handler error_handler);

#endif
