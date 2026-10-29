# =============================================================================
# APP 层：应用层
#
#   业务只有两种入口：Init（调度器启动前）和"一步"（线程里调用），
#   线程由 CubeMX 的 .ioc 配置、线程体在 User/TASK 里，本层不建线程、不碰内核。
#
#   一个模块一件事，目录名 = 模块名，文件名 = 模块名小写：
#     APP_Level/app_level.c          档位与显示（业务状态）
#     APP_Key_Cmd/app_key_cmd.c    按键命令表（事件 → 动作）
#
#   依赖方向单向：app_key_cmd → app（调 APP_Level_Step），反向没有边。
#
#   头文件可见性：
#     app_level.h 不外泄任何下层类型，所以 svc_key_logic 与 dev_led_bar 都用
#     PRIVATE：需要事件类型的是 app_key_cmd（它自己 PUBLIC 链 svc_key_logic），
#     不是 app。TASK 层链 app_key_cmd 就能看见 svc_key_logic.h。
#     少一条 PUBLIC，上层就少一层"可以不经过我就能拿到下层"的机会。
# =============================================================================

# -----------------------------------------------------------------------------
# app_level：档位与显示
# -----------------------------------------------------------------------------
add_library(app_level STATIC
    APP_Level/app_level.c
)
target_include_directories(app_level
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/APP_Level
)
target_link_libraries(app_level
    PRIVATE dev_led_bar
)
target_compile_options(app_level PRIVATE ${USER_WARN_FLAGS})

# -----------------------------------------------------------------------------
# app_key_cmd：按键命令表
# -----------------------------------------------------------------------------
add_library(app_key_cmd STATIC
    APP_Key_Cmd/app_key_cmd.c
)
target_include_directories(app_key_cmd
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/APP_Key_Cmd
)
target_link_libraries(app_key_cmd
    PRIVATE svc_key_logic
            app_level
            dev_led
)
target_compile_options(app_key_cmd PRIVATE ${USER_WARN_FLAGS})
