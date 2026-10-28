#pragma once
#include <stdint.h>

// A settings write that waits until the requests stop (a dragged slider sends
// many), then runs. A write that fails stays pending and is tried again after
// `retryMs`, so it is never dropped; a new request restarts the quiet wait.
class DebouncedSave {
 public:
  DebouncedSave(uint32_t quietMs, uint32_t retryMs) : quietMs_(quietMs), retryMs_(retryMs) {}

  void request(uint32_t now) {
    at_ = now + quietMs_;
    due_ = true;
  }
  bool pending() const { return due_; }

  template <typename Save>
  void service(uint32_t now, Save save) {
    if (!due_ || (int32_t)(now - at_) < 0) return;
    if (save()) due_ = false;
    else at_ = now + retryMs_;
  }

 private:
  uint32_t quietMs_;
  uint32_t retryMs_;
  uint32_t at_ = 0;
  bool due_ = false;
};
