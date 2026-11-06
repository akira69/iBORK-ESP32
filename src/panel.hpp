#pragma once
#if defined(IBORK_DISPLAY_CROWPANEL_V3)
#include "displays/crowpanel_v3.hpp"
static_assert(display_hw::widthPixels == 800 && display_hw::heightPixels == 480,
              "The dashboard requires an 800 x 480 landscape display");
#else
#error "Select a supported iBORK display target in platformio.ini"
#endif
