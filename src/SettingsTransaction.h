#pragma once

// Apply a mutation atomically from the caller's perspective. A failed
// persistent write restores the exact live settings that preceded it.
template <typename SettingsT, typename Apply, typename Save>
bool applyAndSaveSettings(SettingsT& settings, Apply apply, Save save) {
  SettingsT previous=settings;
  apply(settings);
  if(save(settings)) return true;
  settings=previous;
  return false;
}
