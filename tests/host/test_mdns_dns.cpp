// Host test of the mDNS question parser and answer writer (src/mdns_dns.cpp): valid questions, other names,
// hostile packets (every truncation, loops, lying counts and lengths) and a deterministic fuzzer. Built and run
// by tools/test_host.sh with the address and undefined-behaviour sanitizers on.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "mdns_dns.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    checks++;                                                      \
    if (!(cond)) {                                                 \
      failures++;                                                  \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                              \
  } while (0)

typedef std::vector<uint8_t> Bytes;
static const char *HOST = "status-esp";
static const uint8_t IP[4] = {192, 168, 1, 50};

static void put16(Bytes &b, unsigned v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }

static void label(Bytes &b, const std::string &s) {
  b.push_back((uint8_t)s.size());
  b.insert(b.end(), s.begin(), s.end());
}

static Bytes header(unsigned id, unsigned flags, unsigned qd) {
  Bytes b;
  put16(b, id);
  put16(b, flags);
  put16(b, qd);
  put16(b, 0);
  put16(b, 0);
  put16(b, 0);
  return b;
}

static Bytes question(const std::string &host, unsigned type, unsigned cls, unsigned flags = 0, unsigned id = 0x1234) {
  Bytes b = header(id, flags, 1);
  label(b, host);
  label(b, "local");
  b.push_back(0);
  put16(b, type);
  put16(b, cls);
  return b;
}

static size_t ask(const Bytes &q, bool legacy, Bytes &out) {
  out.assign(512, 0xEE);
  size_t n = mdns_dns::reply(q.data(), q.size(), HOST, IP, legacy, out.data(), out.size());
  CHECK(n <= out.size());
  return n;
}

static unsigned rd16(const Bytes &b, size_t i) { return (unsigned)((b[i] << 8) | b[i + 1]); }

static void validQuestions() {
  Bytes out;
  size_t n = ask(question("status-esp", 1, 1), false, out);
  CHECK(n > 12);
  CHECK(rd16(out, 2) == 0x8400);
  CHECK(rd16(out, 4) == 0 && rd16(out, 6) == 1);
  CHECK(memcmp(&out[n - 4], IP, 4) == 0);   // the address ends the A record

  // Case variations of the name, the unicast-response bit, ANY class and type.
  CHECK(ask(question("STATUS-ESP", 1, 1), false, out) == n);
  CHECK(ask(question("Status-Esp", 1, 1), false, out) == n);
  CHECK(ask(question("status-esp", 1, 0x8001), false, out) == n);
  CHECK(ask(question("status-esp", 255, 1), false, out) == n);
  CHECK(ask(question("status-esp", 1, 255), false, out) > 0);

  // AAAA: an NSEC saying "no such record", longer than the A answer; not for a legacy resolver.
  size_t nsec = ask(question("status-esp", 28, 1), false, out);
  CHECK(nsec > 12 && rd16(out, 6) == 1);
  CHECK(ask(question("status-esp", 28, 1), true, out) == 0);

  // Legacy (unicast resolver): echoes the id and the question.
  size_t leg = ask(question("status-esp", 1, 1, 0, 0xBEEF), true, out);
  CHECK(leg > 0 && rd16(out, 0) == 0xBEEF && rd16(out, 4) == 1 && rd16(out, 6) == 1);

  // Two questions (A and AAAA) in one packet: both answered.
  Bytes q = header(1, 0, 2);
  for (unsigned t : {1u, 28u}) {
    label(q, "status-esp");
    label(q, "local");
    q.push_back(0);
    put16(q, t);
    put16(q, 1);
  }
  n = ask(q, false, out);
  CHECK(n > 0 && rd16(out, 6) == 2);

  // A second question that is a compression pointer to the first name.
  q = header(1, 0, 2);
  label(q, "status-esp");
  label(q, "local");
  q.push_back(0);
  put16(q, 1);
  put16(q, 1);
  q.push_back(0xC0);
  q.push_back(12);
  put16(q, 28);
  put16(q, 1);
  n = ask(q, false, out);
  CHECK(n > 0 && rd16(out, 6) == 2);

  // Announcement: an answer, no question, and it fits in 128 bytes but not in 20.
  uint8_t a[128];
  size_t an = mdns_dns::announce(HOST, IP, a, sizeof a);
  CHECK(an > 12 && a[7] == 1);
  uint8_t small[20];
  CHECK(mdns_dns::announce(HOST, IP, small, sizeof small) == 0);
  CHECK(mdns_dns::announce(HOST, IP, a, 0) == 0);
}

static void otherQuestions() {
  Bytes out;
  CHECK(ask(question("other", 1, 1), false, out) == 0);
  CHECK(ask(question("status-esp2", 1, 1), false, out) == 0);
  CHECK(ask(question("status-es", 1, 1), false, out) == 0);
  CHECK(ask(question("status-esp", 16, 1), false, out) == 0);    // TXT
  CHECK(ask(question("status-esp", 1, 3), false, out) == 0);     // class CH
  CHECK(ask(question("status-esp", 1, 1, 0x8000), false, out) == 0);   // a response, not a query
  CHECK(ask(question("status-esp", 1, 1, 0x2800), false, out) == 0);   // another opcode
  Bytes q = header(1, 0, 1);
  label(q, "status-esp");
  label(q, "example");
  q.push_back(0);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  q = header(1, 0, 1);
  label(q, "status-esp");
  q.push_back(0);   // no ".local"
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  q = header(1, 0, 1);
  label(q, "a");
  label(q, "status-esp");
  label(q, "local");
  q.push_back(0);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  CHECK(ask(header(1, 0, 0), false, out) == 0);   // no question at all
}

static void hostile() {
  Bytes out;
  Bytes good = question("status-esp", 1, 1);
  for (size_t len = 0; len <= good.size(); len++) {
    Bytes cut(good.begin(), good.begin() + len);
    size_t n = ask(cut, false, out);
    CHECK(len == good.size() || n == 0);   // a truncated question is never answered
    ask(cut, true, out);
  }
  // Count of questions lies (says 60000, holds one).
  Bytes q = good;
  q[4] = 0xEA;
  q[5] = 0x60;
  ask(q, false, out);
  // Zero length buffer / null-ish sizes.
  CHECK(mdns_dns::reply(good.data(), good.size(), HOST, IP, false, out.data(), 0) == 0);
  CHECK(mdns_dns::reply(good.data(), good.size(), HOST, IP, false, out.data(), 11) == 0);
  for (size_t cap = 0; cap < 80; cap++) {   // every too-small output buffer: refused, never overrun
    Bytes small(cap + 8, 0xAB);
    size_t n = mdns_dns::reply(good.data(), good.size(), HOST, IP, false, small.data(), cap);
    CHECK(n <= cap);
    for (size_t i = cap; i < small.size(); i++) CHECK(small[i] == 0xAB);
  }
  // Label length past the end.
  q = header(1, 0, 1);
  q.push_back(63);
  q.push_back('a');
  ask(q, false, out);
  // Pointer to itself, forward pointer, pointer past the end, pointer loop between two names.
  q = header(1, 0, 1);
  q.push_back(0xC0);
  q.push_back(12);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  q = header(1, 0, 1);
  q.push_back(0xC0);
  q.push_back(40);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  q = header(1, 0, 1);
  q.push_back(0xFF);
  q.push_back(0xFF);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  q = header(1, 0, 1);
  label(q, "status-esp");   // offset 12
  q.push_back(0xC0);        // pointer back to offset 12: a loop
  q.push_back(12);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
  // A long chain of backward pointers (more than the hop limit), ending in the real name.
  q = header(1, 0, 1);
  size_t nameAt = 12 + 40;
  for (int i = 0; i < 20; i++) { q.push_back(0xC0); q.push_back((uint8_t)(i == 0 ? nameAt : 12 + 2 * (i - 1))); }
  while (q.size() < nameAt) q.push_back(0);
  label(q, "status-esp");
  label(q, "local");
  q.push_back(0);
  put16(q, 1);
  put16(q, 1);
  ask(q, false, out);
  // Reserved label types 0x40 and 0x80.
  for (uint8_t t : {0x40, 0x80}) {
    q = header(1, 0, 1);
    q.push_back(t);
    q.insert(q.end(), 20, 'a');
    put16(q, 1);
    put16(q, 1);
    CHECK(ask(q, false, out) == 0);
  }
  // Maximum size packet of one label length 63 repeated.
  q = header(1, 0, 1);
  for (int i = 0; i < 10; i++) label(q, std::string(63, 'a'));
  q.push_back(0);
  put16(q, 1);
  put16(q, 1);
  CHECK(ask(q, false, out) == 0);
}

// splitmix64: a fixed, portable generator.
static uint64_t rng = 0x5EED5EEDULL;
static uint32_t rnd() {
  uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return (uint32_t)((z ^ (z >> 31)) >> 16);
}

static void fuzz() {
  std::vector<Bytes> seeds;
  seeds.push_back(question("status-esp", 1, 1));
  seeds.push_back(question("STATUS-ESP", 28, 0x8001));
  seeds.push_back(question("status-esp", 255, 255, 0, 0xFFFF));
  Bytes two = header(1, 0, 2);
  for (unsigned t : {1u, 28u}) { label(two, "status-esp"); label(two, "local"); two.push_back(0); put16(two, t); put16(two, 1); }
  two.push_back(0xC0); two.push_back(12); put16(two, 1); put16(two, 1);
  seeds.push_back(two);
  size_t answered = 0, total = 60000;
  for (size_t it = 0; it < total; it++) {
    Bytes q = seeds[rnd() % seeds.size()];
    unsigned muts = 1 + rnd() % 6;
    for (unsigned m = 0; m < muts; m++) {
      switch (rnd() % 5) {
        case 0: if (!q.empty()) q[rnd() % q.size()] = (uint8_t)rnd(); break;
        case 1: if (!q.empty()) q[rnd() % q.size()] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 2: if (!q.empty()) q.resize(rnd() % (q.size() + 1)); break;
        case 3: q.insert(q.begin() + (q.empty() ? 0 : rnd() % q.size()), (uint8_t)rnd()); break;
        case 4: if (q.size() > 1) q.erase(q.begin() + rnd() % q.size()); break;
      }
    }
    size_t cap = (rnd() % 4 == 0) ? rnd() % 96 : 512;
    Bytes out(cap + 16, 0xA5);
    // Copy to an exactly sized heap buffer so the sanitizer sees any read past the end.
    uint8_t *exact = new uint8_t[q.size() ? q.size() : 1];
    if (!q.empty()) memcpy(exact, q.data(), q.size());
    size_t n = mdns_dns::reply(exact, q.size(), HOST, IP, rnd() & 1, out.data(), cap);
    delete[] exact;
    CHECK(n <= cap);
    for (size_t i = cap; i < out.size(); i++) CHECK(out[i] == 0xA5);
    if (n) answered++;
  }
  printf("mdns fuzz: %zu of %zu mutated packets answered\n", answered, total);
}

int main() {
  validQuestions();
  otherQuestions();
  hostile();
  fuzz();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
