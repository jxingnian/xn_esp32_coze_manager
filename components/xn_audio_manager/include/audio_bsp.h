/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27
 * @FilePath: \xn_esp32_audio\components\xn_audio_manager\include\audio_bsp.h
 * @Description: 音频 BSP 抽象层（当前实现为 I2S，未来可替换为其他硬件）
 */
#pragma once

#include "esp_err.h"
#include "driver/i2s_std.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 麦克风硬件配置
 */
typedef struct {
    int port;                ///< I2S 端口号
    int bclk_gpio;           ///< BCLK GPIO
    int lrck_gpio;           ///< LRCK GPIO
    int din_gpio;            ///< 数据输入 GPIO
    int sample_rate;         ///< 采样率
    int bits;                ///< 位深
    size_t max_frame_samples;///< 最大采样帧数（用于分配临时缓冲）
    uint8_t bit_shift;       ///< 32bit 转 16bit 的右移位数
} audio_bsp_mic_config_t;

/**
 * @brief 扬声器硬件配置
 */
typedef struct {
    int port;                ///< I2S 端口号
    int bclk_gpio;           ///< BCLK GPIO
    int lrck_gpio;           ///< LRCK GPIO
    int dout_gpio;           ///< 数据输出 GPIO
    int sample_rate;         ///< 采样率
    int bits;                ///< 位深
    size_t max_frame_samples;///< 最大采样帧数
} audio_bsp_speaker_config_t;

/**
 * @brief BSP 硬件配置
 */
typedef struct {
    audio_bsp_mic_config_t mic;
    audio_bsp_speaker_config_t speaker;
} audio_bsp_hw_config_t;

typedef struct audio_bsp_s *audio_bsp_handle_t;

audio_bsp_handle_t audio_bsp_create(const audio_bsp_hw_config_t *config);

void audio_bsp_destroy(audio_bsp_handle_t handle);

esp_err_t audio_bsp_read_mic(audio_bsp_handle_t handle,
                             int16_t *out_samples,
                             size_t sample_count,
                             size_t *out_got);

esp_err_t audio_bsp_write_speaker(audio_bsp_handle_t handle,
                                  const int16_t *samples,
                                  size_t sample_count,
                                  uint8_t volume);

i2s_chan_handle_t audio_bsp_get_rx(audio_bsp_handle_t handle);

i2s_chan_handle_t audio_bsp_get_tx(audio_bsp_handle_t handle);

#ifdef __cplusplus
}
#endif


