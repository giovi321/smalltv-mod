#pragma once
#include "ThemeEngine.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

namespace smalltv {
constexpr size_t MaxDataBody = 2048;
constexpr uint32_t DataTimeoutMs = 3000;

// The most heap the parse of one reply may take (the filter, the document and
// ArduinoJson's strings together, counted by the allocator; see
// extractThemeValues). ArduinoJson's first pool is 1,024 B on the device, so
// this allows one pool and the strings next to it, and no second pool: the
// real weather replies need 1.4 to 1.8 KB (tests/theme/test_memory.cpp).
constexpr size_t DataParseBudget = 2048;

// What a data fetch needs on the ESP8266 before it may start. An allocation
// that fails in `new` (the HTTP client, the parse) restarts the device, so a
// fetch short of heap is refused instead (the source stays as it was and is
// tried again, see fetchWaitMs).
//  - FetchFreeHeap: the HTTP client and the socket. A GUESS, not measured on
//    the device.
//  - FetchLargestBlock: the largest single block a fetch asks for. The reply
//    is not held (it goes from the socket into a filtered parse), so this is
//    ArduinoJson's first pool, 1,024 B plus 8 of the allocator's. Measured on
//    the host (32-bit): 1,032 B for the real replies and the worst ones;
//    tests/theme/test_memory.cpp ties the constant to that (at most 1,024
//    over, 512 is the slack).
//  - FetchParseHeap: free heap needed once the connection is up, just before
//    the parse. Measured on the host: the peak of the parse (the filter, the
//    document and the values) is 1,792 B for a 6-field weather reply and
//    1,376 B for a 2-field one; a worst-case reply is stopped by
//    DataParseBudget. Tied in test_memory.cpp, at most 1,024 over.
constexpr uint32_t FetchFreeHeap = 4096;
constexpr uint32_t FetchLargestBlock = 1536;
constexpr uint32_t FetchParseHeap = 2048;

// A reply as the parse takes it: bytes in the order they arrive, read from the
// network only as the parser asks, so no copy of the reply is ever held.
// Stream supplies a monotonic clock and nonblocking socket reads. The total
// elapsed time is limited, slow trickles included; a socket's idle timeout is
// insufficient.
// `length` is the declared Content-Length, or -1 for chunked/unknown length
// (HTTPClient already de-chunks the stream; here that just means "read until
// the connection closes" instead of "read until N bytes"). Either way the
// reply is capped at MaxDataBody: a longer one fails.
template <class Stream>
class BodyReader : public DataReply {
 public:
  BodyReader(Stream& stream, int length) : stream_(stream), length_(length), started_(stream.now()) {}
  int read() override {
    char c;
    return readBytes(&c, 1) ? static_cast<unsigned char>(c) : -1;
  }
  size_t readBytes(char* dest, size_t count) override {
    if (at_ == have_ && !fill()) return 0;
    const size_t n = std::min(count, have_ - at_);
    memcpy(dest, buffer_ + at_, n);
    at_ += n;
    return n;
  }
  // The document is complete: the rest of the reply is read and dropped, so a
  // reply that goes on past the cap, stops short of its declared length, or
  // never ends still fails. True when the reply was whole.
  bool finish() {
    do at_ = have_; while (fill());
    return !failed_ && (length_ < 0 || taken_ == static_cast<size_t>(length_));
  }
 private:
  // Waits for at least one more byte. False at the end of the reply, or when
  // the fetch must stop (deadline, cancel, past the cap); `failed_` tells which.
  bool fill() {
    at_ = have_ = 0;
    for (;;) {
      if (failed_ || ended_) return false;
      if (stream_.cancelled() || uint32_t(stream_.now() - started_) >= DataTimeoutMs) {
        failed_ = true;
        return false;
      }
      if (length_ > 0 && taken_ == static_cast<size_t>(length_)) {
        ended_ = true;
        return false;
      }
      const int available = stream_.available();
      if (available > 0) {
        const size_t limit = length_ > 0 ? static_cast<size_t>(length_) : MaxDataBody;
        if (taken_ == limit) {
          failed_ = true;
          return false;
        }
        const size_t count = std::min(sizeof(buffer_), std::min(static_cast<size_t>(available), limit - taken_));
        const int got = stream_.read(buffer_, count);
        if (got > 0) {
          taken_ += got;
          have_ = static_cast<size_t>(got);
          return true;
        }
      } else if (!stream_.connected()) {
        // On the ESP8266, available() lets the network stack run when it finds
        // nothing and still returns 0, even if the rest of the reply arrived
        // with the server's close meanwhile. Those bytes are waiting: read them.
        if (stream_.available() > 0) continue;
        ended_ = true;
        return false;
      }
      stream_.idle();
    }
  }
  Stream& stream_;
  const int length_;
  const uint32_t started_;
  uint8_t buffer_[32];
  size_t at_ = 0, have_ = 0, taken_ = 0;
  bool failed_ = false, ended_ = false;
};

// Reads one reply and appends the source's values (extractThemeValues): false,
// with `values` unchanged, for anything but a whole, valid reply.
template <class Stream>
bool readDataValues(Stream& stream, int length, const ThemeDataSource& source, std::vector<ThemeValue>& values, size_t budget = DataParseBudget) {
  if (length == 0 || length < -1 || length > static_cast<int>(MaxDataBody)) return false;
  BodyReader<Stream> reader(stream, length);
  const size_t before = values.size();
  if (!extractThemeValues(reader, source, values, budget)) return false;
  if (reader.finish()) return true;
  values.erase(values.begin() + before, values.end());
  return false;
}

// How the theme data fetches ended since boot: how many were refused for lack
// of memory, and how the last one ended. 4 bytes of RAM. `outcome` is
// 0 ok, 1 refused, 2 failed (anything else is ignored).
struct FetchStats {
  uint16_t refused = 0;
  uint8_t last = 0;   // 0 none yet, else outcome + 1
  void note(int outcome) {
    if (outcome < 0 || outcome > 2) return;
    if (outcome == 1 && refused < 65535) ++refused;
    last = uint8_t(outcome + 1);
  }
  bool any() const { return last != 0; }
};

// One in-flight (or just-completed) fetch, shared between the display loop
// and whatever actually performs the request -- a FreeRTOS task on ESP32, or
// a direct synchronous call on ESP8266 (see startThemeDataFetch in
// ThemeDataClient.cpp for both). `cancelled_`/`finished_` are the only fields
// either side may touch after construction, so they are atomic; `source`,
// `index`, and `values` are written once by their respective owner and never
// contended. `finish()`'s release / `finished()`'s acquire pairing is what
// makes `values` (written just before finish()) visible to the reader once
// finished() observes true.
struct DataFetch {
  ThemeDataSource source;
  size_t index;
  std::vector<ThemeValue> values;
  bool success = false;
  DataFetch(const ThemeDataSource& s, size_t i) : source(s), index(i) {}
  void cancel() { cancelled_.store(true); }
  bool cancelled() const { return cancelled_.load(); }
  void finish(bool ok) {
    success = ok;
    finished_.store(true, std::memory_order_release);
  }
  bool finished() const { return finished_.load(std::memory_order_acquire); }
 private:
  std::atomic<bool> cancelled_{false}, finished_{false};
};

// How old each source's data is, for the "<source>.age" value a spec 2 theme
// can read (docs/features/themes.md): whole minutes since the source's last
// successful fetch. Unknown until the first success, and then no value is
// published, so a binding keeps its static value, text shows "--" and a
// `show` rule with "missing": true holds. A failed fetch (no connection, a
// bad status, a cut-off or unreadable reply) changes nothing, so the age keeps
// growing from the last success.
// It counts the millis() ticks it is given and needs no wall clock, so it
// works before NTP. tick() must be called more often than millis() wraps
// (49.7 days); it adds the difference to the last call, which survives the
// wrap. The age stops at 71582 minutes (about 49.7 days).
class DataAges {
 public:
  // Starts over for a theme with `count` sources: every age is unknown.
  // `enabled` is false for a spec 1 theme: it has no "<source>.age" values, so
  // no age is ever known for it and publish() has nothing to publish.
  void reset(size_t count, bool enabled = true) {
    ageMs_.assign(enabled ? count : 0, Unknown);
    published_.assign(count, Unpublished);   // the first publish() looks at every source
    started_ = false;
  }
  // Moves every known age on to `now`. The first call after reset() only sets
  // the time base.
  void tick(uint32_t now) {
    const uint32_t delta = started_ ? uint32_t(now - last_) : 0;
    started_ = true;
    last_ = now;
    for (auto& age : ageMs_)
      if (age != Unknown) age = delta >= MaxAge - age ? MaxAge : age + delta;
  }
  // A fetch of source `index` ended. Only a success sets its age to 0.
  void result(size_t index, bool success) {
    if (success && index < ageMs_.size()) ageMs_[index] = 0;
  }
  // Whole minutes, or -1 while unknown.
  int minutes(size_t index) const {
    return index < ageMs_.size() && ageMs_[index] != Unknown ? int(ageMs_[index] / 60000) : -1;
  }
  // Brings the "<source>.age" entries of `values` up to date and tells whether
  // anything changed. A source that declares a field called "age" of its own
  // keeps it: nothing is added for that source.
  bool publish(const std::vector<ThemeDataSource>& sources, std::vector<ThemeValue>& values) {
    // Runs on every frame, and a minute changes once a minute. So remember
    // which whole-minute value each source was last published with, and build
    // a key or a number text (a heap allocation each) only for a source whose
    // value moved. A source's own "age" field is skipped by the same rule.
    if (published_.size() < sources.size()) published_.resize(sources.size(), Unpublished);
    bool changed = false;
    for (size_t i = 0; i < sources.size(); ++i) {
      if (minutes(i) == published_[i]) continue;
      published_[i] = minutes(i);
      bool own = false;
      for (const auto& field : sources[i].fields) own = own || field.id == "age";
      if (own) continue;
      const std::string key = sources[i].id + ".age";
      auto it = std::find_if(values.begin(), values.end(), [&](const ThemeValue& v) { return v.key == key; });
      const int age = minutes(i);
      if (age < 0) {
        if (it != values.end()) {
          values.erase(it);
          changed = true;
        }
        continue;
      }
      const std::string text = std::to_string(age);
      if (it == values.end()) {
        values.push_back({key, text});
        changed = true;
      } else if (it->value != text) {
        it->value = text;
        changed = true;
      }
    }
    return changed;
  }
 private:
  // An enum, so neither value is ever stored in or read from memory (C++11
  // would need a definition for a static constexpr member that is odr-used).
  enum : uint32_t { Unknown = 0xffffffffu, MaxAge = 0xfffffffeu };
  // What publish() last did for each source: its whole-minute value (-1 for
  // unknown, which is "no value"), or Unpublished before it looked at all.
  enum : int { Unpublished = -2 };
  std::vector<uint32_t> ageMs_;
  std::vector<int> published_;
  uint32_t last_ = 0;
  bool started_ = false;
};

// Takes in one finished fetch: tells `ages` how it ended and, on a success,
// merges its values into `all` by key rather than replacing the set, so a
// source's fields persist across refreshes of the *other* sources and a slow
// source does not blank fields a fast one already delivered. A failed fetch
// leaves the values and the age alone. True when `all` changed.
inline bool collectFetch(DataFetch& result, DataAges& ages, std::vector<ThemeValue>& all) {
  ages.result(result.index, result.success);
  if (!result.success) return false;
  for (auto& value : result.values) {
    auto existing = std::find_if(all.begin(), all.end(), [&](const ThemeValue& old) { return old.key == value.key; });
    if (existing == all.end()) all.push_back(std::move(value));
    else existing->value = std::move(value.value);
  }
  return true;
}

// When a source is fetched again. Each source keeps one byte, `tries`:
// 0 = never started; otherwise 1 + the number of fetches in a row that
// failed (it stops at 255, so 254 failures count and then no more). One byte
// holds both facts, so it replaces the old one-bit "scheduled" flag and adds
// no allocation.
//
// After a failure the wait is 1, 2, 4 and 8 minutes, then the source's own
// interval, and it stays there until a fetch works. A success goes straight
// back to the interval and clears the count. A wait is never longer than the
// interval, so a theme with a short interval fetches no less often than
// before. At most 4 retries come sooner than the interval: a failed fetch
// blocks the loop for up to 3 s on the ESP8266.
constexpr uint32_t RetryFirstWaitMs = 60000;
constexpr uint8_t RetryShortWaits = 4;

// A source is due when it was never started, or when `nowMs` has reached
// `nextMs`. The test is the signed difference, so it survives the wrap of
// millis(); waits are at most a day (the interval is 86,400 s at most), far
// under the 24.8 days that the signed test can tell apart.
inline bool fetchDue(uint8_t tries, uint32_t nextMs, uint32_t nowMs) {
  return tries == 0 || static_cast<int32_t>(nowMs - nextMs) >= 0;
}
// A fetch of the source starts: it is scheduled from now on. The count of
// failures stays.
inline void fetchStarted(uint8_t& tries) {
  if (tries == 0) tries = 1;
}
// Failures in a row so far.
inline uint8_t fetchFailures(uint8_t tries) { return tries == 0 ? 0 : tries - 1; }
// A fetch of the source ended: updates the count and returns the wait until the
// next one, in milliseconds. `intervalS` is the source's interval in seconds
// (the theme parser allows 10 to 86,400, so the product cannot overflow).
inline uint32_t fetchWaitMs(uint32_t intervalS, bool success, uint8_t& tries) {
  const uint32_t intervalMs = intervalS * 1000u;
  if (success) {
    tries = 1;
    return intervalMs;
  }
  if (tries == 0) tries = 1;
  if (tries < 255) ++tries;
  const uint8_t failures = fetchFailures(tries);
  if (failures > RetryShortWaits) return intervalMs;
  const uint32_t retryMs = RetryFirstWaitMs << (failures - 1);
  return retryMs < intervalMs ? retryMs : intervalMs;
}

// The worker owns a shared reference, so changing/unloading a theme never frees
// its request underneath it. Only the display loop consumes completed values.
class DataRequests {
 public:
  ~DataRequests() { cancel(); }
  bool busy() const { return bool(pending_); }
  // Refuses a second request while one is already pending -- callers must
  // check busy() first; this only guards against a caller that does not.
  std::shared_ptr<DataFetch> start(const ThemeDataSource& source, size_t index) {
    if (pending_) return {};
    pending_ = std::make_shared<DataFetch>(source, index);
    return pending_;
  }
  void cancel() { if (pending_) pending_->cancel(); }
  // Returns the finished result once, or nothing if still pending or if it
  // was cancelled (a cancelled fetch's values are stale by definition, e.g.
  // from a theme that has since been switched away from).
  std::shared_ptr<DataFetch> take() {
    if (!pending_ || !pending_->finished()) return {};
    auto result = std::move(pending_);
    return result->cancelled() ? std::shared_ptr<DataFetch>{} : result;
  }
 private:
  std::shared_ptr<DataFetch> pending_;
};
}
