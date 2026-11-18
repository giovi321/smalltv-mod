// BuildInfo.h — build options for forks. They are build flags, not settings:
// there is no runtime switch for either of them.
//
// FW_VERSION (config.h) is the release number. It is never changed here: the
// release workflow edits that one line, and the update check compares it. A
// fork that wants a suffix on the label it shows, or the GitHub update off,
// says so with build flags (platformio.ini or PLATFORMIO_BUILD_FLAGS):
//
//   -D FW_VERSION_SUFFIX=\"-fork\"   FW_VERSION_FULL is "3.7.0-fork"
//   -D FW_NO_GITHUB_UPDATE           FW_GITHUB_UPDATE is 0
//
// With no flag the label is the release number and the update stays on, so
// every other image builds as before.
#pragma once
#ifndef FW_VERSION
#include "config.h"
#endif

#ifndef FW_VERSION_SUFFIX
#define FW_VERSION_SUFFIX ""
#endif

// The label to show people: the status reply, the serial line, the boot
// screen and the mDNS record. It is for display only. Where the release
// number is compared or stored (the update check, the crash record's 16 byte
// field) use FW_VERSION.
#define FW_VERSION_FULL FW_VERSION FW_VERSION_SUFFIX

#ifdef FW_NO_GITHUB_UPDATE
#define FW_GITHUB_UPDATE 0
#else
#define FW_GITHUB_UPDATE 1
#endif

// What the web UI is told when it asks about updates in a build that has them
// off. The check answers 200 with "disabled", the update request answers 403.
#define FW_UPDATE_OFF_MSG "updates are off in this build"
#define FW_UPDATE_OFF_CHECK_REPLY \
  "{\"current\":\"" FW_VERSION "\",\"ok\":false,\"latest\":\"\",\"newer\":false," \
  "\"disabled\":true,\"error\":\"" FW_UPDATE_OFF_MSG "\"}"
#define FW_UPDATE_OFF_SELF_REPLY "{\"ok\":false,\"error\":\"" FW_UPDATE_OFF_MSG "\"}"
