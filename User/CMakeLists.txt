# =============================================================================
# User 顶层：按层收集子目录
#
#   BSP     第 1 层  硬件驱动层    GPIO 句柄与读写（引脚配置由 CubeMX 负责）
#   Device  第 2 层  设备抽象层    灯带、按键
#   Service 第 3 层  服务层        消抖、长按、连发
#   APP     第 4 层  应用层        档位业务：初始化 + 每步
#   TASK    任务层   线程体        一个任务一个 .c，覆盖 CubeMX 的 __weak 入口
#
# 分层规则由"链接关系"保证，不靠自觉：
#   每层的模块都是一个静态库，只链接下一层的模块。
#   Device 头文件里出现 bsp_gpio_t 指针，所以 bsp_gpio 是 PUBLIC 传递的，
#   上层能看见 BSP 的声明，但句柄里的端口号和引脚号由 BSP 独占。
#
# 线程由 CubeMX 的 .ioc 生成（osThreadNew 在 Core/Src/freertos.c），
# User/ 这边只写线程体和业务，不手写 osThreadNew。
# =============================================================================

# 统一编译选项：警告即错误（子目录里直接引用这个变量）
set(USER_WARN_FLAGS -Wall -Wextra -Werror)

add_subdirectory(Common)
add_subdirectory(BSP)
add_subdirectory(Device)
add_subdirectory(Service)
add_subdirectory(APP)
add_subdirectory(TASK)

target_link_libraries(${CMAKE_PROJECT_NAME} 
    app_key_cmd
    svc_key_logic
    dev_oled
    bsp_iic
    bsp_spi
    stm32cubemx
)
