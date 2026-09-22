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
      return length < 0 && !body.empty();
    }
    stream.idle();
  }
}

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

// The worker owns a shared reference, so changing/unloading a theme never frees
// its request underneath it. Only the display loop consumes completed values.
class DataRequests {
 public:
  ~DataRequests() { cancel(); }
  bool busy() const { return bool(pending_); }
  std::shared_ptr<DataFetch> start(const ThemeDataSource& source, size_t index) {
    if (pending_) return {};
    pending_ = std::make_shared<DataFetch>(source, index);
    return pending_;
  }
  void cancel() { if (pending_) pending_->cancel(); }
  std::shared_ptr<DataFetch> take() {
    if (!pending_ || !pending_->finished()) return {};
    auto result = std::move(pending_);
    return result->cancelled() ? std::shared_ptr<DataFetch>{} : result;
  }
 private:
  std::shared_ptr<DataFetch> pending_;
};
}
