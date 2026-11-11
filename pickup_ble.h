#pragma once

// Disabled stubs are linked unless PICKUP_BLE_ENABLED=1 (BUILD_VARIANTS.md).
// Call from setup/loop only. Optional BLE starts after Wi-Fi/HTTP/OTA have
// reserved memory. Callbacks read locked event snapshots and request restart;
// advertising recovery runs on loop(), outside the BLE task.
bool setupPickupBle();
void servicePickupBle();
bool isPickupBleReady();
