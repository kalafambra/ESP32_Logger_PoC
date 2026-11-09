#pragma once

// Main-loop only. OTA callbacks temporarily own loop execution and coordinate
// sensor pause/recovery. The independent pickup LED timer keeps running.
void setupOta();
void serviceOtaConnection(bool connected);
void serviceOta();
