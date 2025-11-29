idf_component_register(
    SRCS 
        "src/audio_manager.c"
        "src/audio_bsp.c"
        "src/ring_buffer.c"
        "src/i2s_hal.c"
        "src/playback_controller.c"
        "src/button_handler.c"
        "src/afe_wrapper.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES 
        esp-sr
        gmf_ai_audio
        driver
        esp_timer
        mbedtls
    PRIV_REQUIRES
        freertos
)

