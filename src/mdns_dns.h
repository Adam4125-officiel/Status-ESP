// The DNS side of the tiny mDNS responder (see mdns.h): pure functions on byte buffers,
// no Arduino and no network, so they can be exercised on a PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mdns_dns {

// `query` is one received mDNS / DNS datagram. When it asks for the A or AAAA record of
// "<host>.local" (class IN, or ANY), writes the answer into `out` and returns its length;
// returns 0 when there is nothing to say (not a query, another name, malformed, too long
// for `cap`). A asks get the address; AAAA asks get an NSEC record saying "no such record
// here", so a client that asks for both is not left waiting for the second.
// `legacy` is true when the query did not come from port 5353 (a plain unicast resolver):
// the answer then echoes the query id and question, has a short TTL, and no NSEC.
size_t reply(const uint8_t *query, size_t length, const char *host, const uint8_t ip[4], bool legacy,
             uint8_t *out, size_t cap);

// The unsolicited announcement sent when the address becomes known (an answer, no question).
size_t announce(const char *host, const uint8_t ip[4], uint8_t *out, size_t cap);

}  // namespace mdns_dns
