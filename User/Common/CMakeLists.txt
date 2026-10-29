# =============================================================================
# Common_Ringbuf：结构体环形缓冲（纯 C 实现，无外部依赖）
#
# - 目标名 = 模块目录名全小写：Common_Ringbuf → common_ringbuf
# - com_ringbuf.h 是对外接口，ringbuf_t 是完整类型（调用方可以内嵌，BSP 就这么用），
#   所以 include 目录走 PUBLIC，谁链 common_ringbuf 谁就能 #include "com_ringbuf.h"
# - 不链任何东西：com_ringbuf.h 只用到 <stdint.h> / <stddef.h>，
#   写 target_link_libraries 反而会把 HAL/OS 的 -I、-D 带进本层
# =============================================================================

add_library(common_ringbuf STATIC
    Common_Ringbuf/com_ringbuf.c
)

target_include_directories(common_ringbuf
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Common_Ringbuf
)

target_compile_options(common_ringbuf PRIVATE ${USER_WARN_FLAGS})
