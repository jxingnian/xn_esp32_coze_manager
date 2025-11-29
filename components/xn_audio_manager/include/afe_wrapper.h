/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:18:25
 * @FilePath: \xn_esp32_audio\components\audio_manager\include\afe_wrapper.h
 * @Description: AFE 管理模块 - 封装 AFE Manager 和语音识别功能
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "audio_bsp.h"
#include "ring_buffer.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** AFE 事件类型 */
typedef enum {
    AFE_EVENT_WAKEUP_DETECTED,  ///< 唤醒词检测到
    AFE_EVENT_VAD_START,        ///< 人声开始
    AFE_EVENT_VAD_END,          ///< 人声结束
} afe_event_type_t;

/** AFE 事件数据 */
typedef struct {
    afe_event_type_t type;
    union {
        struct {
            int wake_word_index;
            float volume_db;
        } wakeup;
    } data;
} afe_event_t;

/** AFE 事件回调 */
typedef void (*afe_event_callback_t)(const afe_event_t *event, void *user_ctx);

/** 录音数据回调 */
typedef void (*afe_record_callback_t)(const int16_t *pcm_data, size_t samples, void *user_ctx);

/** AFE 唤醒词配置 */
typedef struct {
    bool enabled;
    const char *wake_word_name;
    const char *model_partition;
    int sensitivity;
} afe_wakeup_config_t;

/** AFE VAD 配置 */
typedef struct {
    bool enabled;
    int vad_mode;
    int min_speech_ms;
    int min_silence_ms;
} afe_vad_config_t;

/** AFE 功能配置 */
typedef struct {
    bool aec_enabled;
    bool ns_enabled;
    bool agc_enabled;
    int afe_mode;
} afe_feature_config_t;

/** AFE 包装器配置 */
typedef struct {
    audio_bsp_handle_t bsp_handle;             ///< BSP 句柄
    ring_buffer_handle_t reference_rb;          ///< 回采缓冲区
    afe_wakeup_config_t wakeup_config;          ///< 唤醒词配置
    afe_vad_config_t vad_config;                ///< VAD 配置
    afe_feature_config_t feature_config;        ///< 功能配置
    afe_event_callback_t event_callback;        ///< 事件回调
    void *event_ctx;                            ///< 事件回调上下文
    afe_record_callback_t record_callback;      ///< 录音回调
    void *record_ctx;                           ///< 录音回调上下文
    bool *running_ptr;                          ///< 运行状态指针（外部管理）
    bool *recording_ptr;                        ///< 录音状态指针（外部管理）
} afe_wrapper_config_t;

/** AFE 包装器句柄 */
typedef struct afe_wrapper_s *afe_wrapper_handle_t;

/**
 * @brief 创建 AFE 包装器
 * @param config 配置参数
 * @return AFE 包装器句柄，失败返回 NULL
 */
afe_wrapper_handle_t afe_wrapper_create(const afe_wrapper_config_t *config);

/**
 * @brief 销毁 AFE 包装器
 * @param wrapper AFE 包装器句柄
 */
void afe_wrapper_destroy(afe_wrapper_handle_t wrapper);

/**
 * @brief 更新唤醒词配置
 * @param wrapper AFE 包装器句柄
 * @param config 新配置
 * @return ESP_OK 成功
 */
esp_err_t afe_wrapper_update_wakeup_config(afe_wrapper_handle_t wrapper, 
                                            const afe_wakeup_config_t *config);

/**
 * @brief 获取唤醒词配置
 * @param wrapper AFE 包装器句柄
 * @param config 输出配置
 * @return ESP_OK 成功
 */
esp_err_t afe_wrapper_get_wakeup_config(afe_wrapper_handle_t wrapper, 
                                         afe_wakeup_config_t *config);

#ifdef __cplusplus
}
#endif

