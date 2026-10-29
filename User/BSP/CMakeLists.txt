# =============================================================================
# BSP 层：硬件驱动层
#
#   每个模块一个静态库，只暴露自己的头文件目录给上层；
#   这一层直接操作寄存器，必须能看见 HAL，所以链接 stm32cubemx。
# =============================================================================

# -----------------------------------------------------------------------------
# bsp_gpio：GPIO 寄存器级读写（IDR / BSRR）
# -----------------------------------------------------------------------------
add_library(bsp_gpio STATIC
    BSP_GPIO/bsp_gpio.c
)
target_include_directories(bsp_gpio
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/BSP_GPIO
)
target_link_libraries(bsp_gpio
    PRIVATE stm32cubemx
)
target_compile_options(bsp_gpio PRIVATE ${USER_WARN_FLAGS})

# -----------------------------------------------------------------------------
# bsp_iic：IIC 模块（总线互斥量排队 + 完成中断唤醒，依赖 FreeRTOS）
# -----------------------------------------------------------------------------
add_library(bsp_iic STATIC
    BSP_IIC/bsp_iic.c
)
target_include_directories(bsp_iic
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/BSP_IIC
)
target_link_libraries(bsp_iic
    PRIVATE stm32cubemx
)
target_compile_options(bsp_iic PRIVATE ${USER_WARN_FLAGS})
