#pragma once
#include "config.h"
#if WITH_THEME
#include "ThemeData.h"

// Starts one self-contained worker. The caller keeps at most one request alive.
bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request);
#endif
