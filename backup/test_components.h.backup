#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"

static constexpr const char *SPEAKER_TAG = "OATHBOUND_AUDIO";
static constexpr uint32_t SPEAKER_SAMPLE_RATE = 16000;

inline void test_speaker_idf()
{
    ESP_LOGI(SPEAKER_TAG, "Starting ESP-IDF speaker test");

    // Create an I2S transmit channel.
    i2s_chan_handle_t amp_tx = nullptr;

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    esp_err_t result = i2s_new_channel(&chan_cfg, &amp_tx, nullptr);

    if (result != ESP_OK) {
        ESP_LOGE(SPEAKER_TAG, "Failed to create I2S channel: %s",
                 esp_err_to_name(result));
        return;
    }

    // Configure standard I2S: 16 kHz, 16-bit, stereo.
    i2s_std_config_t std_cfg = {};

    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPEAKER_SAMPLE_RATE);

    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
    );

    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = GPIO_NUM_15;
    std_cfg.gpio_cfg.ws = GPIO_NUM_16;
    std_cfg.gpio_cfg.dout = GPIO_NUM_17;
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags = {};

    result = i2s_channel_init_std_mode(amp_tx, &std_cfg);

    if (result != ESP_OK) {
        ESP_LOGE(SPEAKER_TAG, "I2S configuration failed: %s",
                 esp_err_to_name(result));
        i2s_del_channel(amp_tx);
        return;
    }

    result = i2s_channel_enable(amp_tx);

    // Give the amplifier time to stabilize.
    static int16_t silence[512 * 2] = {};

    for (int i = 0; i < 16; i++) {
        size_t bytes_written = 0;

        i2s_channel_write(
            amp_tx,
            silence,
            sizeof(silence),
            &bytes_written,
            1000
        );
    }

    if (result != ESP_OK) {
        ESP_LOGE(SPEAKER_TAG, "I2S enable failed: %s",
                 esp_err_to_name(result));
        i2s_del_channel(amp_tx);
        return;
    }

    // Generate the same 440 Hz stereo tone as the original test.
    static int16_t samples[512 * 2];
    static uint32_t sample_index = 0;

    constexpr float PI = 3.14159265f;
    constexpr float frequency = 440.0f;
    constexpr float amplitude = 1200.0f;

    for (int i = 0; i < 512; i++) {
        float wave = sinf(
            2.0f * PI * frequency * sample_index / SPEAKER_SAMPLE_RATE
        );

        int16_t sample = static_cast<int16_t>(amplitude * wave);

        samples[i * 2] = sample;
        samples[i * 2 + 1] = sample;

        sample_index++;
    }

    // Send the tone repeatedly.
    for (int i = 0; i < 128; i++) {
        size_t bytes_written = 0;

        result = i2s_channel_write(
            amp_tx,
            samples,
            sizeof(samples),
            &bytes_written,
            1000
        );

        if (result != ESP_OK || bytes_written != sizeof(samples)) {
            ESP_LOGE(SPEAKER_TAG, "Audio write failed");
            break;
        }
    }

    // Send silence before disabling the amplifier.
    std::fill(std::begin(samples), std::end(samples), 0);

    size_t bytes_written = 0;

    i2s_channel_write(
        amp_tx,
        samples,
        sizeof(samples),
        &bytes_written,
        1000
    );

    i2s_channel_disable(amp_tx);
    i2s_del_channel(amp_tx);

    ESP_LOGI(SPEAKER_TAG, "Speaker test finished");
}


inline void test_microphone_idf()
{
    ESP_LOGI("OATHBOUND_MIC", "Starting INMP441 microphone test");

    i2s_chan_handle_t mic_rx = nullptr;

    // Use I2S peripheral 1 so it doesn't conflict with the speaker.
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);

    esp_err_t result = i2s_new_channel(&chan_cfg, nullptr, &mic_rx);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_MIC", "Channel creation failed: %s",
                 esp_err_to_name(result));
        return;
    }

    // INMP441: 16 kHz, 32-bit slots, stereo.
    i2s_std_config_t std_cfg = {};

    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);

    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    );

    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = GPIO_NUM_4;
    std_cfg.gpio_cfg.ws = GPIO_NUM_5;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = GPIO_NUM_6;
    std_cfg.gpio_cfg.invert_flags = {};

    result = i2s_channel_init_std_mode(mic_rx, &std_cfg);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_MIC", "I2S configuration failed: %s",
                 esp_err_to_name(result));
        i2s_del_channel(mic_rx);
        return;
    }

    result = i2s_channel_enable(mic_rx);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_MIC", "I2S enable failed: %s",
                 esp_err_to_name(result));
        i2s_del_channel(mic_rx);
        return;
    }

    ESP_LOGI("OATHBOUND_MIC", "Listening for approximately 3 seconds...");
    ESP_LOGI("OATHBOUND_MIC", "Try speaking or clapping near the microphone.");

    int32_t samples[512 * 2];
    float noise_floor = 0.0f;
    bool noise_floor_initialized = false;

    for (int batch = 0; batch < 90; batch++) {
        size_t bytes_read = 0;

        result = i2s_channel_read(
            mic_rx,
            samples,
            sizeof(samples),
            &bytes_read,
            1000
        );

        if (result != ESP_OK || bytes_read == 0) {
            ESP_LOGE("OATHBOUND_MIC", "Audio read failed: %s",
                     esp_err_to_name(result));
            break;
        }

        // Calculate how many int32_t samples were received.
        int count = bytes_read / sizeof(int32_t);

        int64_t sum_squares = 0;
        int32_t peak = 0;
        int samples_count = 0;

        // Process the left channel only.
        for (int i = 0; i < count; i += 2) {

            // Convert the microphone sample to 16-bit audio.
            int16_t sample = static_cast<int16_t>(samples[i] >> 16);

            int32_t magnitude = abs(static_cast<int32_t>(sample));

            if (magnitude > peak)
                peak = magnitude;

            sum_squares += static_cast<int64_t>(sample) * sample;
            samples_count++;
        }

        // Avoid division by zero.
        if (samples_count == 0)
            continue;

        // Calculate RMS volume.
        float rms = sqrt(
            static_cast<double>(sum_squares) / samples_count
        );

        if (batch < 30) {
            ESP_LOGI("OATHBOUND_MIC", "Warming up | RMS: %.1f", rms);
            continue;
        }

        if (!noise_floor_initialized) {
            noise_floor = rms;
            noise_floor_initialized = true;
        } else {
            // Use different adaptation speeds depending on the direction.
            float alpha;

            if (rms > noise_floor) {
                // Background noise increasing.
                alpha = 0.02f;
            }
            else {
                // Background noise decreasing.
                alpha = 0.10f;
            }

            noise_floor += alpha * (rms - noise_floor);
        }
        float speech_threshold = noise_floor * 2.5f;

        if (speech_threshold < 3000.0f) {
            speech_threshold = 3000.0f;
        }
        ESP_LOGI(
            "OATHBOUND_MIC",
            "RMS: %.1f | Noise Floor: %.1f | Threshold: %.1f | Peak: %.ld",
            rms,
            noise_floor,
            speech_threshold,
            (long)peak
        );
    }

    // Clean up the microphone I2S channel.
    i2s_channel_disable(mic_rx);
    i2s_del_channel(mic_rx);

    ESP_LOGI("OATHBOUND_MIC", "Microphone test finished");
}


inline void record_and_play_audio()
{
    constexpr int SAMPLE_RATE = 16000;
    constexpr int RECORD_SECONDS = 3;
    constexpr int TOTAL_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;

    ESP_LOGI("OATHBOUND_AUDIO", "Starting recording...");

    // ------------------------------------
    // 1. Initialize microphone (I2S 1)
    // ------------------------------------

    i2s_chan_handle_t mic_rx = nullptr;

    i2s_chan_config_t mic_chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);

    esp_err_t result = i2s_new_channel(
        &mic_chan_cfg, nullptr, &mic_rx
    );

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_AUDIO", "Microphone channel creation failed");
        return;
    }

    i2s_std_config_t mic_cfg = {};

    mic_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);

    mic_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    );

    mic_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    mic_cfg.gpio_cfg.bclk = GPIO_NUM_4;
    mic_cfg.gpio_cfg.ws = GPIO_NUM_5;
    mic_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    mic_cfg.gpio_cfg.din = GPIO_NUM_6;
    mic_cfg.gpio_cfg.invert_flags = {};

    result = i2s_channel_init_std_mode(mic_rx, &mic_cfg);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_AUDIO", "Microphone initialization failed");
        i2s_del_channel(mic_rx);
        return;
    }

    result = i2s_channel_enable(mic_rx);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_AUDIO", "Microphone enable failed");
        i2s_del_channel(mic_rx);
        return;
    }

    // Store 3 seconds of 16-bit mono audio.
    std::vector<int16_t> recording;

    recording.reserve(TOTAL_SAMPLES);

    int32_t input_samples[512 * 2];

    while (recording.size() < TOTAL_SAMPLES) {
        size_t bytes_read = 0;

        result = i2s_channel_read(
            mic_rx,
            input_samples,
            sizeof(input_samples),
            &bytes_read,
            1000
        );

        if (result != ESP_OK || bytes_read == 0) {
            ESP_LOGE("OATHBOUND_AUDIO", "Microphone read failed");
            break;
        }

        int count = bytes_read / sizeof(int32_t);

        // L/R is grounded, so use the left channel.
        for (int i = 0; i < count; i += 2) {
            if (recording.size() >= TOTAL_SAMPLES) {
                break;
            }

            // Convert the 24-bit microphone sample to 16-bit PCM.
            int16_t sample = static_cast<int16_t>(
                input_samples[i] >> 16
            );

            recording.push_back(sample);
        }
    }

    i2s_channel_disable(mic_rx);
    i2s_del_channel(mic_rx);

    if (recording.size() != TOTAL_SAMPLES) {
        ESP_LOGE("OATHBOUND_AUDIO", "Recording incomplete");
        return;
    }

    ESP_LOGI("OATHBOUND_AUDIO", "Recording complete: %u samples",
             static_cast<unsigned>(recording.size()));

    // ------------------------------------
    // 2. Initialize speaker (I2S 0)
    // ------------------------------------

    ESP_LOGI("OATHBOUND_AUDIO", "Playing recording...");

    i2s_chan_handle_t amp_tx = nullptr;

    i2s_chan_config_t amp_chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    result = i2s_new_channel(&amp_chan_cfg, &amp_tx, nullptr);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_AUDIO", "Speaker channel creation failed");
        return;
    }

    i2s_std_config_t amp_cfg = {};

    amp_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);

    amp_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
    );

    amp_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    amp_cfg.gpio_cfg.bclk = GPIO_NUM_15;
    amp_cfg.gpio_cfg.ws = GPIO_NUM_16;
    amp_cfg.gpio_cfg.dout = GPIO_NUM_17;
    amp_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    amp_cfg.gpio_cfg.invert_flags = {};

    result = i2s_channel_init_std_mode(amp_tx, &amp_cfg);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_AUDIO", "Speaker initialization failed");
        i2s_del_channel(amp_tx);
        return;
    }

    result = i2s_channel_enable(amp_tx);

    if (result != ESP_OK) {
        ESP_LOGE("OATHBOUND_AUDIO", "Speaker enable failed");
        i2s_del_channel(amp_tx);
        return;
    }

    // ------------------------------------
    // 3. Play recorded audio
    // ------------------------------------

    int16_t output_samples[512 * 2];

    size_t position = 0;

    while (position < recording.size()) {
        size_t frames = std::min(
            static_cast<size_t>(512),
            recording.size() - position
        );

        for (size_t i = 0; i < frames; i++) {
            int16_t sample = recording[position + i];

            // Duplicate mono audio into left and right channels.
            output_samples[i * 2] = sample;
            output_samples[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;

        result = i2s_channel_write(
            amp_tx,
            output_samples,
            frames * 2 * sizeof(int16_t),
            &bytes_written,
            1000
        );

        if (result != ESP_OK) {
            ESP_LOGE("OATHBOUND_AUDIO", "Speaker playback failed");
            break;
        }

        position += frames;
    }

    i2s_channel_disable(amp_tx);
    i2s_del_channel(amp_tx);

    ESP_LOGI("OATHBOUND_AUDIO", "Recording playback finished");
}


inline void record_and_play_audio_task(void *parameter){
    record_and_play_audio();
    //test_microphone_idf();
    vTaskDelete(nullptr);
}