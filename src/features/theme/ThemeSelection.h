#pragma once
// When a theme the user picked is saved (issue #132). Pure, so the host tests
// run it (tests/theme/test_margin.cpp).
//
// A pick in the web UI used to be written to flash in the select request,
// before the theme had ever loaded. A theme that then crashed the device came
// back at the next boot, because the setting named it, and crashed it again.
// Now the pick is the live selection at once (the screen, /api/themes and
// /api/config show it) and is written to flash only after the theme has
// loaded and passed the memory check. A crash during the first load leaves the
// old setting in flash; a refusal never writes the refused theme.
//
// What this costs: a restart within the first seconds of a load (a power cut,
// an update) brings back the old theme, not the new one; and a settings save
// of something else in those seconds writes the new theme with it (the live
// settings are what any save writes).
#include <string>

namespace smalltv {

class PendingSelection {
 public:
  // The select request: this theme is picked and not yet saved. An empty id is
  // no pick; a second pick replaces the first.
  void pick(const std::string& id) { id_ = id; }
  // The theme was refused (memory): it is never saved.
  void drop() { id_.clear(); }
  bool pending() const { return !id_.empty(); }
  // Asked once per frame while a pick waits. `selectedNow` is the theme the
  // live settings name (a C string: nothing is copied, as this runs while the
  // heap is what is being watched), `loadPassed` whether it has loaded and
  // passed the memory check. True once, when the caller must save the
  // settings. If the live selection is another theme than the pick (the user
  // chose again on the way), the pick is forgotten without a save.
  bool accept(const char* selectedNow, bool loadPassed) {
    if (id_.empty()) return false;
    if (selectedNow != id_) {
      id_.clear();
      return false;
    }
    if (!loadPassed) return false;
    id_.clear();
    return true;
  }

 private:
  std::string id_;
};

}  // namespace smalltv
