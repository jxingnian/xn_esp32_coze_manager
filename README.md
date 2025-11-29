<!--
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-29 09:50:35
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-29 19:17:19
 * @FilePath: \xn_esp32_coze_manager\README.md
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
-->

# xn_esp32_coze_manager

基于 ESP32-S3 + Coze 实时语音的语音助手示例工程。
集成了：

- Web WiFi 配网（AP + Web 页面）
- Coze 实时语音聊天（基于 `xn_coze_chat` / `esp_coze` 组件）
- 音频采集与播放管理（`xn_audio_manager`）
- VAD / 唤醒 / 按键触发等事件驱动

> 适合作为 ESP32 语音对话设备（台灯、音箱、机器人等）的模板工程。

## 功能特性

- **WiFi 管理**
  - 自动重连、多路由轮询
  - 内置 AP：默认 SSID `XN-ESP32-AP`，密码 `12345678`
  - Web 配网页面：连接 AP 后访问 `http://192.168.4.1` 进行配网
- **Coze 实时语音**
  - 通过 WebSocket 与 Coze 平台建立双向通道
  - 上行语音流：从麦克风采集 PCM，经 `coze_chat_send_audio_data` 送入 Coze
  - 下行语音流：Coze 返回的 PCM 由 `audio_manager` 播放
  - 支持客户端打断模式、字幕事件、用户自定义数据等
- **音频管理**
  - 统一管理录音、播放、VAD 与唤醒逻辑
  - 录音数据回调 `loopback_record_cb` 直接推送到 Coze
  - VAD 结束 / 唤醒超时等事件会调用 Coze 的 `send_audio_complete` / `send_audio_cancel`

## 工程结构

- `main/`
  - `main.c`：应用入口，初始化 WiFi 管理、音频管理，并与 Coze 应用对接
  - `coze_chat_app/`
    - `coze_chat_app.c`：封装 Coze 相关配置与生命周期（init / start / stop）
    - `coze_chat_app.h`
  - `audio_app/`
    - `audio_config_app.c`：构建 `audio_mgr_config_t`，配置音频通路与事件回调
- `components/`
  - `xn_web_wifi_manger/`：WiFi 自动管理 + AP + Web 配网，导出 `xn_wifi_manage.h`
  - `xn_coze_chat/`：Coze 实时语音组件，封装网络 / 编码 / 事件回调
  - `xn_audio_manager/`：音频采集与播放通用组件
- `sdkconfig.defaults`：默认芯片、Flash、PSRAM、分区表等配置（目标芯片 ESP32-S3）
- `CMakeLists.txt`：ESP-IDF CMake 工程入口与组件配置

## 硬件与软件环境

- **硬件**
  - ESP32-S3 开发板（16MB Flash，带 PSRAM）
  - 板载或外接麦克风 + 扬声器（由 `xn_audio_manager` 组件接管）
- **软件**
  - 已安装 ESP-IDF 开发环境
  - Python 3.x
  - Coze 平台账号，一个已开启实时语音能力的 Bot

## 快速开始

- **1. 克隆工程**

  ```bash
  git clone <this_repo_url>
  cd xn_esp32_coze_manager
  ```

- **2. 设置 ESP-IDF 目标芯片（仅首次）**

  ```bash
  idf.py set-target esp32s3
  ```

- **3. （可选）加载默认配置并进入菜单**

  ```bash
  idf.py menuconfig
  ```

  - 根据需要调整：
    - Flash 大小 / 模式
    - PSRAM 使用方式
    - Audio / Coze 相关配置

- **4. 编译工程**

  ```bash
  idf.py build
  ```

- **5. 烧录 & 监视串口（Windows 示例）**

  ```bash
  idf.py -p COMx flash monitor
  ```

  将 `COMx` 替换为实际串口号，如 `COM3`。

## WiFi 配网说明

1. 首次上电或未能连接已保存路由时：
   - 设备自动启动 AP + Web 配网；
   - 默认 AP 信息见 `xn_wifi_manage.h` 中 `WIFI_MANAGE_DEFAULT_CONFIG`：
     - SSID：`XN-ESP32-AP`
     - 密码：`12345678`
     - AP IP：`192.168.4.1`

2. 使用手机 / PC 连接到该 AP，浏览器访问：

   ```text
   http://192.168.4.1
   ```

3. 按网页提示输入家中路由器的 SSID / 密码并保存。

4. 设备重启或自动重连成功后，会通过 `app_wifi_event_cb` 回调触发 `coze_chat_app_init()`，开始连接 Coze 服务。

## Coze 配置

`main/coze_chat_app/coze_chat_app.c` 中提供了以下配置项（可被 sdkconfig / menuconfig 覆盖）：

- `CONFIG_COZE_BOT_ID`
- `CONFIG_COZE_ACCESS_TOKEN`

使用步骤建议：

1. 在 Coze 平台创建 / 选择你的 Bot，确认其支持实时语音。
2. 复制 Bot ID 与访问 Token。
3. 在本工程中配置：
   - 方法一：在 `menuconfig` 中增加 / 修改同名配置项；
   - 方法二：直接修改 `coze_chat_app.c` 顶部的宏定义（不推荐将真实 Token 提交到公开仓库）。

> 实际使用时，请务必将自己的 Access Token 保存在私密环境变量或本地配置中，不要提交到 GitHub 等公共仓库。

## 运行时行为简述

- 设备连上 WiFi 后：
  - `wifi_manage` 调用 `app_wifi_event_cb(WIFI_MANAGE_STATE_CONNECTED)`；
  - `coze_chat_app_init()` 根据 MAC 地址生成 `user_id`，配置 Coze，并建立 WebSocket 连接。
- 音频路径：
  - `audio_manager` 采集麦克风 PCM，回调 `loopback_record_cb()`；
  - 回调中调用 `coze_chat_send_audio_data()` 上传音频；
  - VAD 结束事件触发 `coze_chat_send_audio_complete()`，一轮语音输入结束；
  - Coze 返回 TTS 音频，由 `coze_audio_callback()` 写入播放缓冲区，扬声器播出。

## 自定义与扩展

- **修改音频参数 / 硬件接线**：
  - 编辑 `audio_app/audio_config_app.c`，根据实际 Codec / 引脚调整 I2S / TDM 配置。
- **调整 WiFi 行为**：
  - 修改 `wifi_manage_config_t` 或 `WIFI_MANAGE_DEFAULT_CONFIG` 中的重连策略、AP 信息、Web 端口等。
- **更改 Bot 行为**：
  - 在 Coze 控制台中调整 Bot Prompt、工具调用、语言 / 情感参数等。
  - 在工程中修改 `coze_chat_config_t` 相关字段（如 `voice_id`、字幕开关等）。

## 许可与声明

- 本工程主要用于学习与个人项目演示，你可以在遵守 Coze 与 ESP-IDF 相关协议的前提下自由修改和使用。
- 若你在产品中使用本工程，建议：
  - 自行审查内存 / 稳定性 / 安全性；
  - 替换为自己的 Coze 凭证与业务逻辑.
