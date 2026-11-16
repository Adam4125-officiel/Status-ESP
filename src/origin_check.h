// Cross-site request check for the web API (pure: no Arduino, built on the PC by tools/test_host.sh).
#pragma once

namespace origin_check {

// True when a state-changing request (anything but GET / HEAD / OPTIONS) may go on.
// `origin` is the request's Origin header ("" or nullptr when it has none), `host` its Host header.
// A request without an Origin (curl, the upload scripts) is allowed. One with an Origin is allowed
// only when the Origin's host and port are the Host header's: a page served by the device itself,
// whatever name the owner reached it by, always passes; a page on another site never does
// ("null" included). Not enforced in the rescue access point (`isAp`): nothing may stand between a
// lost device and /update there (CLAUDE.md rules 1 and 11).
bool allowed(const char *origin, const char *host, bool isAp);

}  // namespace origin_check
