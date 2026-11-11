#pragma once

// BLE-free is the default. scripts/compile_firmware.py --ble overrides this
// for a no_fs build. For Arduino IDE restoration, see BUILD_VARIANTS.md.
#ifndef PICKUP_BLE_ENABLED
#define PICKUP_BLE_ENABLED 0
#endif

#if PICKUP_BLE_ENABLED != 0 && PICKUP_BLE_ENABLED != 1
#error "PICKUP_BLE_ENABLED must be 0 or 1"
#endif
