#pragma once
#include "config.h"
#if WITH_THEME
#include "Platform.h"
#include "Settings.h"
void themeWebBegin(WebServerClass& server,Settings& settings,bool (*requireAuth)());
#endif
