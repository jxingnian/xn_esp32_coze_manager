/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-28 21:15:25
 * @FilePath: \xn_esp32_audio\components\xn_audio_manager\include\playback_controller.h
 * @Description: 播放控制模块 - 管理音频播放任务和缓冲区
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "ring_buffer.h"
#include "audio_bsp.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 播放控制器句柄 */
typedef struct playback_controller_s *playback_controller_handle_t;

/** 回采数据回调函数类型 */
typedef void (*playback_reference_callback_t)(const int16_t *samples, size_t count, void *user_ctx);

/** 播放控制器配置 */
typedef struct {
    audio_bsp_handle_t bsp_handle;                  ///< 音频 BSP 句柄（抽象硬件）
    size_t playback_buffer_samples;                  ///< 播放缓冲区大小（采样点数）
    size_t reference_buffer_samples;                 ///< 回采缓冲区大小（采样点数）
    size_t frame_samples;                            ///< 每帧采样点数
    playback_reference_callback_t reference_callback; ///< 回采数据回调（可选，用于AFE）
    void *reference_ctx;                             ///< 回采回调上下文
    uint8_t *volume_ptr;                             ///< 音量指针（外部管理）
} playback_controller_config_t;

/**
 * @brief 创建播放控制器
 * @param config 配置参数
 * @return 播放控制器句柄，失败返回 NULL
 */
playback_controller_handle_t playback_controller_create(const playback_controller_config_t *config);

/**
 * @brief 销毁播放控制器
 * @param controller 播放控制器句柄
 */
void playback_controller_destroy(playback_controller_handle_t controller);

/**
 * @brief 启动播放任务
 * @param controller 播放控制器句柄
 * @return ESP_OK 成功
 */
esp_err_t playback_controller_start(playback_controller_handle_t controller);

/**
 * @brief 停止播放任务
 * @param controller 播放控制器句柄
 * @return ESP_OK 成功
 */
esp_err_t playback_controller_stop(playback_controller_handle_t controller);

/**
 * @brief 写入音频数据到播放缓冲区
 * @param controller 播放控制器句柄
 * @param pcm_data PCM 数据（16bit, 单声道）
 * @param sample_count 采样点数
 * @return ESP_OK 成功
 */
esp_err_t playback_controller_write(playback_controller_handle_t controller, 
                                     const int16_t *pcm_data, size_t sample_count);

/**
 * @brief 清空播放缓冲区
 * @param controller 播放控制器句柄
 * @return ESP_OK 成功
 */
esp_err_t playback_controller_clear(playback_controller_handle_t controller);

/**
 * @brief 检查是否正在播放
 * @param controller 播放控制器句柄
 * @return true 正在播放
 */
bool playback_controller_is_running(playback_controller_handle_t controller);

/**
 * @brief 获取播放缓冲区可用空间（样本数）
 * @param controller 播放控制器句柄
 * @return 可用空间（样本数），用于流控
 */
size_t playback_controller_get_free_space(playback_controller_handle_t controller);

/**
 * @brief 获取回采缓冲区（用于 AFE 读取）
 * @param controller 播放控制器句柄
 * @return 回采缓冲区句柄
 */
ring_buffer_handle_t playback_controller_get_reference_buffer(playback_controller_handle_t controller);

#ifdef __cplusplus
}
#endif

