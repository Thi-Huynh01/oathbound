#pragma once

#include "esphome.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include "micro_mp3/mp3_decoder.h"

#include <cstring>
#include <string>

namespace oathbound_audio {

    class OathboundAudio : public esphome::Component {
        public:
        void setup() override {
            ESP_LOGI(TAG, "Oathbound audio component initialized");

            set_timeout(10000, [this]() {
            ESP_LOGI(TAG, "Starting test audio download...");
            download();
            });
        }

        void download() {
            xTaskCreate(
                download_task,
                "audio_download",
                8192,
                this,
                5,
                nullptr
            );
        }

        private:
        static constexpr const char *TAG = "oathbound_audio";
        i2s_chan_handle_t speaker_tx = nullptr;

        bool initialize_speaker(uint32_t sample_rate = 16000) {
            ESP_LOGI(TAG, "Initializing MAX98357A speaker...");

            i2s_chan_config_t channel_config =
                I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);

            channel_config.dma_desc_num = 8;
            channel_config.dma_frame_num = 256;

            esp_err_t result = i2s_new_channel(
                &channel_config,
                &speaker_tx,
                nullptr
            );

            if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2S channel: %s",
                    esp_err_to_name(result));
            return false;
            }

            i2s_std_config_t standard_config = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_MONO
            ),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = GPIO_NUM_15,
                .ws = GPIO_NUM_16,
                .dout = GPIO_NUM_17,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
                }
            }
            };

            result = i2s_channel_init_std_mode(
                speaker_tx,
                &standard_config
            );

            if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize speaker I2S: %s",
                    esp_err_to_name(result));
            i2s_del_channel(speaker_tx);
            speaker_tx = nullptr;
            return false;
            }

            result = i2s_channel_enable(speaker_tx);

            if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable speaker I2S: %s",
                    esp_err_to_name(result));
            i2s_del_channel(speaker_tx);
            speaker_tx = nullptr;
            return false;
            }

            ESP_LOGI(TAG, "MAX98357A I2S initialized successfully");
            return true;
        }

        struct DownloadContext {
            std::string body;
            size_t bytes_received = 0;
            bool capture_body = false;

            uint8_t *buffer = nullptr;
            size_t capacity = 0;
            bool overflow = false;
        };

        static esp_err_t http_event_handler(esp_http_client_event_t *event) {
            auto *context =
                static_cast<DownloadContext *>(event->user_data);

            if (event->event_id == HTTP_EVENT_ON_DATA) {
                if (context->capture_body) {
                    context->body.append(
                        static_cast<char *>(event->data),
                        event->data_len
                    );
                } else if (context->buffer != nullptr) {
                    if (context->bytes_received + event->data_len <= context->capacity) {
                    memcpy(
                        context->buffer + context->bytes_received,
                        event->data,
                        event->data_len
                    );

                    context->bytes_received += event->data_len;
                    } else {
                    context->overflow = true;
                    }
                }
            }

            return ESP_OK;
        }

        uint16_t read_u16(const uint8_t *data) {
            return data[0] | (data[1] << 8);
        }

        uint32_t read_u32(const uint8_t *data) {
            return (uint32_t)data[0] |
                ((uint32_t)data[1] << 8) |
                ((uint32_t)data[2] << 16) |
                ((uint32_t)data[3] << 24);
        }

        bool play_wav(const uint8_t *wav, size_t length) {
            if (length < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
                ESP_LOGE(TAG, "Invalid WAV file");
                return false;
            }

            uint16_t channels = 0;
            uint16_t bits_per_sample = 0;
            uint16_t audio_format = 0;
            uint32_t sample_rate = 0;

            const uint8_t *pcm_data = nullptr;
            size_t pcm_size = 0;

            size_t offset = 12;

            while (offset + 8 <= length) {
            const uint8_t *chunk = wav + offset;
            uint32_t chunk_size = read_u32(chunk + 4);

            size_t data_offset = offset + 8;

            if (chunk_size > length - data_offset) {
                ESP_LOGE(TAG, "Invalid WAV chunk size");
                return false;
            }

            if (memcmp(chunk, "fmt ", 4) == 0) {
                if (chunk_size < 16) {
                    ESP_LOGE(TAG, "Invalid WAV format chunk");
                    return false;
                }

                audio_format = read_u16(wav + data_offset);
                channels = read_u16(wav + data_offset + 2);
                sample_rate = read_u32(wav + data_offset + 4);
                bits_per_sample = read_u16(wav + data_offset + 14);
            }

            if (memcmp(chunk, "data", 4) == 0) {
                pcm_data = wav + data_offset;
                pcm_size = chunk_size;
            }

            offset = data_offset + chunk_size + (chunk_size & 1);
            }

            if (audio_format != 1 ||
                channels != 1 ||
                sample_rate != 16000 ||
                bits_per_sample != 16 ||
                pcm_data == nullptr ||
                pcm_size == 0) {
            ESP_LOGE(TAG, "Unsupported WAV format");
            return false;
            }

            ESP_LOGI(TAG, "WAV PCM data found: %u bytes",
                    static_cast<unsigned>(pcm_size));

            if (!initialize_speaker()) {
            return false;
            }

            ESP_LOGI(TAG, "Playing audio through MAX98357A...");

            size_t bytes_written = 0;

            esp_err_t result = i2s_channel_write(
                speaker_tx,
                pcm_data,
                pcm_size,
                &bytes_written,
                portMAX_DELAY
            );

            if (result != ESP_OK) {
            ESP_LOGE(TAG, "I2S playback failed: %s",
                    esp_err_to_name(result));
            return false;
            }

            ESP_LOGI(TAG, "Playback transmission complete: %u bytes",
                    static_cast<unsigned>(bytes_written));

            // Flush remaining DMA audio with silence.
            ESP_LOGI(TAG, "Flushing speaker output...");

            int16_t silence[256] = {};
            bool flush_success = true;

            for (int i = 0; i < 16; i++) {
            size_t silence_written = 0;

            result = i2s_channel_write(
                speaker_tx,
                silence,
                sizeof(silence),
                &silence_written,
                portMAX_DELAY
            );

            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to flush speaker output");
                flush_success = false;
                break;
            }
        }

            // Stop and release the I2S channel.
            i2s_channel_disable(speaker_tx);
            i2s_del_channel(speaker_tx);
            speaker_tx = nullptr;

            if (!flush_success) {
                return false;
            }

            ESP_LOGI(TAG, "Speaker output stopped cleanly");

            return true;
        }
        bool play_mp3(const uint8_t *mp3, size_t length) {
            ESP_LOGI(TAG, "Starting MP3 playback...");

            micro_mp3::Mp3Decoder decoder;

            // Allocate PCM output buffer in internal RAM.
            int16_t *pcm_buffer = static_cast<int16_t *>(
                heap_caps_malloc(
                    micro_mp3::MP3_MIN_OUTPUT_BUFFER_BYTES,
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
                )
            );

            if (pcm_buffer == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate MP3 PCM buffer");
                return false;
            }

            size_t input_offset = 0;
            bool speaker_initialized = false;
            bool playback_success = true;

            while (input_offset < length) {
                size_t bytes_consumed = 0;
                size_t samples_decoded = 0;

                micro_mp3::Mp3Result result = decoder.decode(
                    mp3 + input_offset,
                    length - input_offset,
                    reinterpret_cast<uint8_t *>(pcm_buffer),
                    micro_mp3::MP3_MIN_OUTPUT_BUFFER_BYTES,
                    bytes_consumed,
                    samples_decoded
                );

                input_offset += bytes_consumed;

                if (result == micro_mp3::MP3_STREAM_INFO_READY ||
                    result == micro_mp3::MP3_STREAM_INFO_CHANGED) {

                    uint32_t sample_rate = decoder.get_sample_rate();
                    uint8_t channels = decoder.get_channels();

                    ESP_LOGI(
                        TAG,
                        "MP3 format: %lu Hz, %u channel(s)",
                        (unsigned long) sample_rate,
                        channels
                    );

                    if (sample_rate == 0 || channels == 0 || channels > 2) {
                        ESP_LOGE(TAG, "Invalid MP3 stream information");
                        playback_success = false;
                        break;
                    }

                    if (speaker_tx != nullptr) {
                        i2s_channel_disable(speaker_tx);
                        i2s_del_channel(speaker_tx);
                        speaker_tx = nullptr;
                    }

                    if (!initialize_speaker(sample_rate)) {
                        playback_success = false;
                        break;
                    }

                    speaker_initialized = true;
                    continue;
                }

                if (result == micro_mp3::MP3_NEED_MORE_DATA) {
                    if (bytes_consumed == 0) {
                        break;
                    }

                    continue;
                }

                if (result == micro_mp3::MP3_DECODE_ERROR) {
                    ESP_LOGW(TAG, "Skipping corrupted MP3 frame");
                    continue;
                }

                if (result == micro_mp3::MP3_OUTPUT_BUFFER_TOO_SMALL) {
                    ESP_LOGE(TAG, "MP3 output buffer too small");
                    playback_success = false;
                    break;
                }

                if (result < 0) {
                    ESP_LOGE(TAG, "MP3 decoding failed: %d", result);
                    playback_success = false;
                    break;
                }

                if (samples_decoded == 0) {
                    if (bytes_consumed == 0) {
                        break;
                    }

                    continue;
                }

                if (!speaker_initialized) {
                    ESP_LOGE(TAG, "Received PCM before speaker initialization");
                    playback_success = false;
                    break;
                }

                uint8_t channels = decoder.get_channels();

                // Convert stereo PCM to mono when necessary.
                if (channels == 2) {
                    for (size_t i = 0; i < samples_decoded; i++) {
                        int32_t left = pcm_buffer[i * 2];
                        int32_t right = pcm_buffer[i * 2 + 1];

                        pcm_buffer[i] = static_cast<int16_t>(
                            (left + right) / 2
                        );
                    }
                }

                // Reduce volume to 25%
                for (size_t i = 0; i < samples_decoded; i++) {
                    pcm_buffer[i] = static_cast<int16_t>(
                        pcm_buffer[i] * 0.25f
                    );
                }

                size_t pcm_bytes = samples_decoded * sizeof(int16_t);
                size_t bytes_written = 0;

                esp_err_t write_result = i2s_channel_write(
                    speaker_tx,
                    pcm_buffer,
                    pcm_bytes,
                    &bytes_written,
                    portMAX_DELAY
                );

                if (write_result != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "MP3 I2S playback failed: %s",
                        esp_err_to_name(write_result)
                    );

                    playback_success = false;
                    break;
                }
            }

            if (speaker_tx != nullptr) {
                ESP_LOGI(TAG, "Flushing MP3 speaker output...");

                int16_t silence[256] = {};

                for (int i = 0; i < 16; i++) {
                    size_t bytes_written = 0;

                    if (i2s_channel_write(
                        speaker_tx,
                        silence,
                        sizeof(silence),
                        &bytes_written,
                        portMAX_DELAY
                    ) != ESP_OK) {
                        playback_success = false;
                        break;
                    }
                }

                i2s_channel_disable(speaker_tx);
                i2s_del_channel(speaker_tx);
                speaker_tx = nullptr;
            }

            heap_caps_free(pcm_buffer);

            if (!playback_success) {
                ESP_LOGE(TAG, "MP3 playback failed");
                return false;
            }

            ESP_LOGI(TAG, "MP3 playback completed");
            return true;
        }

        static void download_task(void *parameter) {
            auto *self = static_cast<OathboundAudio *>(parameter);
            self->perform_download();
            vTaskDelete(nullptr);
        }

        void perform_download() {
            ESP_LOGI(TAG, "Requesting audio URL from Spring Boot...");

            DownloadContext url_context;
            url_context.capture_body = true;

            esp_http_client_config_t backend_config = {};
            backend_config.url =
                "http://192.168.1.20:8080/api/audio/recording-url";
            backend_config.event_handler = http_event_handler;
            backend_config.user_data = &url_context;
            backend_config.timeout_ms = 15000;

            esp_http_client_handle_t backend =
                esp_http_client_init(&backend_config);

            esp_err_t result = esp_http_client_perform(backend);
            int status = esp_http_client_get_status_code(backend);

            esp_http_client_cleanup(backend);

            if (result != ESP_OK || status != 200) {
            ESP_LOGE(TAG, "Backend request failed. Status: %d", status);
            return;
            }

            std::string url = url_context.body;

            while (!url.empty() &&
                (url.back() == '\n' || url.back() == '\r' ||
                    url.back() == ' ')) {
            url.pop_back();
            }

            ESP_LOGI(TAG, "Received presigned URL. Starting S3 download...");

            DownloadContext audio_context;
            audio_context.capacity = 512 * 1024;

            audio_context.buffer = static_cast<uint8_t *>(
                heap_caps_malloc(
                    audio_context.capacity,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                )
            );

            if (audio_context.buffer == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate audio buffer in PSRAM");
                return;
            }

            ESP_LOGI(TAG, "Allocated 512 KB audio buffer in PSRAM");

            esp_http_client_config_t audio_config = {};
            audio_config.url = url.c_str();
            audio_config.event_handler = http_event_handler;
            audio_config.user_data = &audio_context;
            audio_config.timeout_ms = 30000;
            audio_config.crt_bundle_attach = esp_crt_bundle_attach;

            esp_http_client_handle_t audio =
                esp_http_client_init(&audio_config);

            result = esp_http_client_perform(audio);
            status = esp_http_client_get_status_code(audio);

            esp_http_client_cleanup(audio);

            if (result != ESP_OK || status != 200) {
            ESP_LOGE(TAG, "S3 download failed. Status: %d", status);
            return;
            }
            if (audio_context.overflow) {
                ESP_LOGE(TAG, "Audio buffer overflow!");
                heap_caps_free(audio_context.buffer);
                return;
            }

            ESP_LOGI(TAG, "S3 download successful!");
            ESP_LOGI(TAG, "Received %u bytes of audio",
                    static_cast<unsigned>(audio_context.bytes_received));

            if (audio_context.bytes_received >= 12 &&
                memcmp(audio_context.buffer, "RIFF", 4) == 0 &&
                memcmp(audio_context.buffer + 8, "WAVE", 4) == 0) {
                ESP_LOGI(TAG, "Valid WAV file detected!");

                ESP_LOGI(TAG, "WAV header: %.4s....%.4s",
                    audio_context.buffer,
                    audio_context.buffer + 8);
                uint16_t audio_format =
                    audio_context.buffer[20] |
                    (audio_context.buffer[21] << 8);

                uint16_t channels =
                    audio_context.buffer[22] |
                    (audio_context.buffer[23] << 8);

                uint32_t sample_rate =
                    audio_context.buffer[24] |
                    (audio_context.buffer[25] << 8) |
                    (audio_context.buffer[26] << 16) |
                    (audio_context.buffer[27] << 24);

                uint16_t bits_per_sample =
                    audio_context.buffer[34] |
                    (audio_context.buffer[35] << 8);

                ESP_LOGI(TAG, "Audio format: %u", audio_format);
                ESP_LOGI(TAG, "Channels: %u", channels);
                ESP_LOGI(TAG, "Sample rate: %lu", (unsigned long) sample_rate);
                ESP_LOGI(TAG, "Bits per sample: %u", bits_per_sample);
                if (!play_wav(audio_context.buffer, audio_context.bytes_received)) {
                    ESP_LOGE(TAG, "Audio playback failed");
                } else {
                    ESP_LOGI(TAG, "Audio playback completed");
                    }
            } else {
                ESP_LOGI(TAG, "Non-WAV audio detected. Attempting MP3 playback...");

                if (!play_mp3(
                    audio_context.buffer,
                    audio_context.bytes_received
                )) {
                    ESP_LOGE(TAG, "MP3 playback failed");
                } else {
                    ESP_LOGI(TAG, "MP3 playback completed");
                }
            }

            heap_caps_free(audio_context.buffer);
        }
    };
} 