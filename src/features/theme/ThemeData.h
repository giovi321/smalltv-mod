#pragma once
#include "ThemeEngine.h"
#include <algorithm>
#include <atomic>
#include <memory>

namespace smalltv {
constexpr size_t MaxDataBody = 2048;
constexpr uint32_t DataTimeoutMs = 3000;

// Stream supplies a monotonic clock and nonblocking socket reads. Limit total
// elapsed time, including slow trickles; a socket's idle timeout is insufficient.
// `length` is the declared Content-Length, or -1 for chunked/unknown length
// (HTTPClient already de-chunks the stream; here that just means "read until
// the connection closes" instead of "read until N bytes"), capped either way
// at MaxDataBody so a source cannot force an unbounded allocation.
template <class Stream>
bool readDataBody(Stream& stream, int length, std::string& body) {
  body.clear();
  if (length == 0 || length < -1 || length > static_cast<int>(MaxDataBody)) return false;
  body.reserve(length > 0 ? static_cast<size_t>(length) : MaxDataBody);
  const uint32_t started = stream.now();
  uint8_t buffer[128];
  for (;;) {
    if (stream.cancelled() || uint32_t(stream.now() - started) >= DataTimeoutMs) return false;
    if (length > 0 && body.size() == static_cast<size_t>(length)) return true;
    int available = stream.available();
    if (available > 0) {
      const size_t limit = length > 0 ? static_cast<size_t>(length) : MaxDataBody;
      if (body.size() == limit) return false;
      size_t count = std::min(sizeof(buffer), std::min(static_cast<size_t>(available), limit - body.size()));
      int read = stream.read(buffer, count);
      if (read > 0) {
        body.append(reinterpret_cast<const char*>(buffer), read);
        continue;
      }
    } else if (!stream.connected()) {
      // On the ESP8266, available() lets the network stack run when it finds
      // nothing and still returns 0, even if the rest of the reply arrived
      // with the server's close meanwhile. Those bytes are waiting: read them.
      if (stream.available() > 0) continue;
      return length < 0 && !body.empty();
    }
    stream.idle();
  }
}

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
