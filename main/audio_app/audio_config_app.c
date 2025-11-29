/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 21:48:21
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-28 21:35:27
 * @FilePath: \xn_esp32_audio\main\audio_config_app.c
 * @Description: 音频配置 - 统一构建音频管理器配置参数
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "audio_config_app.h"

/**
 * @brief 构建音频管理器配置
 * 
 * 根据硬件引脚定义和默认音频处理参数，填充完整的音频管理器配置结构体。
 * 配置包括：
 * - 硬件配置：麦克风/扬声器的 I2S 引脚、采样率、位深等
 * - 唤醒词配置：是否启用、灵敏度、超时时间等
 * - VAD 配置：语音活动检测的模式和阈值
 * - AFE 配置：回声消除、降噪、自动增益等音频前端处理
 * 
 * @param cfg       [out] 输出的音频管理器配置结构体
 * @param event_cb  [in]  事件回调函数（处理状态机事件）
 * @param user_ctx  [in]  用户上下文指针（传递给回调函数）
 */
void audio_config_app_build(audio_mgr_config_t *cfg,
                            audio_mgr_event_cb_t event_cb,
                            void *user_ctx)
{
    if (!cfg) {
        return;
    }

    // 使用默认配置初始化
    *cfg = AUDIO_MANAGER_DEFAULT_CONFIG();

    // ========== 麦克风硬件配置 ==========
    cfg->hw_config.mic.port = 1;              // I2S 端口 1
    cfg->hw_config.mic.bclk_gpio = 15;        // 位时钟引脚
    cfg->hw_config.mic.lrck_gpio = 2;         // 左右声道时钟引脚
    cfg->hw_config.mic.din_gpio = 39;         // 数据输入引脚
    cfg->hw_config.mic.sample_rate = 16000;   // 采样率 16kHz
    cfg->hw_config.mic.bits = 32;             // 32 位采样深度

    // ========== 扬声器硬件配置 ==========
    cfg->hw_config.speaker.port = 0;          // I2S 端口 0
    cfg->hw_config.speaker.bclk_gpio = 48;    // 位时钟引脚
    cfg->hw_config.speaker.lrck_gpio = 38;    // 左右声道时钟引脚
    cfg->hw_config.speaker.dout_gpio = 47;    // 数据输出引脚
    cfg->hw_config.speaker.sample_rate = 16000; // 采样率 16kHz
    cfg->hw_config.speaker.bits = 16;         // 16 位采样深度

    // ========== 按键配置 ==========
    cfg->hw_config.button.gpio = 0;           // 按键 GPIO 0
    cfg->hw_config.button.active_low = true;  // 低电平有效

    // ========== 唤醒词配置 ==========
    cfg->wakeup_config.enabled = false;       // 默认禁用唤醒词
    cfg->wakeup_config.wake_word_name = "小鸭小鸭";  // 唤醒词名称
    cfg->wakeup_config.model_partition = "model";    // 模型分区名称
    cfg->wakeup_config.sensitivity = 2;              // 灵敏度：中等
    cfg->wakeup_config.wakeup_timeout_ms = 8000;     // 唤醒超时 8 秒
    cfg->wakeup_config.wakeup_end_delay_ms = 1200;   // 说话结束延迟 1.2 秒

    // ========== VAD（语音活动检测）配置 ==========
    cfg->vad_config.enabled = true;           // 启用 VAD
    cfg->vad_config.vad_mode = 2;             // VAD 模式 2（中等灵敏度）
    cfg->vad_config.min_speech_ms = 200;      // 最小语音持续时间 200ms
    cfg->vad_config.min_silence_ms = 400;     // 最小静音持续时间 400ms

    // ========== AFE（音频前端处理）配置 ==========
    cfg->afe_config.aec_enabled = true;       // 启用回声消除（AEC）
    cfg->afe_config.ns_enabled = true;        // 启用降噪（NS）
    cfg->afe_config.agc_enabled = true;       // 启用自动增益控制（AGC）
    cfg->afe_config.afe_mode = 1;             // AFE 模式：高质量

    // ========== 回调配置 ==========
    cfg->event_callback = event_cb;           // 设置事件回调函数
    cfg->user_ctx = user_ctx;                 // 设置用户上下文
}
