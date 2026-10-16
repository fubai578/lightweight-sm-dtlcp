/* Compile-only size probe: symbol sizes expose opaque embedded structures to nm. */
#include "dtlcp_internal.h"

const unsigned char size_probe_dtlcp_ctx[sizeof(dtlcp_ctx)] = {0};
const unsigned char size_probe_dtlcp_datagram[sizeof(dtlcp_datagram)] = {0};
const unsigned char size_probe_dtlcp_rec[sizeof(dtlcp_rec)] = {0};
