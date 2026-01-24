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

# Optimization flags to reduce exe size
if(MINGW)
    target_compile_options(can-upgrade PRIVATE
        -Os                    # Optimize for size
        -s                     # Strip symbols (also in link options)
        -fno-exceptions        # Disable C++ exceptions
        -fno-rtti              # Disable RTTI
        -ffunction-sections    # Separate functions into sections
        -fdata-sections        # Separate data into sections
    )
    target_link_options(can-upgrade PRIVATE
        -mwindows
        -static-libgcc
        -static-libstdc++
        -s                     # Strip symbols
        -Wl,--gc-sections      # Remove unused sections
        -Wl,--strip-all        # Strip all symbols
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

target_link_libraries(can-upgrade PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/x64/PCANBasic.lib"
)
