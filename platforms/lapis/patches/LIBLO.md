# Private liblo local-bind fix

Lapis strict CMake copies the pinned liblo into the build directory and applies liblo-explicit-unicast-bind.patch. The tracked submodule is never modified. The existing lo_server_new_from_config IP parameter previously selected address family/interface but still resolved a wildcard bind. The private patch binds an explicit unicast IP and resolves actual OS-assigned port zero.

Both supervisor and worker use sosc_network_server_new. Strict mode requires an actual kernel endpoint AF_INET UDP 127.0.0.1 with matching nonzero port; accidental unpatched linkage fails closed. Default builds retain upstream construction and network behavior. Zeroconf is disabled in the strict build.

The software harness tests exact socket address, real local OSC exchange and non-loopback-destination rejection with a reachable-address positive control if available. No daemon/USB or UDP12002 is used. Standalone tests/network_test.c also supports SOSC_NETWORK_EXPECT_REJECTED against stock liblo. Binding loopback limits remote network reachability; it does not authenticate local processes or make legacy LED messages owner-token-protected.
