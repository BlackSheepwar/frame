# =============================================================================
# Dev 层：设备抽象层
#
#   把"一段灯""一个按键"封装成设备，对外只暴露能力，不暴露引脚。
#   每个模块在自己的 .c 里实例化引脚句柄与设备句柄，头文件只做 extern 声明；
#   本层没有集中式板级文件。
#   头文件里出现了 bsp_gpio_t 指针，所以 bsp_gpio 必须 PUBLIC 传递，
#   否则上层拿到句柄也看不见结构体定义。
# =============================================================================

# -----------------------------------------------------------------------------
# dev_led_bar：连在同一端口上的一段灯
# -----------------------------------------------------------------------------
add_library(dev_led_bar STATIC
    Dev_LED_Bar/dev_led_bar.c
)
target_include_directories(dev_led_bar
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Dev_LED_Bar
)
target_link_libraries(dev_led_bar
    PRIVATE bsp_gpio
)
target_compile_options(dev_led_bar PRIVATE ${USER_WARN_FLAGS})

# -----------------------------------------------------------------------------
# dev_key：单个 GPIO 输入按键
# -----------------------------------------------------------------------------
add_library(dev_key STATIC
    Dev_Key/dev_key.c
)
target_include_directories(dev_key
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Dev_Key
)
target_link_libraries(dev_key
    PRIVATE bsp_gpio
)
target_compile_options(dev_key PRIVATE ${USER_WARN_FLAGS})

# -----------------------------------------------------------------------------
# dev_led：单个 LED
# -----------------------------------------------------------------------------
add_library(dev_led STATIC
    Dev_LED/dev_led.c
)
target_include_directories(dev_led
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Dev_LED
)
target_link_libraries(dev_led
    PRIVATE bsp_gpio
)
target_compile_options(dev_led PRIVATE ${USER_WARN_FLAGS})

# -----------------------------------------------------------------------------
# oled
# -----------------------------------------------------------------------------
add_library(dev_oled STATIC
    Dev_OLED/oled.c
    Dev_OLED/oled_extend.c
    Dev_OLED/font.c
)
target_include_directories(dev_oled
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Dev_OLED
)
target_link_libraries(dev_oled
    PRIVATE bsp_iic
)
target_compile_options(dev_oled PRIVATE ${USER_WARN_FLAGS})
