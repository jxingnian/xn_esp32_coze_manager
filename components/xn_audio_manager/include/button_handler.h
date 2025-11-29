/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:19:15
 * @FilePath: \xn_esp32_audio\components\audio_manager\include\button_handler.h
 * @Description: 按键处理模块 - 管理按键中断和事件
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 按键事件类型 */
typedef enum {
    BUTTON_EVENT_PRESS,     ///< 按键按下
    BUTTON_EVENT_RELEASE,   ///< 按键松开
} button_event_type_t;

/** 按键事件回调函数类型 */
typedef void (*button_event_callback_t)(button_event_type_t event, void *user_ctx);

/** 按键处理器句柄 */
typedef struct button_handler_s *button_handler_handle_t;

/** 按键配置 */
typedef struct {
    int gpio;                           ///< 按键 GPIO
    bool active_low;                    ///< 低电平有效
    uint32_t debounce_ms;               ///< 防抖时间（毫秒）
    button_event_callback_t callback;   ///< 事件回调
    void *user_ctx;                     ///< 用户上下文
} button_handler_config_t;

/**
 * @brief 创建按键处理器
 * @param config 配置参数
 * @return 按键处理器句柄，失败返回 NULL
 */
button_handler_handle_t button_handler_create(const button_handler_config_t *config);

/**
 * @brief 销毁按键处理器
 * @param handler 按键处理器句柄
 */
void button_handler_destroy(button_handler_handle_t handler);

/**
 * @brief 检查按键是否按下
 * @param handler 按键处理器句柄
 * @return true 按下
 */
bool button_handler_is_pressed(button_handler_handle_t handler);

#ifdef __cplusplus
}
#endif

