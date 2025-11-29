/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27
 * @FilePath: \xn_esp32_audio\components\xn_audio_manager\src\audio_bsp.c
 * @Description: 默认 I2S BSP 实现
 */

#include "audio_bsp.h"
#include "i2s_hal.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "audio_bsp";

struct audio_bsp_s {
    i2s_hal_handle_t i2s;
};

audio_bsp_handle_t audio_bsp_create(const audio_bsp_hw_config_t *config)
{
    if (!config) {
        return NULL;
    }

    i2s_mic_config_t mic_cfg = {
        .port = config->mic.port,
        .bclk_gpio = config->mic.bclk_gpio,
        .lrck_gpio = config->mic.lrck_gpio,
        .din_gpio = config->mic.din_gpio,
        .sample_rate = config->mic.sample_rate,
        .bits = config->mic.bits,
        .max_frame_samples = config->mic.max_frame_samples ? config->mic.max_frame_samples : 512,
        .bit_shift = config->mic.bit_shift ? config->mic.bit_shift : 14,
    };

    i2s_speaker_config_t speaker_cfg = {
        .port = config->speaker.port,
        .bclk_gpio = config->speaker.bclk_gpio,
        .lrck_gpio = config->speaker.lrck_gpio,
        .dout_gpio = config->speaker.dout_gpio,
        .sample_rate = config->speaker.sample_rate,
        .bits = config->speaker.bits,
        .max_frame_samples = config->speaker.max_frame_samples ? config->speaker.max_frame_samples : 1024,
    };

    i2s_hal_handle_t hal = i2s_hal_create(&mic_cfg, &speaker_cfg);
    if (!hal) {
        ESP_LOGE(TAG, "create I2S HAL failed");
        return NULL;
    }

    audio_bsp_handle_t handle = (audio_bsp_handle_t)calloc(1, sizeof(struct audio_bsp_s));
    if (!handle) {
        ESP_LOGE(TAG, "alloc audio_bsp failed");
        i2s_hal_destroy(hal);
        return NULL;
    }

    handle->i2s = hal;
    ESP_LOGI(TAG, "audio BSP (I2S) ready");
    return handle;
}

void audio_bsp_destroy(audio_bsp_handle_t handle)
{
    if (!handle) {
        return;
    }

    if (handle->i2s) {
        i2s_hal_destroy(handle->i2s);
        handle->i2s = NULL;
    }

    free(handle);
}

esp_err_t audio_bsp_read_mic(audio_bsp_handle_t handle,
                             int16_t *out_samples,
                             size_t sample_count,
                             size_t *out_got)
{
    if (!handle || !handle->i2s) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2s_hal_read_mic(handle->i2s, out_samples, sample_count, out_got);
}

esp_err_t audio_bsp_write_speaker(audio_bsp_handle_t handle,
                                  const int16_t *samples,
                                  size_t sample_count,
                                  uint8_t volume)
{
    if (!handle || !handle->i2s) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2s_hal_write_speaker(handle->i2s, samples, sample_count, volume);
}

i2s_chan_handle_t audio_bsp_get_rx(audio_bsp_handle_t handle)
{
    if (!handle || !handle->i2s) {
        return NULL;
    }
    return i2s_hal_get_rx_handle(handle->i2s);
}

i2s_chan_handle_t audio_bsp_get_tx(audio_bsp_handle_t handle)
{
    if (!handle || !handle->i2s) {
        return NULL;
    }
    return i2s_hal_get_tx_handle(handle->i2s);
}


