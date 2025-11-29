/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-27 19:21:10
 * @FilePath: \xn_esp32_audio\components\audio_manager\src\button_handler.c
 * @Description: 按键处理模块实现
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "button_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "BUTTON_HANDLER";

/**
 * @brief 按键处理器上下文结构体
 * 
 * 存储按键处理器的所有状态信息，包括：
 * - GPIO 配置参数
 * - 防抖时间
 * - 事件回调函数
 * - 任务和队列句柄
 * - 按键状态历史
 */
typedef struct button_handler_s {
    int gpio;                           ///< 按键 GPIO 引脚号
    bool active_low;                    ///< 是否为低电平有效（true=低电平有效，false=高电平有效）
    uint32_t debounce_ms;               ///< 防抖时间（毫秒），用于消除按键抖动
    button_event_callback_t callback;   ///< 按键事件回调函数指针
    void *user_ctx;                     ///< 用户上下文指针，传递给回调函数
    TaskHandle_t button_task;           ///< 按键处理任务句柄
    QueueHandle_t button_queue;         ///< 按键事件队列句柄（用于 ISR 到任务通信）
    int64_t last_press_time;            ///< 上次按键事件时间戳（毫秒），用于防抖
    bool last_state;                    ///< 上次按键状态（true=按下，false=松开），用于检测状态变化
} button_handler_t;

/**
 * @brief 按键 GPIO 中断服务程序（ISR）
 * 
 * 当按键 GPIO 发生边沿变化时，由硬件触发此中断。
 * 此函数运行在中断上下文中，必须快速返回，因此只负责将事件发送到队列。
 * 
 * @param arg 用户参数，指向 button_handler_t 结构体
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_handler_t *handler = (button_handler_t *)arg;
    uint32_t gpio_num = handler->gpio;
    
    // 将 GPIO 号发送到队列，由按键任务处理
    // 使用 FromISR 版本，因为这是在中断上下文中调用
    xQueueSendFromISR(handler->button_queue, &gpio_num, NULL);
}

/**
 * @brief 按键处理任务
 * 
 * 此任务从队列接收按键中断事件，进行防抖处理后，检测按键状态变化并触发回调。
 * 任务栈分配在 PSRAM 中，以节省内部 RAM。
 * 
 * @param arg 用户参数，指向 button_handler_t 结构体
 */
static void button_task(void *arg)
{
    button_handler_t *handler = (button_handler_t *)arg;
    uint32_t io_num;

    // 无限循环，持续监听按键事件
    while (1) {
        // 从队列接收按键事件，阻塞等待
        if (xQueueReceive(handler->button_queue, &io_num, portMAX_DELAY)) {
            // 获取当前时间戳（毫秒）
            int64_t current_time = esp_timer_get_time() / 1000;

            // 防抖处理：如果距离上次事件时间太短，忽略此次事件
            // 这可以消除按键机械抖动产生的多次中断
            if (current_time - handler->last_press_time < handler->debounce_ms) {
                continue;
            }
            handler->last_press_time = current_time;

            // 读取 GPIO 电平
            int level = gpio_get_level(handler->gpio);
            
            // 根据 active_low 配置判断按键是否按下
            // active_low=true: 低电平(0)表示按下
            // active_low=false: 高电平(1)表示按下
            bool pressed = handler->active_low ? (level == 0) : (level == 1);

            // 检测按键按下事件：当前按下且上次未按下
            if (pressed && !handler->last_state) {
                ESP_LOGI(TAG, "🔘 按键按下");
                if (handler->callback) {
                    handler->callback(BUTTON_EVENT_PRESS, handler->user_ctx);
                }
                handler->last_state = true;
            }
            // 检测按键松开事件：当前未按下且上次按下
            else if (!pressed && handler->last_state) {
                ESP_LOGI(TAG, "🔘 按键松开");
                if (handler->callback) {
                    handler->callback(BUTTON_EVENT_RELEASE, handler->user_ctx);
                }
                handler->last_state = false;
            }
        }
    }
}

/**
 * @brief 创建按键处理器
 * 
 * 初始化按键处理器的所有资源，包括：
 * 1. 分配上下文内存
 * 2. 配置 GPIO 为输入模式并设置中断
 * 3. 创建事件队列
 * 4. 安装 GPIO ISR 服务
 * 5. 创建按键处理任务（栈在 PSRAM）
 * 
 * @param config 按键配置参数指针
 * @return 按键处理器句柄，失败返回 NULL
 */
button_handler_handle_t button_handler_create(const button_handler_config_t *config)
{
    // 参数检查
    if (!config || !config->callback) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }

    // 分配按键处理器上下文内存
    button_handler_t *handler = (button_handler_t *)calloc(1, sizeof(button_handler_t));
    if (!handler) {
        ESP_LOGE(TAG, "按键处理器分配失败");
        return NULL;
    }

    // 保存配置参数
    handler->gpio = config->gpio;
    handler->active_low = config->active_low;
    handler->debounce_ms = config->debounce_ms;
    handler->callback = config->callback;
    handler->user_ctx = config->user_ctx;
    handler->last_state = false;

    // ========== 配置 GPIO ==========
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,  // 双边沿触发中断（按下和松开都触发）
        .mode = GPIO_MODE_INPUT,          // 输入模式
        .pin_bit_mask = (1ULL << config->gpio),  // 设置 GPIO 位掩码
        // 根据 active_low 配置上拉/下拉电阻
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败: %s", esp_err_to_name(ret));
        free(handler);
        return NULL;
    }

    // ========== 创建事件队列 ==========
    // 队列用于 ISR 和任务之间的通信，容量为 10 个事件
    handler->button_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!handler->button_queue) {
        ESP_LOGE(TAG, "按键队列创建失败");
        free(handler);
        return NULL;
    }

    // ========== 安装 GPIO ISR 服务 ==========
    // 使用静态变量确保只安装一次（多个按键共享同一个 ISR 服务）
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        ret = gpio_install_isr_service(0);
        // ESP_ERR_INVALID_STATE 表示已经安装过，可以忽略
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "GPIO ISR 服务安装失败: %s", esp_err_to_name(ret));
            vQueueDelete(handler->button_queue);
            free(handler);
            return NULL;
        }
        isr_service_installed = true;
    }

    // ========== 添加 GPIO ISR 处理器 ==========
    ret = gpio_isr_handler_add(config->gpio, button_isr_handler, handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR 处理器添加失败: %s", esp_err_to_name(ret));
        vQueueDelete(handler->button_queue);
        free(handler);
        return NULL;
    }

    // ========== 创建按键处理任务 ==========
    // 使用静态任务分配，任务栈分配在 PSRAM 中以节省内部 RAM
    
    // 分配任务控制块（TCB），必须在内部 RAM
    StaticTask_t *btn_tcb = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    // 分配任务栈，在 PSRAM 中分配 4KB
    StackType_t *btn_stack = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!btn_tcb || !btn_stack) {
        ESP_LOGE(TAG, "❌ 按键任务内存分配失败");
        if (btn_tcb) heap_caps_free(btn_tcb);
        if (btn_stack) heap_caps_free(btn_stack);
        gpio_isr_handler_remove(config->gpio);
        vQueueDelete(handler->button_queue);
        free(handler);
        return NULL;
    }
    
    // 创建静态任务
    handler->button_task = xTaskCreateStatic(
        button_task,                    // 任务函数
        "button_task",                  // 任务名称
        4096 / sizeof(StackType_t),     // 栈大小（以 StackType_t 为单位）
        handler,                        // 任务参数
        4,                              // 任务优先级
        btn_stack,                      // 栈指针
        btn_tcb                         // 任务控制块指针
    );
    
    if (!handler->button_task) {
        ESP_LOGE(TAG, "❌ 创建按键任务失败");
        heap_caps_free(btn_tcb);
        heap_caps_free(btn_stack);
        gpio_isr_handler_remove(config->gpio);
        vQueueDelete(handler->button_queue);
        free(handler);
        return NULL;
    }

    ESP_LOGI(TAG, "✅ 按键处理器创建成功（GPIO %d, 栈 4KB 在 PSRAM）", config->gpio);
    return handler;
}

/**
 * @brief 销毁按键处理器
 * 
 * 释放按键处理器的所有资源，包括：
 * 1. 删除按键处理任务
 * 2. 移除 GPIO ISR 处理器
 * 3. 删除事件队列
 * 4. 释放上下文内存
 * 
 * @param handler 按键处理器句柄
 */
void button_handler_destroy(button_handler_handle_t handler)
{
    if (!handler) return;

    // 删除按键处理任务
    if (handler->button_task) {
        vTaskDelete(handler->button_task);
    }

    // 移除 GPIO ISR 处理器
    gpio_isr_handler_remove(handler->gpio);

    // 删除事件队列
    if (handler->button_queue) {
        vQueueDelete(handler->button_queue);
    }

    // 释放上下文内存
    free(handler);
    ESP_LOGI(TAG, "按键处理器已销毁");
}

/**
 * @brief 查询按键当前是否按下
 * 
 * 直接读取 GPIO 电平，不经过防抖处理。
 * 适用于需要实时查询按键状态的场景。
 * 
 * @param handler 按键处理器句柄
 * @return true 表示按键按下，false 表示按键松开
 */
bool button_handler_is_pressed(button_handler_handle_t handler)
{
    if (!handler) return false;

    // 读取 GPIO 电平
    int level = gpio_get_level(handler->gpio);
    
    // 根据 active_low 配置判断是否按下
    return handler->active_low ? (level == 0) : (level == 1);
}

