#include "mdns.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "config.h"
#include "mdns_dns.h"

namespace mdns {

namespace {

const uint16_t MDNS_PORT = 5353;
const size_t MAX_QUERY = 256;     // longer datagrams are not for us
const size_t MAX_REPLY = 160;

WiFiUDP udp;
bool running = false;

void currentIp(uint8_t ip[4]) {
  IPAddress a = WiFi.localIP();
  for (int i = 0; i < 4; i++) ip[i] = a[i];
}

void sendMulticast(const uint8_t *data, size_t length) {
  if (!udp.beginPacketMulticast(IPAddress(224, 0, 0, 251), MDNS_PORT, WiFi.localIP())) return;
  udp.write(data, length);
  udp.endPacket();
}

}  // namespace

bool begin() {
  running = udp.beginMulticast(WiFi.localIP(), IPAddress(224, 0, 0, 251), MDNS_PORT) != 0;
  if (running) {
    uint8_t ip[4], out[MAX_REPLY];
    currentIp(ip);
    size_t n = mdns_dns::announce(config::HOSTNAME, ip, out, sizeof(out));
    if (n) sendMulticast(out, n);
  }
  return running;
}

void loop() {
  if (!running) return;
  int size = udp.parsePacket();
  if (size <= 0) return;
  if ((size_t)size > MAX_QUERY) {
    udp.flush();
    return;
  }
  uint8_t query[MAX_QUERY];
  int length = udp.read(query, sizeof(query));
  if (length <= 0) return;
  IPAddress from = udp.remoteIP();
  uint16_t fromPort = udp.remotePort();
  bool legacy = fromPort != MDNS_PORT;

  uint8_t ip[4], out[MAX_REPLY];
  currentIp(ip);
  size_t n = mdns_dns::reply(query, (size_t)length, config::HOSTNAME, ip, legacy, out, sizeof(out));
  if (!n) return;
  if (legacy) {   // a plain resolver asked from a random port: answer it directly
    if (!udp.beginPacket(from, fromPort)) return;
    udp.write(out, n);
    udp.endPacket();
  } else {
    sendMulticast(out, n);
  }
}

bool active() { return running; }

String name() { return running ? String(config::HOSTNAME) + ".local" : String(); }

}  // namespace mdns
