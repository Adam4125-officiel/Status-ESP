// Tests for origin_check::allowed(): the cross-site check on the web API's state-changing requests.
#include <stdio.h>
#include <string.h>

#include <string>

#include "origin_check.h"

static int failures = 0;

static void expect(bool got, bool want, const char *origin, const char *host, bool ap) {
  if (got != want) {
    failures++;
    printf("FAIL: Origin=%s Host=%s ap=%d: got %s, wanted %s\n", origin ? origin : "(null)", host ? host : "(null)",
           ap, got ? "allow" : "refuse", want ? "allow" : "refuse");
  }
}

static void allow(const char *origin, const char *host) { expect(origin_check::allowed(origin, host, false), true, origin, host, false); }
static void refuse(const char *origin, const char *host) { expect(origin_check::allowed(origin, host, false), false, origin, host, false); }

int main() {
  // No Origin: curl, tools/upload.sh, tools/upload.ps1.
  allow(nullptr, "192.168.1.50");
  allow("", "192.168.1.50");
  allow(nullptr, nullptr);

  // The device's own pages, however it is reached.
  allow("http://192.168.1.50", "192.168.1.50");
  allow("http://status-esp.local", "status-esp.local");
  allow("http://my-router-name", "my-router-name");
  allow("http://192.168.1.50:8080", "192.168.1.50:8080");
  allow("http://[fe80::1]", "[fe80::1]");
  allow("http://[fe80::1]:81", "[fe80::1]:81");

  // Case does not matter in a host name or in the scheme.
  allow("http://Status-ESP.LOCAL", "status-esp.local");
  allow("http://status-esp.local", "STATUS-ESP.local");
  allow("HTTP://status-esp.local", "status-esp.local");

  // Default ports.
  allow("http://x", "x:80");
  allow("http://x:80", "x");
  allow("http://x:80", "x:80");
  allow("https://x", "x");             // a reverse proxy that terminates TLS and keeps Host
  allow("https://x", "x:443");
  allow("https://x:443", "x");
  refuse("http://x", "x:81");
  refuse("http://x:81", "x");
  refuse("http://x:81", "x:80");
  refuse("https://x", "x:80");
  refuse("http://x:8080", "x:80");

  // Another site.
  refuse("http://evil.example", "192.168.1.50");
  refuse("https://evil.example", "192.168.1.50");
  refuse("http://192.168.1.51", "192.168.1.50");
  refuse("http://192.168.1.5", "192.168.1.50");
  refuse("http://192.168.1.500", "192.168.1.50");
  refuse("http://status-esp.local.evil.example", "status-esp.local");
  refuse("http://evil.example/status-esp.local", "status-esp.local");
  refuse("http://status-esp.local@evil.example", "status-esp.local");
  refuse("http://evil.example@status-esp.local", "status-esp.local");
  refuse("http://status-esp.local:80@evil.example", "status-esp.local");
  refuse("http://x", "x.");

  // null, other schemes, garbage.
  refuse("null", "x");
  refuse("NULL", "x");
  refuse("file://", "x");
  refuse("file:///x", "x");
  refuse("ftp://x", "x");
  refuse("chrome-extension://x", "x");
  refuse("x", "x");
  refuse("http:/x", "x");
  refuse("http://", "x");
  refuse("http://", "");
  refuse("http://x", "");
  refuse("http://x", nullptr);
  refuse("http://x:", "x");
  refuse("http://x:abc", "x");
  refuse("http://x:99999", "x:99999");
  refuse("http://x:123456", "x:123456");
  refuse("http://x:-1", "x:-1");
  refuse("http://x", "x:");
  refuse("http://x", "x:abc");
  refuse("http://[x", "[x");
  refuse("http://[::1]x", "[::1]x");
  refuse("http://x y", "x y");
  refuse("http://x\t", "x\ty");
  refuse("http://:80", ":80");
  refuse("http://x/", "x/");
  refuse("http://x?a", "x?a");

  // A Host with surrounding blanks (some clients) is still the Host.
  allow("http://x", " x ");

  // Rescue access point: never enforced.
  expect(origin_check::allowed("http://evil.example", "192.168.4.1", true), true, "http://evil.example", "192.168.4.1", true);
  expect(origin_check::allowed("null", "192.168.4.1", true), true, "null", "192.168.4.1", true);
  expect(origin_check::allowed("garbage", nullptr, true), true, "garbage", nullptr, true);

  // Very long input: no out-of-bounds read, refused, and a long legitimate-looking pair is refused too
  // (no real name is that long).
  {
    std::string longHost(100000, 'a');
    std::string origin = "http://" + longHost;
    refuse(origin.c_str(), longHost.c_str());
    refuse("http://x", longHost.c_str());
    refuse(origin.c_str(), "x");
    std::string longPort = "http://x:" + std::string(100000, '1');
    refuse(longPort.c_str(), "x");
    std::string justOver = "http://" + std::string(256, 'a');
    refuse(justOver.c_str(), std::string(256, 'a').c_str());
    std::string justFits(255, 'a');
    allow(("http://" + justFits).c_str(), justFits.c_str());
    // No terminating issue: a buffer that is exactly the text.
    char exact[8];
    memcpy(exact, "http://x", 8);
    std::string copy(exact, 8);
    allow(copy.c_str(), "x");
  }

  // Every prefix of a valid pair and every single-byte mutation: must not crash, nothing else asserted.
  {
    const char *origin = "https://Status-ESP.local:8443";
    const char *host = "status-esp.local:8443";
    size_t n = strlen(origin);
    for (size_t i = 0; i <= n; i++) {
      std::string o(origin, i);
      (void)origin_check::allowed(o.c_str(), host, false);
      (void)origin_check::allowed(host, o.c_str(), false);
    }
    for (size_t i = 0; i < n; i++) {
      for (int c = 0; c < 256; c++) {
        std::string o(origin);
        o[i] = (char)(c ? c : 1);
        (void)origin_check::allowed(o.c_str(), host, false);
      }
    }
  }

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("origin_check: all tests passed\n");
  return 0;
}
