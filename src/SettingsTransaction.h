#pragma once

// Apply a mutation atomically from the caller's perspective. A failed
// persistent write restores the exact live settings that preceded it, so a
// route that reports failure never leaves the in-memory `settings` object
// disagreeing with what is actually on disk.
template <typename SettingsT, typename Apply, typename Save>
bool applyAndSaveSettings(SettingsT& settings, Apply apply, Save save) {
  SettingsT previous = settings;
  apply(settings);
  if (save(settings)) return true;
  settings = previous;
  return false;
}
