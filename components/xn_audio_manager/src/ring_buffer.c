/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:21:54
 * @FilePath: \xn_esp32_audio\components\audio_manager\src\ring_buffer.c
 * @Description: 环形缓冲区实现
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "ring_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "RING_BUFFER";

/** 
 * @brief 环形缓冲区结构体
 * 
 * 线程安全的环形缓冲区实现，用于音频数据的临时存储。
 * 特性：
 * - 使用 PSRAM 存储大容量音频数据
 * - 支持多线程并发访问（互斥锁保护）
 * - 可选的阻塞读取机制（信号量）
 * - 缓冲区满时自动覆盖旧数据
 */
typedef struct ring_buffer_s {
    int16_t *buffer;              ///< 数据缓冲区（PSRAM），存储音频采样点
    size_t size;                  ///< 缓冲区大小（采样点数）
    volatile size_t write_pos;    ///< 写位置索引（生产者）
    volatile size_t read_pos;     ///< 读位置索引（消费者）
    SemaphoreHandle_t mutex;      ///< 互斥锁，保护读写位置的原子性
    SemaphoreHandle_t data_sem;   ///< 数据可用信号量（可选），用于阻塞读取
} ring_buffer_t;

/**
 * @brief 创建环形缓冲区
 * 
 * 分配缓冲区内存（优先使用 PSRAM）并初始化同步机制。
 * 
 * @param samples 缓冲区容量（采样点数），建议值：16000（1秒@16kHz）
 * @param with_sem 是否创建信号量用于阻塞读取
 *                 - true: 支持 ring_buffer_read() 阻塞等待数据
 *                 - false: 仅支持非阻塞读取
 * 
 * @return 环形缓冲区句柄，失败返回 NULL
 * 
 * @note 失败原因可能包括：
 *       - samples == 0（无效参数）
 *       - 内存不足（PSRAM 或 IRAM）
 *       - 互斥锁/信号量创建失败
 */
ring_buffer_handle_t ring_buffer_create(size_t samples, bool with_sem)
{
    if (samples == 0) {
        ESP_LOGE(TAG, "无效的缓冲区大小");
        return NULL;
    }

    // 分配句柄结构体（使用 IRAM）
    ring_buffer_t *rb = (ring_buffer_t *)malloc(sizeof(ring_buffer_t));
    if (!rb) {
        ESP_LOGE(TAG, "环形缓冲区句柄分配失败");
        return NULL;
    }

    // 分配缓冲区内存（优先使用 PSRAM，降低 IRAM 压力）
    rb->buffer = (int16_t *)heap_caps_malloc(samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!rb->buffer) {
        ESP_LOGE(TAG, "环形缓冲区分配失败: %d samples", (int)samples);
        free(rb);
        return NULL;
    }

    // 初始化读写位置
    rb->size = samples;
    rb->write_pos = 0;
    rb->read_pos = 0;

    // 创建互斥锁（保护并发访问）
    rb->mutex = xSemaphoreCreateMutex();
    if (!rb->mutex) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        heap_caps_free(rb->buffer);
        free(rb);
        return NULL;
    }

    // 可选：创建数据可用信号量（用于阻塞读取）
    rb->data_sem = NULL;
    if (with_sem) {
        rb->data_sem = xSemaphoreCreateBinary();
        if (!rb->data_sem) {
            ESP_LOGE(TAG, "信号量创建失败");
            vSemaphoreDelete(rb->mutex);
            heap_caps_free(rb->buffer);
            free(rb);
            return NULL;
        }
    }

    ESP_LOGI(TAG, "环形缓冲区创建成功: %d samples (%.1f KB) at %s",
             (int)samples, 
             (samples * sizeof(int16_t)) / 1024.0f,
             esp_ptr_external_ram(rb->buffer) ? "PSRAM" : "IRAM");

    return rb;
}

/**
 * @brief 销毁环形缓冲区
 * 
 * 释放所有资源：
 * - 删除互斥锁和信号量
 * - 释放缓冲区内存（PSRAM）
 * - 释放句柄结构体
 * 
 * @param rb 环形缓冲区句柄，允许为 NULL（安全检查）
 * 
 * @note 调用前确保没有其他线程正在访问该缓冲区
 */
void ring_buffer_destroy(ring_buffer_handle_t rb)
{
    if (!rb) return;

    // 删除同步对象
    if (rb->mutex) {
        vSemaphoreDelete(rb->mutex);
    }
    if (rb->data_sem) {
        vSemaphoreDelete(rb->data_sem);
    }
    
    // 释放缓冲区内存
    if (rb->buffer) {
        heap_caps_free(rb->buffer);
    }
    
    // 释放句柄
    free(rb);
}

/**
 * @brief 写入数据到环形缓冲区
 * 
 * 将音频采样数据写入缓冲区。如果缓冲区满，会覆盖最旧的数据。
 * 
 * @param rb 环形缓冲区句柄
 * @param data 待写入的数据指针（int16_t 数组）
 * @param samples 采样点数
 * 
 * @return 实际写入的采样点数（通常等于 samples）
 * 
 * @note 线程安全：内部使用互斥锁保护
 * @note 缓冲区溢出时会打印警告日志
 * @note 写入后会触发 data_sem 信号量（如果存在）
 */
size_t ring_buffer_write(ring_buffer_handle_t rb, const int16_t *data, size_t samples)
{
    if (!rb || !data || samples == 0) {
        return 0;
    }

    // 获取互斥锁（超时 10ms）
    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }

    size_t overrun_count = 0;  // 记录被覆盖的样本数

    // 逐个写入采样点
    for (size_t i = 0; i < samples; i++) {
        rb->buffer[rb->write_pos] = data[i];
        rb->write_pos = (rb->write_pos + 1) % rb->size;

        // 检测缓冲区满：写指针追上读指针
        if (rb->write_pos == rb->read_pos) {
            rb->read_pos = (rb->read_pos + 1) % rb->size;  // 丢弃最旧数据
            overrun_count++;
        }
    }

    xSemaphoreGive(rb->mutex);

    // 缓冲区溢出警告（假设 16kHz 采样率）
    if (overrun_count > 0) {
        ESP_LOGW(TAG, "⚠️ 缓冲区溢出！丢弃 %u 样本 (%.1f ms)", 
                 (unsigned)overrun_count, 
                 (float)overrun_count / 16.0f);
    }

    // 通知有数据可读（触发阻塞读取）
    if (rb->data_sem) {
        xSemaphoreGive(rb->data_sem);
    }

    return samples;
}

/**
 * @brief 从环形缓冲区读取数据
 * 
 * 读取指定数量的音频采样数据。如果数据不足，返回实际可读的数量。
 * 
 * @param rb 环形缓冲区句柄
 * @param out 输出缓冲区（调用者分配）
 * @param samples 期望读取的采样点数
 * @param timeout_ms 超时时间（毫秒）
 *                   - 0: 非阻塞，立即返回
 *                   - >0: 阻塞等待数据（需要 data_sem 信号量）
 * 
 * @return 实际读取的采样点数（可能小于 samples）
 * 
 * @note 线程安全：内部使用互斥锁保护
 * @note 如果缓冲区为空且 timeout_ms > 0，会阻塞等待新数据
 */
size_t ring_buffer_read(ring_buffer_handle_t rb, int16_t *out, size_t samples, uint32_t timeout_ms)
{
    if (!rb || !out || samples == 0) {
        return 0;
    }

    // 如果缓冲区为空且有信号量，等待数据
    if (rb->read_pos == rb->write_pos && rb->data_sem && timeout_ms > 0) {
        xSemaphoreTake(rb->data_sem, pdMS_TO_TICKS(timeout_ms));
    }

    // 获取互斥锁（超时 10ms）
    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }

    // 计算可用数据量
    size_t avail = (rb->write_pos >= rb->read_pos)
                   ? (rb->write_pos - rb->read_pos)
                   : (rb->size - rb->read_pos + rb->write_pos);

    // 限制读取量为可用数据量
    if (samples > avail) {
        samples = avail;
    }

    // 读取数据
    for (size_t i = 0; i < samples; i++) {
        out[i] = rb->buffer[rb->read_pos];
        rb->read_pos = (rb->read_pos + 1) % rb->size;
    }

    xSemaphoreGive(rb->mutex);

    return samples;
}

/**
 * @brief 获取环形缓冲区中可用的数据量
 * 
 * 查询当前缓冲区中未读取的采样点数量。
 * 
 * @param rb 环形缓冲区句柄
 * @return 可用的采样点数
 * 
 * @note 线程安全：内部使用互斥锁保护
 * @note 返回值为瞬时快照，可能在返回后立即改变
 */
size_t ring_buffer_available(ring_buffer_handle_t rb)
{
    if (!rb) {
        return 0;
    }

    // 获取互斥锁（超时 10ms）
    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }

    // 计算可用数据量（处理环形回绕）
    size_t avail = (rb->write_pos >= rb->read_pos)
                   ? (rb->write_pos - rb->read_pos)
                   : (rb->size - rb->read_pos + rb->write_pos);

    xSemaphoreGive(rb->mutex);

    return avail;
}

/**
 * @brief 清空环形缓冲区
 * 
 * 重置读写位置，丢弃所有未读数据。
 * 
 * @param rb 环形缓冲区句柄
 * @return 
 *   - ESP_OK: 成功
 *   - ESP_ERR_INVALID_ARG: rb 为 NULL
 *   - ESP_ERR_TIMEOUT: 获取互斥锁超时
 * 
 * @note 线程安全：内部使用互斥锁保护
 * @note 不会清零缓冲区内存，只重置指针
 */
esp_err_t ring_buffer_clear(ring_buffer_handle_t rb)
{
    if (!rb) {
        return ESP_ERR_INVALID_ARG;
    }

    // 获取互斥锁（超时 100ms）
    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // 重置读写位置
    rb->read_pos = 0;
    rb->write_pos = 0;

    xSemaphoreGive(rb->mutex);

    return ESP_OK;
}

/**
 * @brief 获取环形缓冲区的容量
 * 
 * 返回缓冲区的总容量（创建时指定的 samples 参数）。
 * 
 * @param rb 环形缓冲区句柄
 * @return 缓冲区容量（采样点数），rb 为 NULL 时返回 0
 * 
 * @note 线程安全：size 字段创建后不会改变，无需加锁
 */
size_t ring_buffer_get_size(ring_buffer_handle_t rb)
{
    if (!rb) {
        return 0;
    }
    return rb->size;
}
