cmake_minimum_required(VERSION 3.20)

project(CANUpgradeTool VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_WIN32_EXECUTABLE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unicode support
add_compile_definitions(UNICODE _UNICODE)

# Source files
set(SOURCES
    src/main.cpp
    src/can_manager.cpp
)


include_directories(src)

# Resource file
set(RESOURCES
    resources/resource.rc
)

# Create executable
add_executable(can-upgrade
    ${SOURCES}
    ${RESOURCES}
)

# Link Windows subsystem (GUI mode)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_options(can-upgrade PRIVATE -mwindows)
endif()

# Static link C++ runtime for MinGW
if(MINGW)
    target_link_options(can-upgrade PRIVATE
        -static-libgcc
        -static-libstdc++
    )
endif()

# Link common controls library for progress bar
if(MINGW)
    target_link_libraries(can-upgrade PRIVATE
        -lcomctl32
        -lcomdlg32
        -lgdi32
    )
endif()

target_include_directories(can-upgrade PRIVATE
    include
)

target_link_options(can-upgrade PRIVATE "-s")

target_link_libraries(can-upgrade PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/x64/PCANBasic.lib"
)

