#pragma once
// What the settings look like after a theme was refused for lack of memory
// (ThemeMargin.h). Pure, so the host tests run it (tests/theme/test_margin.cpp).
//
// The property that matters: after a refusal the settings never name the
// refused theme, so no later boot loads it again and is refused again.
#include <cstdint>
#include <string>

namespace smalltv {

// The three settings a refusal may change.
struct Selection {
  std::string themeId;
  uint8_t mode;
  bool carouselTheme;
};

// `current` is what the settings hold now. `hasPrev`, `prevMode` and `prevId`
// are what the select request replaced (the mode and theme before the user
// picked the refused one); `hasPrev` is false when nothing is known, as after
// a boot. Returns true when `current` changed, so the caller saves it.
//
//  - A selection that is no longer the refused theme is left alone: the user
//    picked something else on the way.
//  - A known previous mode comes back, with the previous theme id (it may be
//    the one kept for a later pick). If that id is the refused theme it is
//    cleared.
//  - A previous theme (mode theme, another id) comes back.
//  - Nothing known: the theme id is cleared and the mode stays; the screen
//    then shows the refusal message.
//  - An empty theme id leaves a carousel's theme tick off: a tick with no
//    theme would show "Select an installed theme".
inline bool afterRefusal(Selection& current, bool hasPrev, uint8_t prevMode, const std::string& prevId,
                         const std::string& refusedId, uint8_t themeMode) {
  if (current.themeId != refusedId) return false;
  if (hasPrev && prevMode != themeMode) {
    current.mode = prevMode;
    current.themeId = prevId == refusedId ? std::string() : prevId;
  } else if (hasPrev && !prevId.empty() && prevId != refusedId) {
    current.mode = themeMode;
    current.themeId = prevId;
  } else {
    current.themeId.clear();
  }
  if (current.themeId.empty()) current.carouselTheme = false;
  return true;
}

// Which theme has passed its memory check since the boot, so a later load of
// it is known to be a reload.
//
// A running theme is dropped (release()) for a web request, a save or a
// carousel page change, and loaded again at the next frame through the same
// checks. If that reload is refused, the cause is the heap of this moment, not
// the theme: it passed before. A refusal of a reload leaves the settings alone
// (the theme is tried again at the next invalidate(), or at the next boot).
// A first load (a boot, a new pick, an install or delete of the theme) that is
// refused still goes to afterRefusal().
class PassedTheme {
 public:
  // A load of `id` is about to start: is it a reload of the theme that passed?
  void loadStarts(const char* id) { reload_ = !id_.empty() && id_ == id; }
  bool reload() const { return reload_; }
  // `id` has loaded and passed the check (asked again at every frame after).
  void passed(const char* id) {
    if (id_ != id) id_ = id;
  }
  // A new pick, an install or a delete of the theme, or a refusal of it: the
  // next load is a first load.
  void forget() { id_.clear(); }
  // The settings after a refusal; true when they changed and are to be saved.
  bool refused(Selection& current, bool hasPrev, uint8_t prevMode, const std::string& prevId,
               const std::string& refusedId, uint8_t themeMode) {
    if (reload_) return false;
    if (id_ == refusedId) forget();   // a refusal of another theme leaves the pass of this one
    return afterRefusal(current, hasPrev, prevMode, prevId, refusedId, themeMode);
  }

 private:
  std::string id_;
  bool reload_ = false;
};

}  // namespace smalltv
