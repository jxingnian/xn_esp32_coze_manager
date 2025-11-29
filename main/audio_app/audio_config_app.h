#pragma once

#include "audio_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 填充音频管理器配置
 *
 * 根据项目硬件脚位与默认语音链路，统一构建 audio_mgr_config_t。
 *
 * @param cfg         输出配置
 * @param event_cb    回调函数（比如 loopback 状态机）
 * @param user_ctx    回调上下文
 */
void audio_config_app_build(audio_mgr_config_t *cfg,
                            audio_mgr_event_cb_t event_cb,
                            void *user_ctx);

#ifdef __cplusplus
}
#endif


