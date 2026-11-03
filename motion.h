#pragma once
#include <Arduino.h>
#include <Wire.h>

// AD0 must be tied to GND for 0x68 (or 3V3 for 0x69).
constexpr uint8_t MPU_ADDRESS = 0x68;
bool motionReady = false, motionOk = false;
uint32_t motionLastRead = 0, motionSampleMs = 0, motionSequence = 0;
float acceleration[3] = {};
const char *motionError = "MPU6050 not initialized";

/* Read the MPU's own registers without changing the shared BME280 bus address. */
bool motionRead(uint8_t reg, uint8_t *data, uint8_t count) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU_ADDRESS, count) != count) return false;
  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

/* Check acknowledgement on configuration writes so a disconnected MPU stays an error. */
bool motionWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

/* Wake the MPU and choose a known accelerometer scale before converting raw counts. */
void setupMotion() {
  uint8_t identity = 0;
  motionReady = motionRead(0x75, &identity, 1) && identity == 0x68;
  if (motionReady) {
    // PLL clock, 5 Hz low-pass filtering, internal 10 Hz updates, +/-2 g scale.
    motionReady = motionWrite(0x6B, 0x01) && motionWrite(0x1A, 0x06) &&
                  motionWrite(0x19, 99) && motionWrite(0x1C, 0x00);
  }
  motionError = motionReady ? "Waiting for first MPU6050 reading" : "MPU6050 not found/configuration failed at 0x68; check AD0 and wiring, then reset";
  motionLastRead = millis(); // Allow startup/filter settling before the first read.
  Serial.printf("[mpu] %s\n", motionReady ? "MPU6050 ready at 0x68; acceleration X/Y/Z every 1 s" : motionError);
}

/* Snapshot acceleration once per second without delaying BME280/HTTP/OTA handling. */
void acquireMotion() {
  const uint32_t now = millis();
  if (!motionReady || now - motionLastRead < 1000) return;
  motionLastRead = now;
  uint8_t bytes[6], identity;
  motionOk = motionRead(0x75, &identity, 1) && identity == 0x68 && motionRead(0x3B, bytes, 6);
  ++motionSequence; // Failed samples also advance sequence, making graph gaps explicit.
  motionSampleMs = now;
  motionError = motionOk ? "" : "MPU6050 I2C read failed";
  if (!motionOk) { Serial.printf("[mpu] %s\n", motionError); return; }
  for (unsigned axis = 0; axis < 3; ++axis) {
    // MPU registers are big-endian signed counts; +/-2 g is 16384 counts/g.
    const int16_t raw = int16_t((uint16_t(bytes[2 * axis]) << 8) | bytes[2 * axis + 1]);
    acceleration[axis] = raw * (9.80665f / 16384.0f);
  }
  Serial.printf("[mpu %lu] X=%.3f Y=%.3f Z=%.3f m/s^2 (includes gravity)\n",
                (unsigned long)motionSequence, acceleration[0], acceleration[1], acceleration[2]);
}

/* Expose MPU status independently of BME280 errors; never serialize invalid data as zero. */
void appendMotion(String &json, uint32_t now) {
  const bool valid = motionReady && motionOk && now - motionSampleMs <= 2500;
  json += ",\"motion\":{\"sequence\":" + String(motionSequence) + ",\"sample_age_ms\":" + String(now - motionSampleMs);
  json += ",\"ok\":"; json += valid ? "true" : "false";
  json += ",\"error\":\""; json += motionOk && !valid ? "MPU6050 reading stale" : motionError; json += "\",\"acceleration_m_s2\":[";
  for (unsigned axis = 0; axis < 3; ++axis) {
    if (axis) json += ',';
    json += valid ? String(acceleration[axis], 3) : String("null");
  }
  json += "]}";
}
