// src/OtaUpdate.cpp built with -DFW_NO_GITHUB_UPDATE, on the host with the
// fuzz target's stand-in headers. A build with the update off carries none of
// the download code: every entry point answers "no" without touching a
// network, a file or the heap. tests/buildflags/run.sh also looks into the
// object file for the download code.
#include "OtaUpdate.h"
#include <cassert>
#include <cstdio>
#include <cstring>

#ifndef FW_NO_GITHUB_UPDATE
#error "this test is built with -DFW_NO_GITHUB_UPDATE"
#endif

int main() {
  Settings s;
  const char* off = "updates are off in this build";

  OtaLatest r = otaCheckLatest(s);
  assert(!r.ok);
  assert(!r.newer);
  assert(r.tag.length() == 0 && r.url.length() == 0);
  assert(std::strcmp(r.error.c_str(), off) == 0);

  assert(std::strcmp(otaUpdateFromGitHub(s).c_str(), off) == 0);

  // The boot-time update never runs and nothing is queued.
  assert(!otaBootRequested());
  assert(!otaRequestBootUpdate("v9.9.9"));
  assert(!otaRequestBootUpdate(nullptr));
  otaBootUpdate(s);
  assert(otaTakeBootResult().length() == 0);

  std::printf("update-off tests passed\n");
  return 0;
}
