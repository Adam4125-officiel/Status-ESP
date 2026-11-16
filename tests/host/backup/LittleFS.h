// Stand-in for LittleFS: an in-memory file system that records every change, so a test can assert what a
// restore wrote, renamed, removed or created, and that nothing was ever formatted.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Arduino.h"

struct FSInfo {
  size_t totalBytes, usedBytes;
};

struct FakeFs {
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  size_t total = 1000000;             // what info() reports as the size of the storage
  size_t writeBudget = (size_t)-1;    // bytes that can still be written before a write comes up short
  bool renameFails = false;           // every rename fails
  bool renameOverFails = true;        // like the LittleFS versions the code has a fallback for
  // The record of what happened:
  std::vector<std::string> creates;   // files opened for writing
  std::vector<std::pair<std::string, std::string>> renames;   // from, to
  std::vector<std::string> removes;
  std::vector<std::string> mkdirs;
  int formats = 0;
  void clearLog() { creates.clear(); renames.clear(); removes.clear(); mkdirs.clear(); formats = 0; }
  size_t used() const {
    size_t u = 0;
    for (auto &f : files) u += f.second.size();
    return u;
  }
};
extern FakeFs fakeFs;

class File {
 public:
  File() {}
  File(const std::string &path, bool write) : path_(path), write_(write), open_(true) {
    if (!write) data_ = fakeFs.files[path];
  }
  explicit operator bool() const { return open_; }
  size_t write(const uint8_t *d, size_t n) {
    if (!open_ || !write_) return 0;
    // The storage is full when the data written so far plus the rest of the files exceed its size.
    size_t room = fakeFs.total > fakeFs.used() + data_.size() ? fakeFs.total - fakeFs.used() - data_.size() : 0;
    if (n > room) n = room;
    if (n > fakeFs.writeBudget) n = fakeFs.writeBudget;
    fakeFs.writeBudget -= n;
    data_.append((const char *)d, n);
    return n;
  }
  size_t read(uint8_t *d, size_t n) {
    size_t left = data_.size() - pos_;
    if (n > left) n = left;
    memcpy(d, data_.data() + pos_, n);
    pos_ += n;
    return n;
  }
  void close() {
    if (open_ && write_) fakeFs.files[path_] = data_;
    open_ = false;
  }

 private:
  std::string path_, data_;
  bool write_ = false, open_ = false;
  size_t pos_ = 0;
};

class Dir {
 public:
  Dir() {}
  Dir(const std::string &dir) {
    std::string prefix = dir == "/" ? "/" : dir + "/";
    std::set<std::string> seen;
    for (auto &f : fakeFs.files) {
      if (f.first.compare(0, prefix.size(), prefix) != 0) continue;
      std::string rest = f.first.substr(prefix.size());
      size_t slash = rest.find('/');
      if (slash == std::string::npos) entries_.push_back({rest, false, f.second.size()});
      else if (seen.insert(rest.substr(0, slash)).second) entries_.push_back({rest.substr(0, slash), true, 0});
    }
  }
  bool next() { return ++i_ < (int)entries_.size(); }
  String fileName() { return String(entries_[i_].name.c_str()); }
  bool isDirectory() { return entries_[i_].dir; }
  size_t fileSize() { return entries_[i_].size; }

 private:
  struct Entry { std::string name; bool dir; size_t size; };
  std::vector<Entry> entries_;
  int i_ = -1;
};

struct LittleFSClass {
  Dir openDir(const String &d) { return Dir(d.c_str()); }
  File open(const String &p, const char *mode) {
    bool w = mode[0] == 'w';
    if (w) fakeFs.creates.push_back(p.c_str());
    else if (!fakeFs.files.count(p.c_str())) return File();
    return File(p.c_str(), w);
  }
  bool remove(const String &p) {
    fakeFs.removes.push_back(p.c_str());
    return fakeFs.files.erase(p.c_str()) > 0;
  }
  bool rename(const String &a, const String &b) {
    fakeFs.renames.push_back({a.c_str(), b.c_str()});
    auto it = fakeFs.files.find(a.c_str());
    if (it == fakeFs.files.end() || fakeFs.renameFails) return false;
    if (fakeFs.files.count(b.c_str()) && fakeFs.renameOverFails) return false;
    std::string data = it->second;
    fakeFs.files.erase(it);
    fakeFs.files[b.c_str()] = data;
    return true;
  }
  bool mkdir(const String &p) {
    fakeFs.mkdirs.push_back(p.c_str());
    fakeFs.dirs.insert(p.c_str());
    return true;
  }
  bool info(FSInfo &i) {
    i.totalBytes = fakeFs.total;
    i.usedBytes = fakeFs.used();
    return true;
  }
  bool format() { fakeFs.formats++; return true; }
};
extern LittleFSClass LittleFS;
