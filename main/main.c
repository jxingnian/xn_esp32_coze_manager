/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-29 10:40:58
 * @FilePath: \xn_esp32_coze_manager\main\main.c
 * @Description: esp32 网页WiFi配网 By.星年
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "xn_wifi_manage.h"

static const char *TAG = "app";

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
    loopback_ctx_t *ctx = (loopback_ctx_t *)user_ctx;
    
    // 参数有效性检查
    if (!ctx || !ctx->capturing || !pcm_data || sample_count == 0) {
        return;
    }

    // 计算缓冲区剩余空间
    size_t remain = ctx->max_samples - ctx->used_samples;
    if (remain == 0) {
        return;  // 缓冲区已满，丢弃新数据
    }

    // 计算实际可复制的采样点数（不超过剩余空间）
    size_t to_copy = sample_count > remain ? remain : sample_count;
    
    // 将PCM数据复制到缓冲区
    memcpy(ctx->buffer + ctx->used_samples, pcm_data, to_copy * sizeof(int16_t));
    ctx->used_samples += to_copy;  // 更新已使用的采样点数
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
    loopback_ctx_t *ctx = (loopback_ctx_t *)user_ctx;
    
    // 参数有效性检查
    if (!ctx || !event) {
        return;
    }

    switch (event->type) {
    case AUDIO_MGR_EVENT_VAD_START:
        // VAD检测到语音开始
        ESP_LOGI(TAG, "VAD start, begin capture");
        break;

    case AUDIO_MGR_EVENT_VAD_END:
        // VAD检测到语音结束
        break;

    case AUDIO_MGR_EVENT_WAKEUP_TIMEOUT:
        // 唤醒超时（在唤醒后未检测到有效语音）
        ESP_LOGW(TAG, "wake window timeout, discard recording");
        break;

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
    // WiFi配网功能（已注释）
    // printf("esp32 网页WiFi配网 By.星年\n");
    // esp_err_t ret = wifi_manage_init(NULL);
    // (void)ret; 
    
    // 初始化音频管理器
    ESP_LOGI(TAG, "init audio manager");
    ESP_ERROR_CHECK(audio_manager_init(&audio_cfg));
    
    // 设置播放音量为100%
    audio_manager_set_volume(100);
    
    // 注册录音数据回调函数，传入回环测试上下文
    audio_manager_set_record_callback(loopback_record_cb, &s_loop_ctx);
    
    // 启动音频管理器（开始录音和VAD检测）
    ESP_ERROR_CHECK(audio_manager_start());
    
    // 启动播放任务（保持播放任务常驻，随时准备播放数据）
    ESP_ERROR_CHECK(audio_manager_start_playback());
}
