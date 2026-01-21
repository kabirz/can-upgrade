cmake_minimum_required(VERSION 3.16)

project(can_upgrade VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialBus LinguistTools)

set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/canmanager.cpp
    src/canmanager.h
)

set(TS_FILES
    translations/can_upgrade_zh_CN.ts
)

qt_add_executable(can_upgrade
    ${PROJECT_SOURCES}
)

qt_add_resources(can_upgrade "resources"
    PREFIX "/icons"
    FILES
        resources/icon.png
        resources/icon.ico
)

# 手动处理翻译文件的更新和编译
qt_add_lupdate(can_upgrade
    SOURCES ${PROJECT_SOURCES}
    TS_FILES ${TS_FILES}
)

qt_add_lrelease(can_upgrade
    TS_FILES ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

# 将 .qm 文件添加到资源中
qt_add_resources(can_upgrade "translations"
    PREFIX "/i18n"
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${QM_FILES}
)

target_link_libraries(can_upgrade PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialBus
)

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Windows特定设置
if(WIN32)
    # 设置Windows可执行文件图标
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
        LINK_FLAGS "/ENTRY:mainCRTStartup"
    )

    # 创建Windows资源文件以设置图标
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/app.ico.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.ico.rc"
        @ONLY
    )

    target_sources(can_upgrade PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.ico.rc")

    # 查找windeployqt工具
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin" "${Qt6_DIR}/bin")

    if(WINDEPLOYQT_EXECUTABLE)
        # 添加windeployqt自定义目标，只部署实际使用的模块
        # 根据构建类型选择 --release 或 --debug
        # 部署中文和英文翻译，排除不需要的模块
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --translations zh_CN,en
                --no-system-d3d-compiler
                --no-system-dxc-compiler
                --no-opengl-sw
                --skip-plugin-types tls,imageformats
                $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Running windeployqt to deploy Qt dependencies..."
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
        message(STATUS "windeployqt found: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "windeployqt not found. Qt dependencies will not be automatically deployed.")
    endif()
endif()


install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
