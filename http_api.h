#pragma once

// Owns the sole WebServer and all routes/JSON responses. Register routes during
// setup; start once the Wi-Fi driver has an IP. Service only from loop().
void setupHttp();
void startHttp();   // Idempotent; no socket is created before network readiness.
void serviceHttp(); // Includes the one-second idle-client deadline.
