// Tests cmpVer() of web/index.html: the function is cut out between its two marker comments and evaluated.
const fs = require("fs"), assert = require("assert"), path = require("path");
const html = fs.readFileSync(path.join(__dirname, "../../web/index.html"), "utf8");
const m = /\/\* >>> cmpVer \*\/([\s\S]*?)\/\* <<< cmpVer \*\//.exec(html);
assert(m, "cmpVer markers not found in web/index.html");
const cmpVer = new Function(m[1] + "; return cmpVer;")();
const sign = (a, b) => Math.sign(cmpVer(a, b));
const cases = [
  ["v1.2.0", "1.1.2-rc.1", 1], ["1.2.0", "1.2.0", 0], ["v1.2.0", "1.2.0", 0], ["1.1.1", "1.2.0", -1],
  ["v1.2.0", "1.2.0-rc.3", 1], ["1.2.0-rc.3", "1.2.0", -1], ["v1.2.0-rc.2", "1.2.0-rc.1", 1],
  ["1.2.0-rc.10", "1.2.0-rc.9", 1], ["1.2.0-rc.1", "1.2.0-rc.1", 0], ["1.10.0", "1.9.9", 1],
  ["2.0.0-rc.1", "1.99.99", 1], ["1.2.1-rc.1", "1.2.0", 1], ["1.2.0-rc.5", "1.1.9", 1],
];
for (const [a, b, want] of cases) assert.strictEqual(sign(a, b), want, `cmpVer(${a}, ${b})`);
for (const bad of ["", "latest", "1.2", "v1.2.0-beta", "1.2.0.1", undefined, null]) {
  assert(Number.isNaN(cmpVer(bad, "1.0.0")) && Number.isNaN(cmpVer("1.0.0", bad)), "unreadable: " + bad);
}
console.log("cmpVer: " + cases.length + " ordering cases and the unreadable ones passed");
