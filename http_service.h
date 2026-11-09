#pragma once
#include <WebServer.h>

// WebServer's default five-second wait for an idle accepted socket can block
// all other clients after an aborted poll or speculative browser connection.
// Request parsing/body delivery remain owned by WebServer; only sockets that
// have supplied no bytes to its cooperative handler are timed out here.
void serviceHttpClients(WebServer &http) {
  static bool waiting = false;
  static uint32_t waitingSince = 0;
  http.handleClient();
  auto client = http.client();
  if (!client || !client.connected() || client.available()) {
    waiting = false;
    return;
  }
  const uint32_t now = millis();
  if (!waiting) { waiting = true; waitingSince = now; }
  else if (now - waitingSince >= 1000) {
    client.stop();
    waiting = false;
  }
}
