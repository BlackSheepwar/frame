# =============================================================================
# TASK 层：任务体
#
#   CubeMX 在 .ioc 里配置任务（名字、优先级、栈、静态分配），
#   生成的 freertos.c 负责 osThreadNew，并给出与任务同名的 __weak 空入口；
#   这里为每个任务提供同名强定义，覆盖那个空入口。
#
#   所以：加任务 = 先在 CubeMX 里加（入口名与函数同名、As weak），
#   再加一个 task_xxx.c。不用手写 osThreadNew，也不用改 freertos.c。
#
#   任务体直接平铺在 TASK/ 下，不按任务再分子目录：
#     TASK/task_key_scan.c     Task_Key_Scan     10 ms 采样，产出按键事件
#     TASK/task_key_cmd.c      Task_Key_Cmd      阻塞收事件，执行按键业务
#   一个任务一个文件，文件名就是任务名的小写下划线形式，一眼能对上。
#
#   为什么用 OBJECT 库再挂到 frame 上，而不是 STATIC 库：
#     静态库里的强符号如果没有被引用，链接器会整段丢掉，而 freertos.c 里的
#     __weak 空实现恰好"满足"了 osThreadNew 的引用，归档成员根本不会被拉进来，
#     结果任务线程空转。目标文件是强制链接的，强定义必定覆盖 __weak。
# =============================================================================

add_library(task_objects OBJECT
    task_key_scan.c
    task_key_cmd.c
    task_oled.c
)

target_link_libraries(task_objects 
    PRIVATE stm32cubemx
            app_key_cmd
            svc_key_logic
            dev_oled
)

target_compile_options(task_objects PRIVATE ${USER_WARN_FLAGS})

target_sources(${CMAKE_PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:task_objects>)
