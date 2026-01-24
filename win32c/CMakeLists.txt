cmake_minimum_required(VERSION 3.20)

project(CANUpgradeTool VERSION 1.0.0 LANGUAGES C)

set(CMAKE_WIN32_EXECUTABLE ON)

# 启用 Unicode 支持
add_compile_definitions(UNICODE _UNICODE)

# 源文件
set(SOURCES
    src/main.c
    src/can_manager.c
)

# 资源文件
set(RESOURCES
    resources/resource.rc
)

# 创建可执行文件
add_executable(can-upgrade
    ${SOURCES}
    ${RESOURCES}
)

# 编译优化选项以减小可执行文件体积
if(MINGW)
    target_compile_options(can-upgrade PRIVATE
        -Os                    # 优化代码大小
        -ffunction-sections    # 将函数放入独立段
        -fdata-sections        # 将数据放入独立段
    )
    target_link_options(can-upgrade PRIVATE
        -mwindows              # Windows GUI 应用程序
        -static-libgcc         # 静态链接 libgcc
        -s                     # 去除符号
        -Wl,--gc-sections      # 删除未使用的段
        -Wl,--strip-all        # 去除所有符号
    )
endif()

# 链接 Windows 系统库
if(MINGW)
    target_link_libraries(can-upgrade PRIVATE
        -lcomctl32    # 通用控件库（进度条）
        -lcomdlg32    # 通用对话框库（文件选择）
        -lgdi32       # GDI 图形设备接口
    )
endif()

target_include_directories(can-upgrade PRIVATE include)

target_link_libraries(can-upgrade PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/x64/PCANBasic.lib"
)
