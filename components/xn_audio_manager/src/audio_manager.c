/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-11-28 19:36:06
 * @FilePath: \xn_esp32_audio\components\audio_manager\src\audio_manager.c
 * @Description: 音频管理器实现 - 模块化架构
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "audio_manager.h"
#include "ring_buffer.h"
#include "playback_controller.h"
#include "button_handler.h"
#include "afe_wrapper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "AUDIO_MGR";

typedef enum {
    AUDIO_INT_EVT_START_LISTEN = 0,
    AUDIO_INT_EVT_STOP_LISTEN,
    AUDIO_INT_EVT_BUTTON_PRESS,
    AUDIO_INT_EVT_BUTTON_RELEASE,
    AUDIO_INT_EVT_WAKE_WORD,
    AUDIO_INT_EVT_VAD_START,
    AUDIO_INT_EVT_VAD_END,
    AUDIO_INT_EVT_WAKE_TIMEOUT,
} audio_mgr_internal_event_t;

typedef struct {
    audio_mgr_internal_event_t type;
    union {
        struct {
            int   wake_word_index;
            float volume_db;
        } wakeup;
    } data;
} audio_mgr_internal_msg_t;

// ============ 音频管理器上下文 ============

/**
 * @brief 音频管理器上下文结构体
 * 
 * 存储音频管理器的所有状态和配置信息，包括：
 * - 各模块的句柄
 * - 共享缓冲区
 * - 运行状态
 * - 回调函数
 */
typedef struct {
    // 配置
    audio_mgr_config_t config;              ///< 音频管理器配置参数
    
    // 模块句柄
    audio_bsp_handle_t bsp;                ///< 硬件 BSP 句柄
    playback_controller_handle_t playback_ctrl;  ///< 播放控制器句柄
    button_handler_handle_t button_handler; ///< 按键处理器句柄
    afe_wrapper_handle_t afe_wrapper;      ///< AFE 包装器句柄
    
    // 共享缓冲区
    ring_buffer_handle_t reference_rb;     ///< 回采缓冲区句柄（播放控制器和 AFE 共享）
    
    // 状态
    bool initialized;                       ///< 是否已初始化
    bool running;                           ///< 是否正在运行（监听音频）
    bool recording;                         ///< 是否正在录音
    bool playing;                           ///< 是否正在播放
    uint8_t volume;                         ///< 音量（0-100）
    audio_mgr_state_t state;                ///< 状态机
    bool wake_active;                       ///< 是否处于唤醒窗口
    TickType_t wake_deadline_tick;          ///< 唤醒超时tick
    
    // 回调
    audio_record_callback_t record_callback; ///< 录音数据回调函数
    void *record_ctx;                        ///< 录音回调的用户上下文

    // 调度
    QueueHandle_t event_queue;
    TaskHandle_t manager_task;

} audio_manager_ctx_t;

/**
 * @brief 音频管理器全局上下文实例
 * 使用静态变量存储，确保全局唯一性
 */
static audio_manager_ctx_t s_ctx = {0};

static void audio_manager_set_state(audio_mgr_state_t new_state);
static void audio_manager_refresh_state(void);
static void audio_manager_notify_event(const audio_mgr_event_t *event);
static bool audio_manager_post_event(const audio_mgr_internal_msg_t *msg);
static void audio_manager_handle_internal_event(const audio_mgr_internal_msg_t *msg);
static void audio_manager_task(void *arg);
static void audio_manager_tick(void);
static void audio_manager_arm_wake_timer(int duration_ms);
static void audio_manager_clear_wake_timer(void);

static void audio_manager_set_state(audio_mgr_state_t new_state)
{
    if (s_ctx.state == new_state) {
        return;
    }
    s_ctx.state = new_state;
    ESP_LOGD(TAG, "state -> %d", new_state);
    if (s_ctx.config.state_callback) {
        s_ctx.config.state_callback(new_state, s_ctx.config.user_ctx);
    }
}

static void audio_manager_refresh_state(void)
{
    if (!s_ctx.initialized) {
        audio_manager_set_state(AUDIO_MGR_STATE_DISABLED);
        return;
    }

    if (s_ctx.playing) {
        audio_manager_set_state(AUDIO_MGR_STATE_PLAYBACK);
    } else if (s_ctx.recording) {
        audio_manager_set_state(AUDIO_MGR_STATE_RECORDING);
    } else if (s_ctx.running) {
        audio_manager_set_state(AUDIO_MGR_STATE_LISTENING);
    } else {
        audio_manager_set_state(AUDIO_MGR_STATE_IDLE);
    }
}

static void audio_manager_notify_event(const audio_mgr_event_t *event)
{
    if (!event || !s_ctx.config.event_callback) {
        return;
    }
    s_ctx.config.event_callback(event, s_ctx.config.user_ctx);
}

static bool audio_manager_post_event(const audio_mgr_internal_msg_t *msg)
{
    if (!s_ctx.event_queue || !msg) {
        return false;
    }
    if (xQueueSend(s_ctx.event_queue, msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, drop type=%d", msg->type);
        return false;
    }
    return true;
}

static void audio_manager_arm_wake_timer(int duration_ms)
{
    if (duration_ms <= 0) {
        audio_manager_clear_wake_timer();
        return;
    }
    s_ctx.wake_active = true;
    s_ctx.wake_deadline_tick = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
}

static void audio_manager_clear_wake_timer(void)
{
    s_ctx.wake_active = false;
    s_ctx.wake_deadline_tick = 0;
}

static void audio_manager_tick(void)
{
    if (!s_ctx.wake_active) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_ctx.wake_deadline_tick) >= 0) {
        s_ctx.wake_active = false;
        audio_mgr_internal_msg_t msg = {
            .type = AUDIO_INT_EVT_WAKE_TIMEOUT,
        };
        audio_manager_handle_internal_event(&msg);
    }
}

// ============ 内部回调函数 ============

/**
 * @brief 按键事件回调函数
 * 
 * 当按键被按下或松开时，由按键处理器调用此函数。
 * 将按键事件转换为音频管理器事件并通知上层应用。
 * 
 * @param event 按键事件类型（按下/松开）
 * @param user_ctx 用户上下文（未使用）
 */
static void button_event_handler(button_event_type_t event, void *user_ctx)
{
    audio_mgr_internal_msg_t msg = {
        .type = (event == BUTTON_EVENT_PRESS) ? AUDIO_INT_EVT_BUTTON_PRESS
                                              : AUDIO_INT_EVT_BUTTON_RELEASE,
    };
    audio_manager_post_event(&msg);
}

/**
 * @brief AFE 事件回调函数
 * 
 * 当 AFE 检测到唤醒词、VAD 开始/结束时，由 AFE 包装器调用此函数。
 * 将 AFE 事件转换为音频管理器事件并通知上层应用。
 * 
 * @param event AFE 事件指针
 * @param user_ctx 用户上下文（未使用）
 */
static void afe_event_handler(const afe_event_t *event, void *user_ctx)
{
    if (!event) {
        return;
    }

    audio_mgr_internal_msg_t msg = {0};

    switch (event->type) {
        case AFE_EVENT_WAKEUP_DETECTED:
            msg.type = AUDIO_INT_EVT_WAKE_WORD;
            msg.data.wakeup.wake_word_index = event->data.wakeup.wake_word_index;
            msg.data.wakeup.volume_db = event->data.wakeup.volume_db;
            break;
            
        case AFE_EVENT_VAD_START:
            msg.type = AUDIO_INT_EVT_VAD_START;
            break;
            
        case AFE_EVENT_VAD_END:
            msg.type = AUDIO_INT_EVT_VAD_END;
            break;
        default:
            return;
    }

    audio_manager_post_event(&msg);
}

/**
 * @brief AFE 录音数据回调函数
 * 
 * 当 AFE 处理完音频数据后，调用此函数将处理后的音频数据传递给上层应用。
 * 
 * @param pcm_data PCM 音频数据指针
 * @param samples 采样点数
 * @param user_ctx 用户上下文（未使用）
 */
static void afe_record_handler(const int16_t *pcm_data, size_t samples, void *user_ctx)
{
    // 如果设置了录音回调，则调用它
    if (s_ctx.record_callback) {
        s_ctx.record_callback(pcm_data, samples, s_ctx.record_ctx);
    }
}

static void audio_manager_handle_internal_event(const audio_mgr_internal_msg_t *msg)
{
    if (!msg) {
        return;
    }

    audio_mgr_event_t evt = {0};

    switch (msg->type) {
    case AUDIO_INT_EVT_START_LISTEN:
        if (!s_ctx.running) {
            ESP_LOGI(TAG, "🎧 启动音频监听");
        }
        s_ctx.running = true;
        audio_manager_clear_wake_timer();
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_STOP_LISTEN:
        if (s_ctx.running) {
            ESP_LOGI(TAG, "🛑 停止音频监听");
        }
        s_ctx.running = false;
        s_ctx.recording = false;
        audio_manager_clear_wake_timer();
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_BUTTON_PRESS:
        ESP_LOGI(TAG, "🔘 按键按下");
        evt.type = AUDIO_MGR_EVENT_BUTTON_TRIGGER;
        audio_manager_notify_event(&evt);
        s_ctx.recording = true;
        audio_manager_arm_wake_timer(s_ctx.config.wakeup_config.wakeup_timeout_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_BUTTON_RELEASE:
        evt.type = AUDIO_MGR_EVENT_BUTTON_RELEASE;
        audio_manager_notify_event(&evt);
        break;

    case AUDIO_INT_EVT_WAKE_WORD:
        evt.type = AUDIO_MGR_EVENT_WAKEUP_DETECTED;
        evt.data.wakeup.wake_word_index = msg->data.wakeup.wake_word_index;
        evt.data.wakeup.volume_db = msg->data.wakeup.volume_db;
        audio_manager_notify_event(&evt);
        s_ctx.recording = true;
        audio_manager_arm_wake_timer(s_ctx.config.wakeup_config.wakeup_timeout_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_VAD_START:
        evt.type = AUDIO_MGR_EVENT_VAD_START;
        audio_manager_notify_event(&evt);
        s_ctx.recording = true;
        audio_manager_arm_wake_timer(s_ctx.config.wakeup_config.wakeup_timeout_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_VAD_END:
        evt.type = AUDIO_MGR_EVENT_VAD_END;
        audio_manager_notify_event(&evt);
        s_ctx.recording = false;
        audio_manager_arm_wake_timer(s_ctx.config.wakeup_config.wakeup_end_delay_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_WAKE_TIMEOUT:
        evt.type = AUDIO_MGR_EVENT_WAKEUP_TIMEOUT;
        audio_manager_notify_event(&evt);
        s_ctx.recording = false;
        audio_manager_clear_wake_timer();
        audio_manager_refresh_state();
        break;
    }
}

static void audio_manager_task(void *arg)
{
    audio_mgr_internal_msg_t msg = {0};

    while (true) {
        if (xQueueReceive(s_ctx.event_queue, &msg, pdMS_TO_TICKS(AUDIO_MANAGER_STEP_INTERVAL_MS)) == pdTRUE) {
            audio_manager_handle_internal_event(&msg);
        }
        audio_manager_tick();
    }
}

// ============ 公共 API 实现 ============

/**
 * @brief 初始化音频管理器
 * 
 * 按照以下顺序初始化各个模块：
 * 1. 创建 I2S HAL（硬件抽象层）
 * 2. 创建回采缓冲区（用于 AEC）
 * 3. 创建播放控制器（管理音频播放）
 * 4. 创建 AFE 包装器（音频前端处理）
 * 5. 创建按键处理器（处理物理按键）
 * 
 * @param config 音频管理器配置参数
 * @return 
 *     - ESP_OK: 初始化成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t audio_manager_init(const audio_mgr_config_t *config)
{
    esp_err_t ret = ESP_OK;

    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "音频管理器已初始化");
        return ESP_OK;
    }

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "======== 初始化音频管理器（模块化状态机）========");
    memset(&s_ctx, 0, sizeof(s_ctx));
    memcpy(&s_ctx.config, config, sizeof(audio_mgr_config_t));
    s_ctx.volume = AUDIO_MANAGER_DEFAULT_VOLUME;
    s_ctx.state = AUDIO_MGR_STATE_DISABLED;

    audio_bsp_hw_config_t bsp_cfg = {
        .mic = s_ctx.config.hw_config.mic,
        .speaker = s_ctx.config.hw_config.speaker,
    };

    s_ctx.bsp = audio_bsp_create(&bsp_cfg);
    if (!s_ctx.bsp) {
        ESP_LOGE(TAG, "BSP 创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    playback_controller_config_t playback_cfg = {
        .bsp_handle = s_ctx.bsp,
        .playback_buffer_samples = AUDIO_MANAGER_PLAYBACK_BUFFER_BYTES / sizeof(int16_t),
        .reference_buffer_samples = AUDIO_MANAGER_REFERENCE_BUFFER_BYTES / sizeof(int16_t),
        .frame_samples = AUDIO_MANAGER_PLAYBACK_FRAME_SAMPLES,
        .reference_callback = NULL,
        .reference_ctx = NULL,
        .volume_ptr = &s_ctx.volume,
    };

    s_ctx.playback_ctrl = playback_controller_create(&playback_cfg);
    if (!s_ctx.playback_ctrl) {
        ESP_LOGE(TAG, "播放控制器创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_ctx.reference_rb = playback_controller_get_reference_buffer(s_ctx.playback_ctrl);

    s_ctx.event_queue = xQueueCreate(AUDIO_MANAGER_EVENT_QUEUE_LENGTH, sizeof(audio_mgr_internal_msg_t));
    if (!s_ctx.event_queue) {
        ESP_LOGE(TAG, "事件队列创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    if (xTaskCreatePinnedToCore(audio_manager_task,
                                "audio_mgr",
                                AUDIO_MANAGER_TASK_STACK_SIZE,
                                NULL,
                                AUDIO_MANAGER_TASK_PRIORITY,
                                &s_ctx.manager_task,
                                0) != pdPASS) {
        ESP_LOGE(TAG, "状态机任务创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    afe_wrapper_config_t afe_cfg = {
        .bsp_handle = s_ctx.bsp,
        .reference_rb = s_ctx.reference_rb,
        .wakeup_config = (afe_wakeup_config_t){
            .enabled = s_ctx.config.wakeup_config.enabled,
            .wake_word_name = s_ctx.config.wakeup_config.wake_word_name,
            .model_partition = s_ctx.config.wakeup_config.model_partition,
            .sensitivity = s_ctx.config.wakeup_config.sensitivity,
        },
        .vad_config = (afe_vad_config_t){
            .enabled = s_ctx.config.vad_config.enabled,
            .vad_mode = s_ctx.config.vad_config.vad_mode,
            .min_speech_ms = s_ctx.config.vad_config.min_speech_ms,
            .min_silence_ms = s_ctx.config.vad_config.min_silence_ms,
        },
        .feature_config = (afe_feature_config_t){
            .aec_enabled = s_ctx.config.afe_config.aec_enabled,
            .ns_enabled = s_ctx.config.afe_config.ns_enabled,
            .agc_enabled = s_ctx.config.afe_config.agc_enabled,
            .afe_mode = s_ctx.config.afe_config.afe_mode,
        },
        .event_callback = afe_event_handler,
        .event_ctx = NULL,
        .record_callback = afe_record_handler,
        .record_ctx = NULL,
        .running_ptr = &s_ctx.running,
        .recording_ptr = &s_ctx.recording,
    };

    s_ctx.afe_wrapper = afe_wrapper_create(&afe_cfg);
    if (!s_ctx.afe_wrapper) {
        ESP_LOGE(TAG, "AFE 包装器创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    button_handler_config_t button_cfg = {
        .gpio = s_ctx.config.hw_config.button.gpio,
        .active_low = s_ctx.config.hw_config.button.active_low,
        .debounce_ms = 50,
        .callback = button_event_handler,
        .user_ctx = NULL,
    };

    s_ctx.button_handler = button_handler_create(&button_cfg);
    if (!s_ctx.button_handler) {
        ESP_LOGE(TAG, "按键处理器创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_ctx.initialized = true;
    s_ctx.state = AUDIO_MGR_STATE_IDLE;
    audio_manager_refresh_state();
    ESP_LOGI(TAG, "✅ 音频管理器初始化完成");
    return ESP_OK;

fail:
    audio_manager_deinit();
    return ret;
}

/**
 * @brief 反初始化音频管理器
 * 
 * 按照与初始化相反的顺序销毁各个模块，释放资源。
 * 注意：reference_rb 由播放控制器管理，不需要单独销毁。
 */
void audio_manager_deinit(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized && !s_ctx.bsp) {
        return;
    }

    // 停止所有运行中的功能
    audio_manager_stop();
    audio_manager_stop_playback();

    if (s_ctx.manager_task) {
        vTaskDelete(s_ctx.manager_task);
        s_ctx.manager_task = NULL;
    }

    if (s_ctx.event_queue) {
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
    }

    // 销毁按键处理器
    if (s_ctx.button_handler) {
        button_handler_destroy(s_ctx.button_handler);
        s_ctx.button_handler = NULL;
    }

    // 销毁 AFE 包装器
    if (s_ctx.afe_wrapper) {
        afe_wrapper_destroy(s_ctx.afe_wrapper);
        s_ctx.afe_wrapper = NULL;
    }

    // 销毁播放控制器
    if (s_ctx.playback_ctrl) {
        playback_controller_destroy(s_ctx.playback_ctrl);
        s_ctx.playback_ctrl = NULL;
    }

    // 销毁 I2S HAL
    if (s_ctx.bsp) {
        audio_bsp_destroy(s_ctx.bsp);
        s_ctx.bsp = NULL;
    }

    // reference_rb 由播放控制器管理，不需要单独销毁

    // 清空上下文
    memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "音频管理器已销毁");
}

/**
 * @brief 启动音频监听
 * 
 * 启动音频监听功能，开始检测唤醒词和语音活动。
 * 
 * @return 
 *     - ESP_OK: 启动成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_start(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    
    audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_START_LISTEN };
    audio_manager_post_event(&msg);
    return ESP_OK;
}

/**
 * @brief 停止音频监听
 * 
 * 停止音频监听功能，不再检测唤醒词和语音活动。
 * 
 * @return ESP_OK: 停止成功
 */
esp_err_t audio_manager_stop(void)
{
    if (!s_ctx.initialized) {
        return ESP_OK;
    }
    audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_STOP_LISTEN };
    audio_manager_post_event(&msg);
    return ESP_OK;
}

/**
 * @brief 触发对话
 * 
 * 手动触发对话，模拟按键按下事件。
 * 用于程序内部触发对话，而不需要物理按键。
 * 
 * @return 
 *     - ESP_OK: 触发成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_trigger_conversation(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;

    audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_BUTTON_PRESS };
    audio_manager_post_event(&msg);
    return ESP_OK;
}

/**
 * @brief 开始录音
 * 
 * 设置录音标志，AFE 会开始将处理后的音频数据通过回调传递给上层应用。
 * 
 * @return 
 *     - ESP_OK: 开始成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_start_recording(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "📼 开始录音");
    s_ctx.recording = true;
    audio_manager_refresh_state();

    return ESP_OK;
}

/**
 * @brief 停止录音
 * 
 * 清除录音标志，AFE 停止传递音频数据。
 * 
 * @return ESP_OK: 停止成功
 */
esp_err_t audio_manager_stop_recording(void)
{
    // 如果未在录音，直接返回
    if (!s_ctx.recording) return ESP_OK;

    ESP_LOGI(TAG, "⏹️ 停止录音");
    s_ctx.recording = false;
    audio_manager_refresh_state();

    return ESP_OK;
}

/**
 * @brief 播放音频数据
 * 
 * 将 PCM 音频数据写入播放缓冲区，等待播放。
 * 
 * @param pcm_data PCM 音频数据指针
 * @param sample_count 采样点数
 * @return 
 *     - ESP_OK: 写入成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_play_audio(const int16_t *pcm_data, size_t sample_count)
{
    // 参数检查
    if (!s_ctx.initialized || !pcm_data || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 写入播放缓冲区
    return playback_controller_write(s_ctx.playback_ctrl, pcm_data, sample_count);
}

size_t audio_manager_get_playback_free_space(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized || !s_ctx.playback_ctrl) {
        return 0;
    }
    
    return playback_controller_get_free_space(s_ctx.playback_ctrl);
}

/**
 * @brief 启动播放
 * 
 * 启动播放控制器，开始播放缓冲区中的音频数据。
 * 
 * @return 
 *     - ESP_OK: 启动成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_start_playback(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = playback_controller_start(s_ctx.playback_ctrl);
    if (ret == ESP_OK) {
        s_ctx.playing = true;
        audio_manager_refresh_state();
    }
    return ret;
}

/**
 * @brief 停止播放
 * 
 * 停止播放控制器，不再播放音频。
 * 
 * @return ESP_OK: 停止成功
 */
esp_err_t audio_manager_stop_playback(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized) return ESP_OK;

    esp_err_t ret = playback_controller_stop(s_ctx.playback_ctrl);
    if (ret == ESP_OK) {
        s_ctx.playing = false;
        audio_manager_refresh_state();
    }
    return ret;
}

/**
 * @brief 清空播放缓冲区
 * 
 * 清空播放缓冲区中的所有待播放数据。
 * 
 * @return 
 *     - ESP_OK: 清空成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_clear_playback_buffer(void)
{
    // 检查是否已初始化
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;

    return playback_controller_clear(s_ctx.playback_ctrl);
}

/**
 * @brief 设置音量
 * 
 * 设置播放音量，范围 0-100。
 * 
 * @param volume 音量值（0-100）
 */
void audio_manager_set_volume(uint8_t volume)
{
    // 限制音量范围
    if (volume > 100) volume = 100;
    s_ctx.volume = volume;
    ESP_LOGI(TAG, "🔊 音量: %d%%", volume);
}

/**
 * @brief 获取音量
 * 
 * 获取当前播放音量。
 * 
 * @return 音量值（0-100）
 */
uint8_t audio_manager_get_volume(void)
{
    return s_ctx.volume;
}

/**
 * @brief 更新唤醒词配置
 * 
 * 动态更新唤醒词检测的配置参数。
 * 
 * @param config 唤醒词配置参数
 * @return 
 *     - ESP_OK: 更新成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_update_wakeup_config(const audio_mgr_wakeup_config_t *config)
{
    // 参数检查
    if (!s_ctx.initialized || !config) return ESP_ERR_INVALID_ARG;

    // 更新配置
    memcpy(&s_ctx.config.wakeup_config, config, sizeof(audio_mgr_wakeup_config_t));
    
    // 构造 AFE 唤醒词配置
    afe_wakeup_config_t afe_wakeup = {
        .enabled = config->enabled,
        .wake_word_name = config->wake_word_name,
        .model_partition = config->model_partition,
        .sensitivity = config->sensitivity,
    };
    
    // 更新 AFE 配置
    return afe_wrapper_update_wakeup_config(s_ctx.afe_wrapper, &afe_wakeup);
}

/**
 * @brief 获取唤醒词配置
 * 
 * 获取当前唤醒词检测的配置参数。
 * 
 * @param config 输出参数，用于存储配置
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t audio_manager_get_wakeup_config(audio_mgr_wakeup_config_t *config)
{
    // 参数检查
    if (!s_ctx.initialized || !config) return ESP_ERR_INVALID_ARG;

    // 复制配置
    memcpy(config, &s_ctx.config.wakeup_config, sizeof(audio_mgr_wakeup_config_t));

    return ESP_OK;
}

/**
 * @brief 检查是否正在运行
 * 
 * 检查音频监听是否正在运行。
 * 
 * @return true: 正在运行，false: 未运行
 */
bool audio_manager_is_running(void)
{
    return s_ctx.running;
}

/**
 * @brief 检查是否正在录音
 * 
 * 检查是否正在录音。
 * 
 * @return true: 正在录音，false: 未录音
 */
bool audio_manager_is_recording(void)
{
    return s_ctx.recording;
}

/**
 * @brief 检查是否正在播放
 * 
 * 检查是否正在播放音频。
 * 
 * @return true: 正在播放，false: 未播放
 */
bool audio_manager_is_playing(void)
{
    return playback_controller_is_running(s_ctx.playback_ctrl);
}

audio_mgr_state_t audio_manager_get_state(void)
{
    return s_ctx.state;
}

/**
 * @brief 设置录音回调函数
 * 
 * 设置录音数据回调函数，当有录音数据时，会调用此回调函数。
 * 
 * @param callback 回调函数指针
 * @param user_ctx 用户上下文指针
 */
void audio_manager_set_record_callback(audio_record_callback_t callback, void *user_ctx)
{
    s_ctx.record_callback = callback;
    s_ctx.record_ctx = user_ctx;
}
