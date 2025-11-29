/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:21:22
 * @FilePath: \xn_esp32_audio\components\audio_manager\src\i2s_hal.c
 * @Description: I2S 硬件抽象层实现
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "i2s_hal.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "I2S_HAL";

/**
 * @brief I2S HAL 上下文结构体
 * 
 * 存储 I2S 硬件抽象层的所有状态信息，包括：
 * - TX 和 RX 通道句柄
 * - 立体声转换缓冲区（用于单声道到立体声的转换）
 * - 麦克风临时缓冲区（预分配，避免频繁 malloc/free）
 */
typedef struct i2s_hal_s {
    i2s_chan_handle_t tx_handle;    ///< 扬声器（TX）通道句柄
    i2s_chan_handle_t rx_handle;    ///< 麦克风（RX）通道句柄
    int16_t *stereo_buffer;         ///< 立体声转换缓冲区（PSRAM），用于单声道到立体声转换
    size_t stereo_buffer_size;      ///< 立体声缓冲区大小（采样点数）
    int32_t *mic_temp_buffer;       ///< 麦克风临时缓冲区（PSRAM），用于32位数据读取
    size_t mic_temp_buffer_size;    ///< 麦克风临时缓冲区大小（采样点数）
    uint8_t mic_bit_shift;          ///< 32位转16位的右移位数（默认14，可调12-16）
} i2s_hal_t;

/**
 * @brief 创建 I2S HAL 实例
 * 
 * 初始化 I2S 的 TX（扬声器）和 RX（麦克风）通道，并分配必要的资源。
 * 
 * @param mic_config 麦克风配置参数（采样率、GPIO 引脚等）
 * @param speaker_config 扬声器配置参数（采样率、GPIO 引脚、最大帧大小等）
 * @return i2s_hal_handle_t 成功返回句柄，失败返回 NULL
 * 
 * @note 初始化顺序：先 TX 后 RX，失败时自动清理已分配的资源
 */
i2s_hal_handle_t i2s_hal_create(const i2s_mic_config_t *mic_config, 
                                 const i2s_speaker_config_t *speaker_config)
{
    // 参数有效性检查
    if (!mic_config || !speaker_config) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }

    // 分配 HAL 上下文内存
    i2s_hal_t *hal = (i2s_hal_t *)calloc(1, sizeof(i2s_hal_t));
    if (!hal) {
        ESP_LOGE(TAG, "HAL 上下文分配失败");
        return NULL;
    }

    // ========== 初始化 TX（扬声器）通道 ==========
    // 配置 TX 通道参数：使用主模式，自动清除 DMA 缓冲区
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(speaker_config->port, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;  // 自动清除 DMA 缓冲区，避免播放残留数据

    // 创建 TX 通道
    esp_err_t ret = i2s_new_channel(&tx_chan_cfg, &hal->tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 TX 通道失败: %s", esp_err_to_name(ret));
        free(hal);
        return NULL;
    }

    // 配置 TX 标准模式：16位立体声，Philips 格式
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(speaker_config->sample_rate),  // 时钟配置
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),  // 16位立体声
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,  // 主时钟不使用
            .bclk = speaker_config->bclk_gpio,  // 位时钟 GPIO
            .ws   = speaker_config->lrck_gpio,  // 字选择（左右声道）GPIO
            .dout = speaker_config->dout_gpio,  // 数据输出 GPIO
            .din  = GPIO_NUM_NC,  // 数据输入不使用
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },  // 不反转信号
        },
    };

    // 初始化 TX 通道为标准模式
    ret = i2s_channel_init_std_mode(hal->tx_handle, &tx_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 TX 失败: %s", esp_err_to_name(ret));
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    // 使能 TX 通道
    ret = i2s_channel_enable(hal->tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能 TX 失败: %s", esp_err_to_name(ret));
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    ESP_LOGI(TAG, "I2S TX 初始化成功: 端口%d, BCLK=%d, LRCK=%d, DOUT=%d",
             speaker_config->port, speaker_config->bclk_gpio,
             speaker_config->lrck_gpio, speaker_config->dout_gpio);

    // ========== 初始化 RX（麦克风）通道 ==========
    // 配置 RX 通道参数：使用主模式
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(mic_config->port, I2S_ROLE_MASTER);

    // 创建 RX 通道
    ret = i2s_new_channel(&rx_chan_cfg, NULL, &hal->rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 RX 通道失败: %s", esp_err_to_name(ret));
        // 清理已创建的 TX 通道
        i2s_channel_disable(hal->tx_handle);
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    // 配置 RX 标准模式：32位单声道，Philips 格式
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(mic_config->sample_rate),  // 时钟配置
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),  // 32位单声道
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,  // 主时钟不使用
            .bclk = mic_config->bclk_gpio,  // 位时钟 GPIO
            .ws   = mic_config->lrck_gpio,  // 字选择（左右声道）GPIO
            .dout = GPIO_NUM_NC,  // 数据输出不使用
            .din  = mic_config->din_gpio,  // 数据输入 GPIO
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },  // 不反转信号
        },
    };
    // 设置接收右声道数据（单声道麦克风通常使用右声道）
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

    // 初始化 RX 通道为标准模式
    ret = i2s_channel_init_std_mode(hal->rx_handle, &rx_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 RX 失败: %s", esp_err_to_name(ret));
        i2s_del_channel(hal->rx_handle);
        i2s_channel_disable(hal->tx_handle);
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    // 使能 RX 通道
    ret = i2s_channel_enable(hal->rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能 RX 失败: %s", esp_err_to_name(ret));
        i2s_del_channel(hal->rx_handle);
        i2s_channel_disable(hal->tx_handle);
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    ESP_LOGI(TAG, "I2S RX 初始化成功: 端口%d, BCLK=%d, LRCK=%d, DIN=%d",
             mic_config->port, mic_config->bclk_gpio,
             mic_config->lrck_gpio, mic_config->din_gpio);

    // ========== 分配麦克风临时缓冲区（PSRAM）==========
    // 用于存储 32-bit 原始数据，避免频繁 malloc/free
    hal->mic_temp_buffer_size = mic_config->max_frame_samples > 0 ? 
                                 mic_config->max_frame_samples : 512;  // 默认 512
    hal->mic_temp_buffer = (int32_t *)heap_caps_malloc(
        hal->mic_temp_buffer_size * sizeof(int32_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // 使用 PSRAM
    
    if (!hal->mic_temp_buffer) {
        ESP_LOGE(TAG, "麦克风临时缓冲区分配失败");
        // 清理已创建的资源
        i2s_channel_disable(hal->rx_handle);
        i2s_del_channel(hal->rx_handle);
        i2s_channel_disable(hal->tx_handle);
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    // 保存右移位数配置
    hal->mic_bit_shift = (mic_config->bit_shift >= 12 && mic_config->bit_shift <= 16) ? 
                          mic_config->bit_shift : 14;  // 默认 14

    ESP_LOGI(TAG, "✅ 麦克风临时缓冲区初始化: %d samples (%.1f KB) at PSRAM, 右移 %d 位",
             hal->mic_temp_buffer_size,
             (hal->mic_temp_buffer_size * sizeof(int32_t)) / 1024.0f,
             hal->mic_bit_shift);

    // ========== 分配立体声转换缓冲区（PSRAM）==========
    // 缓冲区大小：最大帧采样数 × 2（左右声道）× sizeof(int16_t)
    hal->stereo_buffer_size = speaker_config->max_frame_samples;
    hal->stereo_buffer = (int16_t *)heap_caps_malloc(
        hal->stereo_buffer_size * 2 * sizeof(int16_t), 
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // 使用 PSRAM 分配，节省内部 RAM
    
    if (!hal->stereo_buffer) {
        ESP_LOGE(TAG, "立体声缓冲区分配失败");
        // 清理已创建的资源
        if (hal->mic_temp_buffer) heap_caps_free(hal->mic_temp_buffer);
        i2s_channel_disable(hal->rx_handle);
        i2s_del_channel(hal->rx_handle);
        i2s_channel_disable(hal->tx_handle);
        i2s_del_channel(hal->tx_handle);
        free(hal);
        return NULL;
    }

    ESP_LOGI(TAG, "✅ 立体声缓冲区初始化: %d samples (%.1f KB) at PSRAM",
             hal->stereo_buffer_size * 2, 
             (hal->stereo_buffer_size * 2 * sizeof(int16_t)) / 1024.0f);

    return hal;
}

/**
 * @brief 销毁 I2S HAL 实例
 * 
 * 释放所有分配的资源，包括：
 * - 禁用并删除 RX 和 TX 通道
 * - 释放立体声转换缓冲区
 * - 释放 HAL 上下文内存
 * 
 * @param hal I2S HAL 句柄
 */
void i2s_hal_destroy(i2s_hal_handle_t hal)
{
    if (!hal) return;

    // 禁用并删除 RX 通道
    if (hal->rx_handle) {
        i2s_channel_disable(hal->rx_handle);
        i2s_del_channel(hal->rx_handle);
    }

    // 禁用并删除 TX 通道
    if (hal->tx_handle) {
        i2s_channel_disable(hal->tx_handle);
        i2s_del_channel(hal->tx_handle);
    }

    // 释放麦克风临时缓冲区（PSRAM）
    if (hal->mic_temp_buffer) {
        heap_caps_free(hal->mic_temp_buffer);
    }

    // 释放立体声转换缓冲区（PSRAM）
    if (hal->stereo_buffer) {
        heap_caps_free(hal->stereo_buffer);
    }

    // 释放 HAL 上下文内存
    free(hal);
    ESP_LOGI(TAG, "I2S HAL 已销毁");
}

/**
 * @brief 从麦克风读取音频数据
 * 
 * 从 I2S RX 通道读取 32 位音频数据，并转换为 16 位输出。
 * 使用预分配的缓冲区，避免频繁 malloc/free。
 * 
 * @param hal I2S HAL 句柄
 * @param out_samples 输出缓冲区（16位）
 * @param sample_count 期望读取的采样点数
 * @param out_got 实际读取的采样点数（可选）
 * @return esp_err_t ESP_OK 成功，其他值表示错误
 * 
 * @note 数据格式转换：32位右移可配置位数（默认14）得到16位数据
 * @note 根据 MSM261S4030H0R 数据手册：24-bit 有效数据在 32-bit 字中
 */
esp_err_t i2s_hal_read_mic(i2s_hal_handle_t hal, int16_t *out_samples, 
                           size_t sample_count, size_t *out_got)
{
    // 参数有效性检查
    if (!hal || !hal->rx_handle || !out_samples || !hal->mic_temp_buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查是否超出预分配缓冲区大小
    if (sample_count > hal->mic_temp_buffer_size) {
        ESP_LOGE(TAG, "请求采样数 %d 超出缓冲区大小 %d", 
                 sample_count, hal->mic_temp_buffer_size);
        return ESP_ERR_INVALID_ARG;
    }

    // 从 I2S RX 通道读取 32 位数据（使用预分配的缓冲区）
    size_t bytes32 = sample_count * sizeof(int32_t);
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(hal->rx_handle, hal->mic_temp_buffer, 
                                      bytes32, &bytes_read, 100);

    // 将 32 位数据转换为 16 位
    // 根据数据手册：24-bit 有效数据 + 8-bit 低位填充
    // 右移位数可配置，以适应不同的音量需求
    size_t got = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < got; i++) {
        out_samples[i] = (int16_t)(hal->mic_temp_buffer[i] >> hal->mic_bit_shift);
    }

    if (out_got) *out_got = got;
    return ret;
}

/**
 * @brief 向扬声器写入音频数据
 * 
 * 将单声道音频数据转换为立体声并写入 I2S TX 通道。
 * 支持音量控制（0-100）。
 * 
 * @param hal I2S HAL 句柄
 * @param samples 输入音频数据（16位单声道）
 * @param sample_count 采样点数
 * @param volume 音量（0-100）
 * @return esp_err_t ESP_OK 成功，其他值表示错误
 * 
 * @note 转换过程：
 *       1. 检查缓冲区大小
 *       2. 应用音量控制
 *       3. 单声道复制到左右声道
 *       4. 写入 I2S TX 通道
 */
esp_err_t i2s_hal_write_speaker(i2s_hal_handle_t hal, const int16_t *samples, 
                                 size_t sample_count, uint8_t volume)
{
    // 参数有效性检查
    if (!hal || !hal->tx_handle || !samples || !hal->stereo_buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    // 防止缓冲区溢出：检查采样点数是否超过缓冲区大小
    if (sample_count > hal->stereo_buffer_size) {
        ESP_LOGE(TAG, "❌ 样本数超出限制: %d > %d", sample_count, hal->stereo_buffer_size);
        return ESP_ERR_INVALID_ARG;
    }

    // 单声道 -> 立体声转换，并应用音量控制
    // 音量因子：将 0-100 映射到 0.0-1.0
    float factor = (volume > 100 ? 100 : volume) / 100.0f;
    for (size_t i = 0; i < sample_count; i++) {
        int16_t v = (int16_t)(samples[i] * factor);  // 应用音量
        hal->stereo_buffer[i * 2] = v;      // Left 声道
        hal->stereo_buffer[i * 2 + 1] = v;  // Right 声道
    }

    // 写入 I2S TX 通道
    size_t written = 0;
    size_t bytes_to_write = sample_count * 2 * sizeof(int16_t);  // 立体声字节数
    esp_err_t ret = i2s_channel_write(hal->tx_handle, hal->stereo_buffer, 
                                      bytes_to_write, &written, portMAX_DELAY);

    // 检查写入结果
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2S 写入失败: %s (期望%d字节)", esp_err_to_name(ret), bytes_to_write);
        return ret;
    }

    // 检查是否完整写入
    if (written < bytes_to_write) {
        ESP_LOGW(TAG, "⚠️ I2S 写入不完整: 期望%d, 实际%d", bytes_to_write, written);
    }

    return ESP_OK;
}

/**
 * @brief 获取 RX 通道句柄
 * 
 * @param hal I2S HAL 句柄
 * @return i2s_chan_handle_t RX 通道句柄，失败返回 NULL
 */
i2s_chan_handle_t i2s_hal_get_rx_handle(i2s_hal_handle_t hal)
{
    return hal ? hal->rx_handle : NULL;
}

/**
 * @brief 获取 TX 通道句柄
 * 
 * @param hal I2S HAL 句柄
 * @return i2s_chan_handle_t TX 通道句柄，失败返回 NULL
 */
i2s_chan_handle_t i2s_hal_get_tx_handle(i2s_hal_handle_t hal)
{
    return hal ? hal->tx_handle : NULL;
}

