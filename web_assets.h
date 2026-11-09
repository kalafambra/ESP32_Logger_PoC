#pragma once
#include <Arduino.h>

// Flash-resident, self-contained pages. send_P() serves these without a
// filesystem or a RAM copy. Definitions live only in web_assets.cpp.
extern const char DASHBOARD_HTML[] PROGMEM;
extern const char PICKUP_TUNING_HTML[] PROGMEM;
extern const char PICKUP_UUID_DEFINITIONS[] PROGMEM;
