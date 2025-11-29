/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:21:36
 * @FilePath: \xn_esp32_audio\components\audio_manager\src\playback_controller.c
 * @Description: 播放控制模块实现
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "playback_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PLAYBACK_CTRL";

/**
 * @brief 播放控制器上下文结构体
 * 
 * 存储播放控制器的所有状态信息和资源句柄
 */
typedef struct playback_controller_s {
    audio_bsp_handle_t bsp_handle;                  ///< BSP 句柄，用于音频输出
    ring_buffer_handle_t playback_rb;               ///< 播放缓冲区，存储待播放的音频数据
    ring_buffer_handle_t reference_rb;              ///< 回采缓冲区，存储回采的音频数据供AFE使用
    TaskHandle_t playback_task;                     ///< 播放任务句柄，用于管理播放任务
    bool running;                                   ///< 运行状态标志，true表示正在运行
    size_t frame_samples;                           ///< 每帧采样点数，用于分配帧缓冲区
    playback_reference_callback_t reference_callback; ///< 回采回调函数，用于将音频数据传递给AFE
    void *reference_ctx;                            ///< 回采回调上下文，传递给回调函数的用户数据
    uint8_t *volume_ptr;                            ///< 音量指针，指向音量值（0-100）
} playback_controller_t;

/**
 * @brief 播放任务函数
 * 
 * 从播放缓冲区读取音频数据，先回采给AFE，再输出到扬声器
 * 
 * @param arg 播放控制器上下文指针
 */
static void playback_task(void *arg)
{
    playback_controller_t *ctrl = (playback_controller_t *)arg;
    
    // 分配帧缓冲区，用于存储从环形缓冲区读取的音频数据
    int16_t *frame = (int16_t *)malloc(ctrl->frame_samples * sizeof(int16_t));
    if (!frame) {
        ESP_LOGE(TAG, "播放任务内存分配失败");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "播放任务启动");

    // 主循环：持续从播放缓冲区读取数据并播放
    while (ctrl->running) {
        // 从播放缓冲区读取一帧音频数据，超时时间200ms
        size_t got = ring_buffer_read(ctrl->playback_rb, frame, ctrl->frame_samples, 200);

        if (got > 0) {
            // 先回采给 AFE（通过回调或写入缓冲区）
            // 回采的目的是让AFE能够处理播放的音频，用于回声消除等功能
            if (ctrl->reference_callback) {
                // 如果设置了回调函数，直接调用回调函数传递音频数据
                ctrl->reference_callback(frame, got, ctrl->reference_ctx);
            } else {
                // 否则将音频数据写入回采缓冲区，供AFE读取
                ring_buffer_write(ctrl->reference_rb, frame, got);
            }

            // 再播放音频数据到扬声器
            // 获取音量值，如果未设置音量指针则使用默认值80
            uint8_t volume = ctrl->volume_ptr ? *ctrl->volume_ptr : 80;
            // 通过 BSP 将音频数据写入扬声器
            audio_bsp_write_speaker(ctrl->bsp_handle, frame, got, volume);
        }
    }

    // 清理资源
    free(frame);
    ESP_LOGI(TAG, "播放任务结束");
    vTaskDelete(NULL);
}

/**
 * @brief 创建播放控制器
 * 
 * 根据配置参数创建并初始化播放控制器
 * 
 * @param config 配置参数指针
 * @return 播放控制器句柄，失败返回NULL
 */
playback_controller_handle_t playback_controller_create(const playback_controller_config_t *config)
{
    // 参数校验
    if (!config || !config->bsp_handle) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }

    // 分配播放控制器内存
    playback_controller_t *ctrl = (playback_controller_t *)calloc(1, sizeof(playback_controller_t));
    if (!ctrl) {
        ESP_LOGE(TAG, "播放控制器分配失败");
        return NULL;
    }

    // 初始化配置参数
    ctrl->bsp_handle = config->bsp_handle;
    ctrl->frame_samples = config->frame_samples;
    ctrl->reference_callback = config->reference_callback;
    ctrl->reference_ctx = config->reference_ctx;
    ctrl->volume_ptr = config->volume_ptr;

    // 创建播放缓冲区（阻塞模式）
    ctrl->playback_rb = ring_buffer_create(config->playback_buffer_samples, true);
    if (!ctrl->playback_rb) {
        ESP_LOGE(TAG, "播放缓冲区创建失败");
        free(ctrl);
        return NULL;
    }

    // 创建回采缓冲区（非阻塞模式）
    ctrl->reference_rb = ring_buffer_create(config->reference_buffer_samples, false);
    if (!ctrl->reference_rb) {
        ESP_LOGE(TAG, "回采缓冲区创建失败");
        ring_buffer_destroy(ctrl->playback_rb);
        free(ctrl);
        return NULL;
    }

    ESP_LOGI(TAG, "✅ 播放控制器创建成功");
    return ctrl;
}

/**
 * @brief 销毁播放控制器
 * 
 * 停止播放任务并释放所有资源
 * 
 * @param controller 播放控制器句柄
 */
void playback_controller_destroy(playback_controller_handle_t controller)
{
    if (!controller) return;

    // 先停止播放任务
    playback_controller_stop(controller);

    // 销毁播放缓冲区
    if (controller->playback_rb) {
        ring_buffer_destroy(controller->playback_rb);
    }

    // 销毁回采缓冲区
    if (controller->reference_rb) {
        ring_buffer_destroy(controller->reference_rb);
    }

    // 释放控制器内存
    free(controller);
    ESP_LOGI(TAG, "播放控制器已销毁");
}

/**
 * @brief 启动播放控制器
 * 
 * 创建并启动播放任务
 * 
 * @param controller 播放控制器句柄
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t playback_controller_start(playback_controller_handle_t controller)
{
    if (!controller) {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果已经在运行，直接返回
    if (controller->running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "▶️ 启动播放器");
    controller->running = true;

    // 创建播放任务，固定到 Core 1
    // 任务优先级7，栈大小5KB
    xTaskCreatePinnedToCore(playback_task, "playback", 5 * 1024, controller, 
                            7, &controller->playback_task, 1);

    return ESP_OK;
}

/**
 * @brief 停止播放控制器
 * 
 * 停止播放任务
 * 
 * @param controller 播放控制器句柄
 * @return ESP_OK 成功
 */
esp_err_t playback_controller_stop(playback_controller_handle_t controller)
{
    if (!controller || !controller->running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "⏹️ 停止播放器");
    controller->running = false;

    // 等待任务结束
    if (controller->playback_task) {
        vTaskDelay(pdMS_TO_TICKS(300));
        controller->playback_task = NULL;
    }

    return ESP_OK;
}

/**
 * @brief 写入音频数据到播放缓冲区
 * 
 * 将PCM音频数据写入播放缓冲区，供播放任务读取
 * 
 * @param controller 播放控制器句柄
 * @param pcm_data PCM音频数据指针
 * @param sample_count 采样点数
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t playback_controller_write(playback_controller_handle_t controller, 
                                     const int16_t *pcm_data, size_t sample_count)
{
    if (!controller || !pcm_data || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 将音频数据写入播放缓冲区
    ring_buffer_write(controller->playback_rb, pcm_data, sample_count);
    return ESP_OK;
}

/**
 * @brief 清空播放缓冲区
 * 
 * 清空播放缓冲区和回采缓冲区中的所有数据
 * 
 * @param controller 播放控制器句柄
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t playback_controller_clear(playback_controller_handle_t controller)
{
    if (!controller) {
        return ESP_ERR_INVALID_ARG;
    }

    // 清空播放缓冲区
    esp_err_t ret = ring_buffer_clear(controller->playback_rb);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🗑️ 已清空播放缓冲区");
    }

    // 清空回采缓冲区
    ring_buffer_clear(controller->reference_rb);
    return ret;
}

/**
 * @brief 检查播放控制器是否正在运行
 * 
 * @param controller 播放控制器句柄
 * @return true 正在运行，false 未运行或参数无效
 */
bool playback_controller_is_running(playback_controller_handle_t controller)
{
    return controller ? controller->running : false;
}

/**
 * @brief 获取播放缓冲区可用空间
 * 
 * 用于流控：让解码任务根据可用空间决定是否延迟
 * 
 * @param controller 播放控制器句柄
 * @return 可用空间（样本数）
 */
size_t playback_controller_get_free_space(playback_controller_handle_t controller)
{
    if (!controller || !controller->playback_rb) {
        return 0;
    }
    
    // 计算可用空间 = 总容量 - 已占用
    size_t total_size = ring_buffer_get_size(controller->playback_rb);
    size_t used_size = ring_buffer_available(controller->playback_rb);
    
    return (total_size > used_size) ? (total_size - used_size) : 0;
}

/**
 * @brief 获取回采缓冲区句柄
 * 
 * 返回回采缓冲区句柄，供AFE读取回采的音频数据
 * 
 * @param controller 播放控制器句柄
 * @return 回采缓冲区句柄，参数无效返回NULL
 */
ring_buffer_handle_t playback_controller_get_reference_buffer(playback_controller_handle_t controller)
{
    return controller ? controller->reference_rb : NULL;
}

