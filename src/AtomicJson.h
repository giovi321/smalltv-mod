#pragma once
#include <ArduinoJson.h>

// Keep the previous config intact on short writes or failed publication.
// LittleFS rename replaces an existing file atomically. The temporary path
// must be on the same filesystem and owned exclusively by this writer.
template <typename FileSystem>
bool saveJsonAtomically(FileSystem& fs, const JsonDocument& doc,
                         const char* target, const char* temporary) {
  // A document that ran out of memory while it was built is valid JSON of
  // only part of the data. Saving it would replace the working config with
  // that part, and the next boot would use defaults for the rest.
  if (doc.overflowed()) return false;
  auto file = fs.open(temporary, "w");
  if (!file) return false;
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  file.flush();
  // Checked three ways because any one alone can miss a short write: the
  // returned count, the deferred write error a buffered FS surfaces only
  // after flush(), and the file's own final size once closed.
  bool ok = written == expected && !file.getWriteError() && file.size() == expected;
  file.close();
  if (ok) ok = fs.rename(temporary, target);
  // A failed rename or a short write must not leave the half-written
  // temporary file behind to be mistaken for a real save next boot.
  if (!ok) fs.remove(temporary);
  return ok;
}
