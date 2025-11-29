/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:20:05
 * @FilePath: \xn_esp32_audio\components\audio_manager\include\ring_buffer.h
 * @Description: 环形缓冲区 - 用于音频数据缓存
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 环形缓冲区句柄 */
typedef struct ring_buffer_s *ring_buffer_handle_t;

/**
 * @brief 创建环形缓冲区
 * @param samples 缓冲区容量（采样点数）
 * @param with_sem 是否使用信号量（用于阻塞读取）
 * @return 环形缓冲区句柄，失败返回NULL
 */
ring_buffer_handle_t ring_buffer_create(size_t samples, bool with_sem);

/**
 * @brief 销毁环形缓冲区
 * @param rb 环形缓冲区句柄
 */
void ring_buffer_destroy(ring_buffer_handle_t rb);

/**
 * @brief 写入数据到环形缓冲区
 * @param rb 环形缓冲区句柄
 * @param data 数据指针
 * @param samples 采样点数
 * @return 实际写入的采样点数
 * @note 如果缓冲区满，会覆盖旧数据
 */
size_t ring_buffer_write(ring_buffer_handle_t rb, const int16_t *data, size_t samples);

/**
 * @brief 从环形缓冲区读取数据
 * @param rb 环形缓冲区句柄
 * @param out 输出缓冲区
 * @param samples 期望读取的采样点数
 * @param timeout_ms 超时时间（毫秒），0表示不阻塞
 * @return 实际读取的采样点数
 */
size_t ring_buffer_read(ring_buffer_handle_t rb, int16_t *out, size_t samples, uint32_t timeout_ms);

/**
 * @brief 获取环形缓冲区中可用的数据量
 * @param rb 环形缓冲区句柄
 * @return 可用的采样点数
 */
size_t ring_buffer_available(ring_buffer_handle_t rb);

/**
 * @brief 清空环形缓冲区
 * @param rb 环形缓冲区句柄
 * @return ESP_OK 成功
 */
esp_err_t ring_buffer_clear(ring_buffer_handle_t rb);

/**
 * @brief 获取环形缓冲区的容量
 * @param rb 环形缓冲区句柄
 * @return 缓冲区容量（采样点数）
 */
size_t ring_buffer_get_size(ring_buffer_handle_t rb);

#ifdef __cplusplus
}
#endif

