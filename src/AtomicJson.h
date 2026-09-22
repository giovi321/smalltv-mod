#pragma once
#include <ArduinoJson.h>

// Keep the previous config intact on short writes or failed publication.
// LittleFS rename replaces an existing file atomically. The temporary path
// must be on the same filesystem and owned exclusively by this writer.
template <typename FileSystem>
bool saveJsonAtomically(FileSystem& fs,const JsonDocument& doc,
                        const char* target,const char* temporary) {
  auto file=fs.open(temporary,"w");
  if(!file) return false;
  const size_t expected=measureJson(doc);
  const size_t written=serializeJson(doc,file);
  file.flush();
  bool ok=written==expected && !file.getWriteError() && file.size()==expected;
  file.close();
  if(ok) ok=fs.rename(temporary,target);
  if(!ok) fs.remove(temporary);
  return ok;
}
