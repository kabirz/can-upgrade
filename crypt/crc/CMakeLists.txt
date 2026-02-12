# CMakeLists.txt for CRC Library
# 不使用查表法的 CRC 算法库

cmake_minimum_required(VERSION 3.10)
project(crc VERSION 1.0.0 LANGUAGES C)

# 设置 C 标准
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 编译选项
add_compile_options(-Wall -Wextra -pedantic)

# ============================================================================
# 目录配置
# ============================================================================
set(SRC_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INC_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TEST_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/tests")

# ============================================================================
# 静态库目标
# ============================================================================
add_library(crc_static STATIC
    ${SRC_DIR}/crc.c
    ${SRC_DIR}/crc_api.c
)

target_include_directories(crc_static PUBLIC
    ${INC_DIR}
)

# 设置库输出名称
set_target_properties(crc_static PROPERTIES
    OUTPUT_NAME crc
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 可选：构建共享库
option(BUILD_SHARED_LIB "Build shared library" OFF)
if(BUILD_SHARED_LIB)
    add_library(crc_shared SHARED
        ${SRC_DIR}/crc.c
        ${SRC_DIR}/crc_api.c
    )

    target_include_directories(crc_shared PUBLIC
        ${INC_DIR}
    )

    set_target_properties(crc_shared PROPERTIES
        OUTPUT_NAME crc
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
    )
endif()

# ============================================================================
# 测试程序
# ============================================================================
option(BUILD_TESTS "Build test program" ON)
if(BUILD_TESTS)
    add_executable(test_crc ${TEST_DIR}/test_crc.c)

    target_include_directories(test_crc PRIVATE
        ${INC_DIR}
    )

    target_link_libraries(test_crc crc_static)

    # 设置测试输出目录
    set_target_properties(test_crc PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    # 启用测试
    enable_testing()
    add_test(NAME crc_test COMMAND test_crc)
endif()

# ============================================================================
# 安装配置
# ============================================================================
include(GNUInstallDirs)

install(FILES
    ${INC_DIR}/crc.h
    ${INC_DIR}/crc_api.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS crc_static
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(BUILD_SHARED_LIB)
    install(TARGETS crc_shared
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

if(BUILD_TESTS)
    install(TARGETS test_crc
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ============================================================================
# 构建信息
# ============================================================================
message(STATUS "")
message(STATUS "╔════════════════════════════════════════════════════╗")
message(STATUS "║           CRC Library - Configuration              ║")
message(STATUS "╠════════════════════════════════════════════════════╣")
message(STATUS "║  Version:         ${PROJECT_VERSION}               ║")
message(STATUS "║  Build type:      ${CMAKE_BUILD_TYPE}              ║")
message(STATUS "║  C standard:      ${CMAKE_C_STANDARD}              ║")
message(STATUS "║  Shared lib:      ${BUILD_SHARED_LIB}              ║")
message(STATUS "║  Tests:           ${BUILD_TESTS}                   ║")
message(STATUS "╠════════════════════════════════════════════════════╣")
message(STATUS "║  Source dir:      ${SRC_DIR}                       ║")
message(STATUS "║  Include dir:     ${INC_DIR}                       ║")
message(STATUS "║  Test dir:        ${TEST_DIR}                      ║")
message(STATUS "╚════════════════════════════════════════════════════╝")
message(STATUS "")
