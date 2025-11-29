/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-11-29 14:35:57
 * @FilePath: \xn_esp32_coze_manager\main\main.c
 * @Description: esp32 网页WiFi配网 By.星年
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "xn_wifi_manage.h"
#include "audio_manager.h"
#include "coze_chat.h"
#include "coze_chat_app.h"
#include "audio_app/audio_config_app.h"

static const char *TAG = "app";

extern coze_chat_handle_t coze_chat_get_handle(void);

/**
 * @brief 录音数据回调函数
 * 
 * @param pcm_data 采集到的PCM数据指针（16位有符号整数）
 * @param sample_count PCM数据采样点数
 * @param user_ctx 用户上下文指针（指向loopback_ctx_t）
 */
static void loopback_record_cb(const int16_t *pcm_data,
                               size_t sample_count,
                               void *user_ctx)
{
    (void)user_ctx;

    coze_chat_handle_t handle = coze_chat_get_handle();
    if (!handle || !pcm_data || sample_count == 0) {
        return;
    }

    int len_bytes = (int)(sample_count * sizeof(int16_t));
    esp_err_t ret = coze_chat_send_audio_data(handle, (char *)pcm_data, len_bytes);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "send audio to Coze failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief 音频管理器事件回调函数
 * 
 * 处理音频管理器产生的各种事件（唤醒、VAD开始/结束、按键等），
 * 驱动录音→播放的状态流转
 * 
 * @param event 音频事件指针
 * @param user_ctx 用户上下文指针（指向loopback_ctx_t）
 */
static void audio_event_cb(const audio_mgr_event_t *event, void *user_ctx)
{
    (void)user_ctx;

    if (!event) {
        return;
    }

    switch (event->type) {
    case AUDIO_MGR_EVENT_VAD_START:
        // VAD检测到语音开始
        ESP_LOGI(TAG, "VAD start, begin capture");
        break;

    case AUDIO_MGR_EVENT_VAD_END: {
        // VAD检测到语音结束，通知 Coze 结束一轮语音输入
        ESP_LOGI(TAG, "VAD end, send audio complete to Coze");
        coze_chat_handle_t handle = coze_chat_get_handle();
        if (handle) {
            coze_chat_send_audio_complete(handle);
        }
        break;
    }

    case AUDIO_MGR_EVENT_WAKEUP_TIMEOUT: {
        // 唤醒超时（在唤醒后未检测到有效语音）
        ESP_LOGW(TAG, "wake window timeout, cancel Coze audio");
        coze_chat_handle_t handle = coze_chat_get_handle();
        if (handle) {
            coze_chat_send_audio_cancel(handle);
        }
        break;
    }

    case AUDIO_MGR_EVENT_BUTTON_TRIGGER:
        // 按键触发录音
        ESP_LOGI(TAG, "button trigger, force capture");
        break;

    default:
        break;
    }
}

/**
 * @brief 应用程序主入口函数
 * 
 * 初始化音频管理器，配置录音回调，启动音频采集和播放任务
 */
void app_main(void)
{
    printf("esp32 网页WiFi配网 By.星年\n");
    esp_err_t ret = wifi_manage_init(NULL);
    (void)ret; 
    
    // 构建音频管理器配置
    audio_mgr_config_t audio_cfg = {0};
    audio_config_app_build(&audio_cfg, audio_event_cb, NULL);

    // 初始化音频管理器
    ESP_LOGI(TAG, "init audio manager");
    ESP_ERROR_CHECK(audio_manager_init(&audio_cfg));
    
    // 设置播放音量为100%
    audio_manager_set_volume(100);
    
    // 初始化 Coze Chat
    ESP_LOGI(TAG, "init Coze chat app");
    ESP_ERROR_CHECK(coze_chat_app_init());
    
    // 注册录音数据回调，将麦克风PCM送入 Coze
    audio_manager_set_record_callback(loopback_record_cb, NULL);
    
    // 启动播放任务（保持播放任务常驻，随时准备播放数据）
    ESP_ERROR_CHECK(audio_manager_start_playback());
    
    // 启动音频管理器（开始录音和VAD检测）
    ESP_ERROR_CHECK(audio_manager_start());
}
