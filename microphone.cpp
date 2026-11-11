#include "microphone.h"
#include <Arduino.h>
#include <driver/i2s_std.h>
#include "config.h"

namespace {

i2s_chan_handle_t microphoneRx = nullptr;
bool microphoneReady = false, microphonePaused = false;
esp_err_t microphoneError = ESP_OK;
uint32_t microphoneSamples = 0;
volatile uint32_t microphoneOverflows = 0;
bool IRAM_ATTR microphoneOverflowISR(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  ++microphoneOverflows;
  return false;
}
int32_t microphoneBlock[MICROPHONE_BLOCK_SAMPLES];
size_t microphoneBlockCount = 0;

} // namespace

void stopMicrophone() {
  if (microphoneRx) {
    if (microphoneReady && !microphonePaused)
      i2s_channel_disable(microphoneRx);
    i2s_del_channel(microphoneRx);
    microphoneRx = nullptr;
  }
  microphoneReady = false;
  microphonePaused = false;
  microphoneBlockCount = 0;
}

bool setupMicrophone() {
  if (!MICROPHONE_ENABLED) {
    Serial.println("[microphone] Disabled in config.h; I2S pins and DMA inactive");
    return false;
  }
  if (microphoneReady)
    return true;
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = MICROPHONE_DMA_BUFFERS;
  channel.dma_frame_num =
      MICROPHONE_BLOCK_SAMPLES; // 16 ms per buffer; six buffers cover 96 ms at 16 kHz.
  microphoneError = i2s_new_channel(&channel, nullptr, &microphoneRx); // RX only.
  if (microphoneError == ESP_OK) {
    i2s_std_config_t config = {};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MICROPHONE_SAMPLE_RATE_HZ);
    config.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk = static_cast<gpio_num_t>(MICROPHONE_BCLK_PIN);
    config.gpio_cfg.ws = static_cast<gpio_num_t>(MICROPHONE_WS_PIN);
    config.gpio_cfg.dout = I2S_GPIO_UNUSED;
    config.gpio_cfg.din = static_cast<gpio_num_t>(MICROPHONE_DATA_PIN);
    microphoneError = i2s_channel_init_std_mode(microphoneRx, &config);
  }
  if (microphoneError == ESP_OK) {
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_recv_q_ovf = microphoneOverflowISR;
    microphoneError = i2s_channel_register_event_callback(microphoneRx, &callbacks, nullptr);
  }
  if (microphoneError == ESP_OK)
    microphoneError = i2s_channel_enable(microphoneRx);
  if (microphoneError != ESP_OK) {
    Serial.printf("[microphone] Initialization failed: %s\n", esp_err_to_name(microphoneError));
    stopMicrophone(); // Allocation failure must not prevent other sensors from running.
    return false;
  }
  microphoneReady = true;
  Serial.printf("[microphone] INMP441: %lu Hz left mono, BCLK=%u WS=%u DIN=%u\n",
                MICROPHONE_SAMPLE_RATE_HZ, MICROPHONE_BCLK_PIN, MICROPHONE_WS_PIN,
                MICROPHONE_DATA_PIN);
  return true;
}

void pauseMicrophone() {
  if (!microphoneReady || microphonePaused)
    return;
  microphoneError = i2s_channel_disable(microphoneRx);
  if (microphoneError == ESP_OK)
    microphonePaused = true;
  microphoneBlockCount = 0;
}

void resumeMicrophone() {
  if (!microphoneReady || !microphonePaused)
    return;
  microphoneError = i2s_channel_enable(microphoneRx);
  if (microphoneError == ESP_OK)
    microphonePaused = false;
  else
    Serial.printf("[microphone] Resume failed: %s\n", esp_err_to_name(microphoneError));
}

// One bounded, nonblocking read per call. The drain service consumes the returned
// block before requesting another. DMA timeouts can still return partial audio.
AudioBlock serviceMicrophone() {
  microphoneBlockCount = 0;
  if (!microphoneReady || microphonePaused)
    return {microphoneBlock, 0};
  size_t bytes = 0;
  microphoneError =
      i2s_channel_read(microphoneRx, microphoneBlock, sizeof(microphoneBlock), &bytes, 0);
  if (microphoneError != ESP_OK && microphoneError != ESP_ERR_TIMEOUT)
    return {microphoneBlock, 0};
  // A timeout may still deliver a partial block. Remove the unused low eight bits.
  microphoneBlockCount = bytes / sizeof(microphoneBlock[0]);
  for (size_t i = 0; i < microphoneBlockCount; ++i)
    microphoneBlock[i] >>= 8;
  microphoneSamples += microphoneBlockCount;
  return {microphoneBlock, microphoneBlockCount};
}

MicrophoneStatus microphoneStatus() {
  return {microphoneReady, microphonePaused, microphoneError, microphoneSamples,
          microphoneOverflows};
}
