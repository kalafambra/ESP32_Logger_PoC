#pragma once
#include <stdint.h>

// Reserve Wi-Fi memory before microphone/BLE initialization. Connection and
// retry progress is cooperative; Wi-Fi callbacks only log on their own task.
void setupNetwork();
void serviceNetwork(uint32_t now); // Also starts HTTP, NTP, OTA and optional BLE.
