/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:19:33
 * @FilePath: \xn_esp32_audio\components\audio_manager\include\i2s_hal.h
 * @Description: I2S 硬件抽象层 - 封装麦克风和扬声器的 I2S 操作
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "driver/i2s_std.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** I2S 麦克风配置 */
typedef struct {
    int port;           ///< I2S 端口号
    int bclk_gpio;      ///< 位时钟 GPIO
    int lrck_gpio;      ///< 左右声道时钟 GPIO
    int din_gpio;       ///< 数据输入 GPIO
    int sample_rate;    ///< 采样率（通常 16000）
    int bits;           ///< 位深度（硬件采集 32bit，由数据手册要求）
    size_t max_frame_samples;  ///< 最大帧采样数（用于预分配临时缓冲区，默认 512）
    uint8_t bit_shift;  ///< 32位转16位的右移位数（默认 14，可调 12-16）
} i2s_mic_config_t;

/** I2S 扬声器配置 */
typedef struct {
    int port;           ///< I2S 端口号
    int bclk_gpio;      ///< 位时钟 GPIO
    int lrck_gpio;      ///< 左右声道时钟 GPIO
    int dout_gpio;      ///< 数据输出 GPIO
    int sample_rate;    ///< 采样率（通常 16000）
    int bits;           ///< 位深度（16bit）
    size_t max_frame_samples;  ///< 最大帧采样数（用于分配立体声缓冲区）
} i2s_speaker_config_t;

/** I2S HAL 句柄 */
typedef struct i2s_hal_s *i2s_hal_handle_t;

/**
 * @brief 创建 I2S HAL 实例
 * @param mic_config 麦克风配置
 * @param speaker_config 扬声器配置
 * @return I2S HAL 句柄，失败返回 NULL
 */
i2s_hal_handle_t i2s_hal_create(const i2s_mic_config_t *mic_config, 
                                 const i2s_speaker_config_t *speaker_config);

/**
 * @brief 销毁 I2S HAL 实例
 * @param hal I2S HAL 句柄
 */
void i2s_hal_destroy(i2s_hal_handle_t hal);

/**
 * @brief 从麦克风读取音频数据
 * @param hal I2S HAL 句柄
 * @param out_samples 输出缓冲区（16bit PCM）
 * @param sample_count 期望读取的采样点数
 * @param out_got 实际读取的采样点数（可选）
 * @return ESP_OK 成功
 * @note 自动将 32bit 硬件数据转换为 16bit
 */
esp_err_t i2s_hal_read_mic(i2s_hal_handle_t hal, int16_t *out_samples, 
                           size_t sample_count, size_t *out_got);

/**
 * @brief 向扬声器写入音频数据
 * @param hal I2S HAL 句柄
 * @param samples 输入数据（16bit PCM 单声道）
 * @param sample_count 采样点数
 * @param volume 音量（0-100）
 * @return ESP_OK 成功
 * @note 自动将单声道转换为立体声，并应用音量
 */
esp_err_t i2s_hal_write_speaker(i2s_hal_handle_t hal, const int16_t *samples, 
                                 size_t sample_count, uint8_t volume);

/**
 * @brief 获取 RX 句柄（用于 AFE 回调）
 * @param hal I2S HAL 句柄
 * @return RX 句柄
 */
i2s_chan_handle_t i2s_hal_get_rx_handle(i2s_hal_handle_t hal);

/**
 * @brief 获取 TX 句柄（用于 AFE 回调）
 * @param hal I2S HAL 句柄
 * @return TX 句柄
 */
i2s_chan_handle_t i2s_hal_get_tx_handle(i2s_hal_handle_t hal);

#ifdef __cplusplus
}
#endif

