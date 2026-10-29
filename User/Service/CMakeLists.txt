# =============================================================================
# Svc 层：服务层
#
#   存放"设备怎么用"的业务规则：消抖、长按、连发。
#   不碰寄存器，只调用 Dev 层暴露的设备句柄与函数。
#
#   按键事件投递出口只有一个：svc_key_logic.c 的 Svc_Key_Logic_SendEvent()。
#   它投的是 CubeMX 生成的消息队列 KEY_EVT_Q（frame.ioc 的 Queues01）。
#   想换后端（环形缓冲、直接回调）只改那一个函数体，不要另建并行组件：
#   并行的"备选实现"没人调用，既占维护成本又容易和真实路径走偏。
#
#   头文件里出现了 dev_key_t 指针（事件按设备句柄回传），所以 dev_key 必须
#   PUBLIC 传递：上层只拿到 svc_key_logic 的头文件路径，看不见 Dev_Key 目录，
#   就会报 dev_key.h: No such file or directory。
# =============================================================================

# -----------------------------------------------------------------------------
# 消抖服务
# -----------------------------------------------------------------------------
add_library(svc_key_logic STATIC
    Svc_Key_Logic/svc_key_logic.c
)
target_include_directories(svc_key_logic
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Svc_Key_Logic
)
target_link_libraries(svc_key_logic
    PRIVATE stm32cubemx
    PUBLIC  dev_key
)
target_compile_options(svc_key_logic PRIVATE ${USER_WARN_FLAGS})
