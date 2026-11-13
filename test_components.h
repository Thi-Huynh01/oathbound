#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>
#include <atomic>
#include <cstring>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"

namespace oathbound_recording {

    constexpr size_t SAMPLE_RATE = 16000;
    constexpr size_t RECORD_SECONDS = 10;
    constexpr size_t BUFFER_SIZE = SAMPLE_RATE * RECORD_SECONDS * sizeof(int32_t);
    
    inline size_t wav_size = 0;
    inline uint8_t* buffer = nullptr;
    inline std::atomic<size_t> bytes_written{0};
    inline std::atomic<bool> recording_active{false};

    // allocate the recording buffer in PSRAM and prepare for recording
    inline bool begin() {
        recording_active.store(false);

        if (buffer == nullptr) {
            buffer = static_cast<uint8_t*>(
                heap_caps_malloc(
                    BUFFER_SIZE,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                )
            );
        }
        if (buffer == nullptr) {
            ESP_LOGE("OATHBOUND RECORDING", "PSRAM allocation failed");
            return false;
        }

        bytes_written.store(0);
        recording_active.store(true);
        
        ESP_LOGI(
            "OATHBOUND_RECORDING",
            "Recording buffer read: %u bytes",
            static_cast<unsigned>(BUFFER_SIZE)
        );
        
        return true;
    }

    inline void append(const std::vector<uint8_t>& data) {
        if (!recording_active.load()) {
            return;
        }

        size_t offset = bytes_written.load();

        if (offset >= BUFFER_SIZE) {
            recording_active.store(false);
            return;
        }

        size_t amount = std::min(data.size(), BUFFER_SIZE - offset);

        memcpy(buffer + offset, data.data(), amount);

        bytes_written.store(offset + amount);

        if (offset + amount >= BUFFER_SIZE) {
            recording_active.store(false);
        }
    }
    // stop accepting audio and return the number of bytes captured
    inline size_t finish() {
        recording_active.store(false);
        return bytes_written.load();
    }

    inline size_t convert_to_pcm16(size_t raw_bytes) {
        if (buffer == nullptr || raw_bytes == 0) {
            return 0;
        }

        if (raw_bytes % sizeof(int32_t) != 0) {
            ESP_LOGE(
                "OATHBOUND_RECORDING",
                "Invalid raw audio size: %u",
                (unsigned) raw_bytes
            );
            return 0;
        }

        size_t sample_count = raw_bytes / sizeof(int32_t);

        int32_t raw_sample;
        int16_t pcm_sample;

        int32_t peak = 0;
        uint64_t sum_squares = 0;

        for (size_t i = 0; i < sample_count; i++) {

            // Read the original 32-bit microphone sample.
            memcpy(
                &raw_sample,
                buffer + (i * sizeof(int32_t)),
                sizeof(int32_t)
            );

            // Convert the 24-bit microphone data to 16-bit PCM.
            pcm_sample = static_cast<int16_t>(raw_sample >> 16);

            // Write the smaller sample back into the same buffer.
            memcpy(
                buffer + (i * sizeof(int16_t)),
                &pcm_sample,
                sizeof(int16_t)
            );

            int32_t magnitude = pcm_sample < 0
                ? -static_cast<int32_t>(pcm_sample)
                : static_cast<int32_t>(pcm_sample);

            if (magnitude > peak) {
                peak = magnitude;
            }

            sum_squares +=
                static_cast<int64_t>(pcm_sample) * pcm_sample;
        }

        float rms = sqrt(
            static_cast<double>(sum_squares) / sample_count
        );

        size_t pcm_bytes = sample_count * sizeof(int16_t);

        ESP_LOGI(
            "OATHBOUND_RECORDING",
            "PCM conversion complete: %u samples, %u bytes, peak: %ld, RMS: %.1f",
            (unsigned) sample_count,
            (unsigned) pcm_bytes,
            (long) peak,
            rms
        );

        return pcm_bytes;
    }
    
    inline size_t package_wav(size_t pcm_bytes) {
        if (buffer == nullptr || pcm_bytes == 0 ||
            pcm_bytes > BUFFER_SIZE - 44) {
            ESP_LOGE("OATHBOUND_RECORDING", "Invalid PCM size for WAV");
            return 0;
        }

        // Move PCM data forward to make room for the 44-byte WAV header.
        memmove(buffer + 44, buffer, pcm_bytes);

        // WAV header identifiers.
        memcpy(buffer, "RIFF", 4);

        auto write_u16 = [](size_t offset, uint16_t value) {
            buffer[offset] = value & 0xFF;
            buffer[offset + 1] = (value >> 8) & 0xFF;
        };

        auto write_u32 = [](size_t offset, uint32_t value) {
            buffer[offset] = value & 0xFF;
            buffer[offset + 1] = (value >> 8) & 0xFF;
            buffer[offset + 2] = (value >> 16) & 0xFF;
            buffer[offset + 3] = (value >> 24) & 0xFF;
        };

        write_u32(4, 36 + pcm_bytes);
        memcpy(buffer + 8, "WAVE", 4);

        // Format chunk.
        memcpy(buffer + 12, "fmt ", 4);
        write_u32(16, 16);          // PCM format chunk size
        write_u16(20, 1);           // Audio format: PCM
        write_u16(22, 1);           // Mono
        write_u32(24, SAMPLE_RATE); // 16,000 Hz
        write_u32(28, SAMPLE_RATE * 2); // Byte rate
        write_u16(32, 2);           // Block alignment
        write_u16(34, 16);          // Bits per sample

        // Data chunk.
        memcpy(buffer + 36, "data", 4);
        write_u32(40, pcm_bytes);

        size_t wav_bytes = 44 + pcm_bytes;

        ESP_LOGI(
            "OATHBOUND_RECORDING",
            "WAV packaging complete: %u bytes",
            (unsigned) wav_bytes
        );

        return wav_bytes;
    }
    
    inline esp_err_t download_handler(httpd_req_t *req) {
        if (buffer == nullptr || wav_size == 0) {
            httpd_resp_send_err(
                req,
                HTTPD_404_NOT_FOUND,
                "No recording available"
            );
            return ESP_OK;
        }

        httpd_resp_set_type(req, "audio/wav");

        httpd_resp_set_hdr(
            req,
            "Content-Disposition",
            "attachment; filename=recording.wav"
        );

        return httpd_resp_send(
            req,
            reinterpret_cast<const char*>(buffer),
            wav_size
        );
    }

    inline void start_http_server() {
        static httpd_handle_t server = nullptr;

        if (server != nullptr) {
            return;
        }

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();

        if (httpd_start(&server, &config) != ESP_OK) {
            ESP_LOGE(
                "OATHBOUND_HTTP",
                "Failed to start HTTP server"
            );
            return;
        }

        httpd_uri_t uri = {};
        uri.uri = "/recording.wav";
        uri.method = HTTP_GET;
        uri.handler = download_handler;
        uri.user_ctx = nullptr;

        httpd_register_uri_handler(server, &uri);

        ESP_LOGI(
            "OATHBOUND_HTTP",
            "WAV download endpoint started"
        );
    }
}