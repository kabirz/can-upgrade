cmake_minimum_required(VERSION 3.25)

project(CANUpgradeTool VERSION 1.0.0 LANGUAGES C)

# Enable Unicode support
add_compile_definitions(UNICODE _UNICODE)

# Source files
set(SOURCES
    src/main.c
    src/can_manager.c
    src/uart_manager.c
)

# Resource files
set(RESOURCES
    resources/resource.rc
)

# Create executable
add_executable(can-upgrade
    ${SOURCES}
    ${RESOURCES}
)

# Compile optimization options to reduce executable size
if(MINGW)
    target_compile_options(can-upgrade PRIVATE
        -Os                    # Optimize code size
        -ffunction-sections    # Place functions in independent sections
        -fdata-sections        # Place data in independent sections
    )
    target_link_options(can-upgrade PRIVATE
        -mwindows              # Windows GUI application
        -static-libgcc         # Statically link libgcc
        -s                     # Remove symbols
        -Wl,--gc-sections      # Remove unused sections
        -Wl,--strip-all        # Remove all symbols
    )
elseif(MSVC)
    # Support UTF-8 encoded source files
    target_compile_options(can-upgrade PRIVATE /utf-8)
    # Windows GUI application entry point is WinMain
    set_target_properties(can-upgrade PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# Link Windows system libraries
if(MINGW)
    target_link_libraries(can-upgrade PRIVATE
        -lcomctl32    # Common controls library (progress bar)
        -lcomdlg32    # Common dialog library (file selection)
        -lgdi32       # GDI graphics device interface
        -lsetupapi    # SetupAPI (serial port enumeration)
    )
elseif(MSVC)
    target_link_libraries(can-upgrade PRIVATE
        comctl32      # Common controls library (progress bar)
        comdlg32      # Common dialog library (file selection)
        gdi32         # GDI graphics device interface
        setupapi      # SetupAPI (serial port enumeration)
    )
endif()

target_include_directories(can-upgrade PRIVATE include)

target_link_libraries(can-upgrade PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/x64/PCANBasic.lib"
)
