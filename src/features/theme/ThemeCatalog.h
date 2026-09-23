#pragma once
#include "ThemePackage.h"

namespace smalltv {
// One row of GET /api/themes: metadata for an installed package regardless
// of whether it loads successfully -- an invalid package still gets an `id`,
// `bytes`, and `error` so the web UI can list and offer to remove it.
struct InstalledTheme {
  std::string id, name, author, version, error;
  uint32_t bytes = 0;
  bool valid = false;
};
// Identity always comes from the installed filename, even when the manifest is
// unreadable or has a conflicting ID. Such packages remain visible/removable.
inline InstalledTheme inspectInstalledTheme(Source& source, const std::string& id) {
  InstalledTheme entry;
  entry.id = entry.name = id;
  entry.bytes = source.size();
  if (!validId(id)) {
    entry.error = "Invalid installed filename";
    return entry;
  }
  Package package(source);
  Theme theme;
  if (!package.load(theme, entry.error)) return entry;
  if (theme.id != id) {
    entry.error = "Theme ID does not match installed filename";
    return entry;
  }
  entry.valid = true;
  entry.name = theme.name;
  entry.author = theme.author;
  entry.version = theme.version;
  return entry;
}
}
