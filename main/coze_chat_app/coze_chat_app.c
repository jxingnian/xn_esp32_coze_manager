/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-12 16:30:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-29 10:28:19
 * @FilePath: \xn_esp32_coze_manager\main\coze_chat_app\coze_chat_app.c
 * @Description: Coze聊天应用程序实现文件（基于espressif/esp_coze组件）
 *
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "coze_chat.h"
// #include "audio_manager.h"
// #include "lottie_manager.h"

static const char *TAG = "COZE_CHAT_APP";

// Coze配置
#ifndef CONFIG_COZE_BOT_ID
#define CONFIG_COZE_BOT_ID "7550222162704547880"
#endif

#ifndef CONFIG_COZE_ACCESS_TOKEN
#define CONFIG_COZE_ACCESS_TOKEN "sat_EnWEk9OwkxmQ4flAO3hAB6Np8O9Ilhz2uJ3cmteoM1GMjZjQobRFSgo7mGX0pEpO"
#endif

// 全局Coze句柄
static coze_chat_handle_t g_coze_chat = NULL;

// 静态存储用户ID（生命周期贯穿整个程序，避免栈变量被释放）
static char s_user_id[32] = {0};

/**
 * @brief Coze WebSocket 事件回调（防止空指针崩溃）
 */
static void coze_ws_event_callback(coze_ws_event_t *event)
{
    if (!event || !event->handle) {
        return;
    }

    // 只记录关键事件，避免过多日志
    if (event->event_id == COZE_WS_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "WebSocket已连接");
    } else if (event->event_id == COZE_WS_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "WebSocket已断开");
    } else if (event->event_id == COZE_WS_EVENT_ERROR) {
        ESP_LOGE(TAG, "WebSocket错误");
    }
}

/**
 * @brief Coze事件回调函数
 *
 * 处理Coze各种事件：聊天创建、开始说话、停止说话、错误等
 *
 * @param event 事件类型
 * @param data 事件数据（可能为NULL）
 * @param ctx 用户上下文（未使用）
 */
static void coze_event_callback(coze_chat_event_t event, char *data, void *ctx)
{
    switch (event) {
    case COZE_CHAT_EVENT_CHAT_CREATE:
        ESP_LOGI(TAG, "🎬 Coze会话已创建");
        break;

    case COZE_CHAT_EVENT_CHAT_UPDATE:
        ESP_LOGI(TAG, "🔄 Coze会话已更新");
        break;

    case COZE_CHAT_EVENT_CHAT_COMPLETED:
        ESP_LOGI(TAG, "✅ Coze会话已完成");
        break;

    case COZE_CHAT_EVENT_CHAT_SPEECH_STARTED:
        ESP_LOGI(TAG, "🗣️ Coze开始说话");
        break;

    case COZE_CHAT_EVENT_CHAT_SPEECH_STOPED:
        ESP_LOGI(TAG, "🤐 Coze停止说话");
        break;

    case COZE_CHAT_EVENT_CHAT_ERROR:
        ESP_LOGE(TAG, "❌ Coze错误");
        break;

    case COZE_CHAT_EVENT_INPUT_AUDIO_BUFFER_COMPLETED:
        ESP_LOGI(TAG, "🎤 音频缓冲区处理完成");
        break;

    case COZE_CHAT_EVENT_CHAT_SUBTITLE_EVENT:
        // 字幕事件（显示 Coze 返回的文字）
        break;

    case COZE_CHAT_EVENT_CHAT_CUSTOMER_DATA:
        // 自定义数据事件
        if (data) {
            ESP_LOGI(TAG, "📦 自定义数据: %s", data);
        }
        break;

    default:
        ESP_LOGW(TAG, "⚠️ 未知事件类型: %d", event);
        break;
    }
}

/**
 * @brief Coze音频数据回调函数（带流控）
 *
 * 接收Coze组件返回的音频数据（已解码的PCM格式），带流控地送到播放器
 *
 * ⚠️ 注意：组件已在内部完成 Opus → PCM 解码，这里收到的是PCM数据！
 *
 * @param data 音频数据指针（PCM格式，int16_t，16kHz单声道）
 * @param len 数据长度（字节数，需除以2得到样本数）
 * @param ctx 用户上下文（未使用）
 */
static void coze_audio_callback(char *data, int len, void *ctx)
{
    if (!data || len <= 0) {
        ESP_LOGW(TAG, "⚠️ 收到空音频数据");
        return;
    }

    // // 组件已解码为PCM，len是字节数，样本数 = len / sizeof(int16_t) = len / 2
    // size_t samples = len / sizeof(int16_t);

    // // ✅ 流控机制：检查播放缓冲区可用空间，避免溢出
    // size_t free_space = audio_manager_get_playback_free_space();
    
    // // 如果剩余空间不足，延迟发送（自适应）
    // // 阈值：保留至少 32K样本（2秒）的缓冲空间
    // const size_t MIN_FREE_SPACE = 32 * 1024;  // 32K样本 = 2秒 @ 16kHz
    
    // if (free_space < MIN_FREE_SPACE) {
    //     // 计算需要延迟的时间：让播放器消耗一些数据
    //     // 延迟时间 = 当前包大小的播放时长
    //     uint32_t delay_ms = (samples * 1000) / 16000;  // 样本数 → 毫秒
        
    //     ESP_LOGD(TAG, "🔒 播放缓冲区空间不足(%zu样本)，延迟%ums", 
    //              free_space, delay_ms);
        
    //     vTaskDelay(pdMS_TO_TICKS(delay_ms));
    // }

    // audio_manager_play_audio((int16_t *)data, samples);
}

/**
 * @brief 初始化Coze聊天应用程序
 *
 * @return esp_err_t
 *         - ESP_OK: 成功
 *         - 其他: 失败
 */
esp_err_t coze_chat_app_init(void)
{
    esp_err_t ret;

    // 生成唯一的用户ID（基于MAC地址）
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_user_id, sizeof(s_user_id), "ESP32_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "======== Coze配置信息 ========");
    ESP_LOGI(TAG, "用户ID: %s", s_user_id);
    ESP_LOGI(TAG, "Bot ID: %s", CONFIG_COZE_BOT_ID);
    ESP_LOGI(TAG, "访问Token: %s", CONFIG_COZE_ACCESS_TOKEN[0] ? "已配置" : "未配置");
    ESP_LOGI(TAG, "==============================");

    // 打印内存状态
    ESP_LOGI(TAG, "========== 初始化前内存状态 ==========");
    ESP_LOGI(TAG, "总堆内存: %lu 字节", esp_get_free_heap_size() + (heap_caps_get_total_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    ESP_LOGI(TAG, "可用堆内存: %lu 字节", esp_get_free_heap_size());
    ESP_LOGI(TAG, "最小可用堆: %lu 字节", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "内部RAM可用: %lu 字节", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM可用: %lu 字节", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "最大可分配块(内部): %lu 字节", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "最大可分配块(PSRAM): %lu 字节", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "======================================");

    // 配置Coze聊天参数
    // ⚠️ 注意：使用USB RNDIS后，4G和WiFi在应用层完全统一，都使用WiFi配置
    coze_chat_config_t chat_config = COZE_CHAT_DEFAULT_CONFIG_WIFI();
    
    // ========== Coze基本配置 ==========
    chat_config.bot_id = CONFIG_COZE_BOT_ID;
    chat_config.access_token = CONFIG_COZE_ACCESS_TOKEN;
    chat_config.enable_subtitle = true;  // 启用字幕，查看更多事件信息
    chat_config.user_id = s_user_id;
    chat_config.voice_id = "7426720361733144585";

    // ========== VAD模式配置 ==========
    // 选项1：服务器端VAD - 自动检测说话开始/结束（推荐）
    // chat_config.turn_detection_type = COZE_TURN_DETECTION_SERVER_VAD;

    // 选项2：客户端打断模式 - 需要手动调用 coze_chat_send_audio_complete()
    chat_config.turn_detection_type = COZE_TURN_DETECTION_CLIENT_INTERRUPT;

    // 音频格式
    // 上行：Opus格式（大幅减少4G网络传输量，提升实时性）
    // 下行：Opus格式（节省带宽）
    chat_config.uplink_audio_type = COZE_CHAT_AUDIO_TYPE_OPUS;  // ✅ 启用Opus上行
    chat_config.downlink_audio_type = COZE_CHAT_AUDIO_TYPE_OPUS;

    // WebSocket 缓冲区配置（按键模式不需要太大）
    chat_config.websocket_buffer_size = 8192;  // 8KB（按键模式足够）

    // 任务栈配置（⚠️ JSON解析任务需要足够的栈空间处理Opus解码和cJSON解析）
    // JSON缓冲区已移至PSRAM，但仍需足够栈空间用于函数调用链
    chat_config.pull_task_stack_size = 16384;  // 16KB（参考旧组件配置）
    chat_config.push_task_stack_size = 4096;   // 4KB（发送音频足够）
    // 使用内部 RAM（更快的栈操作，JSON解析任务优先使用内部RAM）
    chat_config.pull_task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    chat_config.push_task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

    // 回调函数（⚠️ 必须设置 ws_event_callback 防止空指针）
    chat_config.audio_callback = coze_audio_callback;
    chat_config.event_callback = coze_event_callback;
    chat_config.ws_event_callback = coze_ws_event_callback;  // ⚠️ 关键：防止崩溃

    // 初始化Coze聊天
    ret = coze_chat_init(&chat_config, &g_coze_chat);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Coze聊天初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 启动Coze聊天（连接WebSocket）
    ret = coze_chat_start(g_coze_chat);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Coze聊天启动失败: %s", esp_err_to_name(ret));
        coze_chat_deinit(g_coze_chat);
        g_coze_chat = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "✅ Coze聊天应用初始化成功");
    return ESP_OK;
}

/**
 * @brief 反初始化Coze聊天应用程序
 *
 * @return esp_err_t
 *         - ESP_OK: 成功
 *         - 其他: 失败
 */
esp_err_t coze_chat_app_deinit(void)
{
    // 停止Coze聊天（组件会自动清理内部的Opus解码器）
    if (g_coze_chat) {
        coze_chat_stop(g_coze_chat);
        coze_chat_deinit(g_coze_chat);
        g_coze_chat = NULL;
        ESP_LOGI(TAG, "✅ Coze聊天应用已反初始化");
    }

    return ESP_OK;
}

/**
 * @brief 获取Coze聊天句柄（供其他模块使用）
 *
 * @return coze_chat_handle_t Coze聊天句柄
 */
coze_chat_handle_t coze_chat_get_handle(void)
{
    return g_coze_chat;
}

