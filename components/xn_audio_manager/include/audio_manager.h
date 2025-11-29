/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:18:48
 * @FilePath: \xn_esp32_audio\components\audio_manager\include\audio_manager.h
 * @Description: 音频管理器 - 统一封装语音唤醒、录音、播放功能
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "audio_bsp.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============ 调度与缓冲配置宏 ============

#define AUDIO_MANAGER_TASK_STACK_SIZE        (6 * 1024)
#define AUDIO_MANAGER_TASK_PRIORITY          7
#define AUDIO_MANAGER_EVENT_QUEUE_LENGTH     16
#define AUDIO_MANAGER_STEP_INTERVAL_MS       100
#define AUDIO_MANAGER_DEFAULT_VOLUME         80

#define AUDIO_MANAGER_PLAYBACK_FRAME_SAMPLES 1024
#define AUDIO_MANAGER_PLAYBACK_BUFFER_BYTES  (512 * 1024)
#define AUDIO_MANAGER_REFERENCE_BUFFER_BYTES (16 * 1024)

// ============ 状态机定义 ============

typedef enum {
    AUDIO_MGR_STATE_DISABLED = 0,
    AUDIO_MGR_STATE_IDLE,
    AUDIO_MGR_STATE_LISTENING,
    AUDIO_MGR_STATE_RECORDING,
    AUDIO_MGR_STATE_PLAYBACK,
} audio_mgr_state_t;

typedef void (*audio_mgr_state_cb_t)(audio_mgr_state_t state, void *user_ctx);

// ============ 事件定义 ============

/** 音频管理器事件类型 */
typedef enum {
    AUDIO_MGR_EVENT_WAKEUP_DETECTED,    ///< 唤醒词检测到
    AUDIO_MGR_EVENT_VAD_START,          ///< 人声开始
    AUDIO_MGR_EVENT_VAD_END,            ///< 人声结束
    AUDIO_MGR_EVENT_WAKEUP_TIMEOUT,     ///< 唤醒超时（无人说话）
    AUDIO_MGR_EVENT_BUTTON_TRIGGER,     ///< 按键手动触发（按下）
    AUDIO_MGR_EVENT_BUTTON_RELEASE,     ///< 按键松开（新增）
} audio_mgr_event_type_t;

/** 音频管理器事件数据 */
typedef struct {
    audio_mgr_event_type_t type;        ///< 事件类型
    union {
        struct {
            int wake_word_index;        ///< 唤醒词索引
            float volume_db;            ///< 音量(dB)
        } wakeup;
    } data;
} audio_mgr_event_t;

/** 事件回调函数类型（应用层实现） */
typedef void (*audio_mgr_event_cb_t)(const audio_mgr_event_t *event, void *user_ctx);

// ============ 配置结构 ============

/** 硬件配置（应用层提供） */
typedef struct {
    audio_bsp_mic_config_t     mic;     ///< 麦克风 I2S 配置
    audio_bsp_speaker_config_t speaker; ///< 扬声器 I2S 配置
    struct {
        int  gpio;                      ///< 按键 GPIO
        bool active_low;                ///< 低电平有效
    } button;
} audio_mgr_hw_config_t;

/** 唤醒词配置（应用层提供，可后期网页配置） */
typedef struct {
    bool enabled;                   ///< 是否启用唤醒词检测
    const char *wake_word_name;     ///< 唤醒词名称（如"小鸭小鸭"）
    const char *model_partition;    ///< 模型分区名称（默认"model"）
    int sensitivity;                ///< 灵敏度 (0-3: 低/中/高/最高，对应DET_MODE_xxx)
    int wakeup_timeout_ms;          ///< 唤醒超时（无人说话自动结束）
    int wakeup_end_delay_ms;        ///< 说话结束后延迟多久结束唤醒
} audio_mgr_wakeup_config_t;

/** VAD配置（应用层提供） */
typedef struct {
    bool enabled;                   ///< 是否启用VAD
    int vad_mode;                   ///< VAD模式 (0-3)
    int min_speech_ms;              ///< 最小语音持续时间
    int min_silence_ms;             ///< 最小静音持续时间
} audio_mgr_vad_config_t;

/** AFE功能配置（应用层提供） */
typedef struct {
    bool aec_enabled;               ///< 回声消除
    bool ns_enabled;                ///< 降噪
    bool agc_enabled;               ///< 自动增益
    int afe_mode;                   ///< AFE模式（0=LOW_COST, 1=HIGH_QUALITY）
} audio_mgr_afe_config_t;

/** 音频管理器配置（应用层组装） */
typedef struct {
    audio_mgr_hw_config_t      hw_config;       ///< 硬件配置
    audio_mgr_wakeup_config_t  wakeup_config;   ///< 唤醒词配置
    audio_mgr_vad_config_t     vad_config;      ///< VAD配置
    audio_mgr_afe_config_t     afe_config;      ///< AFE配置
    audio_mgr_event_cb_t       event_callback;  ///< 事件回调
    audio_mgr_state_cb_t       state_callback;  ///< 状态机回调
    void                      *user_ctx;        ///< 用户上下文
} audio_mgr_config_t;

#define AUDIO_MANAGER_DEFAULT_HW_CONFIG()                            \
    (audio_mgr_hw_config_t){                                         \
        .mic = {                                                     \
            .port = 0, .bclk_gpio = -1, .lrck_gpio = -1, .din_gpio = -1, \
            .sample_rate = 16000, .bits = 32,                        \
            .max_frame_samples = 512, .bit_shift = 14,               \
        },                                                           \
        .speaker = {                                                 \
            .port = 0, .bclk_gpio = -1, .lrck_gpio = -1, .dout_gpio = -1, \
            .sample_rate = 16000, .bits = 16,                        \
            .max_frame_samples = AUDIO_MANAGER_PLAYBACK_FRAME_SAMPLES,\
        },                                                           \
        .button = { .gpio = -1, .active_low = true },                \
    }

#define AUDIO_MANAGER_DEFAULT_WAKEUP_CONFIG()                        \
    (audio_mgr_wakeup_config_t){                                     \
        .enabled = true,                                             \
        .wake_word_name = "小鸭小鸭",                                \
        .model_partition = "model",                                  \
        .sensitivity = 2,                                            \
        .wakeup_timeout_ms = 8000,                                   \
        .wakeup_end_delay_ms = 1200,                                 \
    }

#define AUDIO_MANAGER_DEFAULT_VAD_CONFIG()                           \
    (audio_mgr_vad_config_t){                                        \
        .enabled = true,                                             \
        .vad_mode = 2,                                               \
        .min_speech_ms = 200,                                        \
        .min_silence_ms = 400,                                       \
    }

#define AUDIO_MANAGER_DEFAULT_AFE_CONFIG()                           \
    (audio_mgr_afe_config_t){                                        \
        .aec_enabled = true,                                         \
        .ns_enabled = true,                                          \
        .agc_enabled = true,                                         \
        .afe_mode = 1,                                               \
    }

#define AUDIO_MANAGER_DEFAULT_CONFIG()                               \
    (audio_mgr_config_t){                                            \
        .hw_config = AUDIO_MANAGER_DEFAULT_HW_CONFIG(),              \
        .wakeup_config = AUDIO_MANAGER_DEFAULT_WAKEUP_CONFIG(),      \
        .vad_config = AUDIO_MANAGER_DEFAULT_VAD_CONFIG(),            \
        .afe_config = AUDIO_MANAGER_DEFAULT_AFE_CONFIG(),            \
        .event_callback = NULL,                                      \
        .state_callback = NULL,                                      \
        .user_ctx = NULL,                                            \
    }

// ============ API接口 ============

/**
 * @brief 初始化音频管理器
 * @param config 配置参数（由应用层提供）
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_init(const audio_mgr_config_t *config);

/**
 * @brief 反初始化音频管理器
 */
void audio_manager_deinit(void);

/**
 * @brief 启动音频管理器（开始监听唤醒词）
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_start(void);

/**
 * @brief 停止音频管理器
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_stop(void);

/**
 * @brief 手动触发对话（按键触发）
 * @note 会发送 AUDIO_MGR_EVENT_BUTTON_TRIGGER 事件
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_trigger_conversation(void);

/**
 * @brief 开始录音（用于对话）
 * @note 录音数据会通过audio_record_callback回调返回
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_start_recording(void);

/**
 * @brief 停止录音
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_stop_recording(void);

/**
 * @brief 播放音频数据（播放器接口）
 * @param pcm_data PCM数据（16bit, 16kHz, 单声道）
 * @param sample_count 采样点数
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_play_audio(const int16_t *pcm_data, size_t sample_count);

/**
 * @brief 获取播放缓冲区可用空间（样本数）
 * 
 * 用于流控：让上层根据可用空间决定发送速率
 * 
 * @return 可用空间（样本数），0表示缓冲区满
 */
size_t audio_manager_get_playback_free_space(void);

/**
 * @brief 开始播放（启动播放任务）
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_start_playback(void);

/**
 * @brief 停止播放
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_stop_playback(void);

/**
 * @brief 清空播放缓冲区（用于打断场景）
 * @note 立即清空所有待播放的音频数据
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_clear_playback_buffer(void);

/**
 * @brief 设置音量
 * @param volume 音量 (0-100)
 */
void audio_manager_set_volume(uint8_t volume);

/**
 * @brief 获取当前音量
 * @return 音量值 (0-100)
 */
uint8_t audio_manager_get_volume(void);

/**
 * @brief 动态更新唤醒词配置（后期网页配置用）
 * @param config 新的唤醒词配置
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_update_wakeup_config(const audio_mgr_wakeup_config_t *config);

/**
 * @brief 获取当前唤醒词配置
 * @param config 输出配置
 * @return ESP_OK 成功
 */
esp_err_t audio_manager_get_wakeup_config(audio_mgr_wakeup_config_t *config);

/**
 * @brief 检查音频管理器是否正在运行
 * @return true 运行中
 */
bool audio_manager_is_running(void);

/**
 * @brief 检查是否正在录音
 * @return true 录音中
 */
bool audio_manager_is_recording(void);

/**
 * @brief 检查是否正在播放
 * @return true 播放中
 */
bool audio_manager_is_playing(void);

/**
 * @brief 获取状态机当前状态
 * @return audio_mgr_state_t
 */
audio_mgr_state_t audio_manager_get_state(void);

// ============ 录音数据回调（应用层实现） ============

/**
 * @brief 录音数据回调函数类型
 * @note 应用层实现此回调，接收录音数据用于发送到Coze
 * @param pcm_data 录音PCM数据（16bit, 16kHz, 单声道）
 * @param sample_count 采样点数
 * @param user_ctx 用户上下文
 */
typedef void (*audio_record_callback_t)(const int16_t *pcm_data, size_t sample_count, void *user_ctx);

/**
 * @brief 注册录音数据回调
 * @param callback 回调函数
 * @param user_ctx 用户上下文
 */
void audio_manager_set_record_callback(audio_record_callback_t callback, void *user_ctx);

#ifdef __cplusplus
}
#endif

