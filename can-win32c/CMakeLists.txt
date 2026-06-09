cmake_minimum_required(VERSION 3.25)

project(CANUpgradeTool VERSION 1.0.0 LANGUAGES C)

add_compile_definitions(UNICODE _UNICODE)

set(SOURCES
    src/main.c
    src/can_manager.c
    src/pcan_loader.c
)

set(RESOURCES
    resources/resource.rc
)

add_executable(can-upgrade
    ${SOURCES}
    ${RESOURCES}
)

if(MINGW)
    target_compile_options(can-upgrade PRIVATE
        -Os
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(can-upgrade PRIVATE
        -mwindows
        -static-libgcc
        -s
        -Wl,--gc-sections
        -Wl,--strip-all
    )
elseif(MSVC)
    target_compile_options(can-upgrade PRIVATE /utf-8)
    set_target_properties(can-upgrade PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

if(MINGW)
    target_link_libraries(can-upgrade PRIVATE
        -lcomctl32
        -lcomdlg32
        -lgdi32
    )
elseif(MSVC)
    target_link_libraries(can-upgrade PRIVATE
        comctl32
        comdlg32
        gdi32
    )
endif()

target_include_directories(can-upgrade PRIVATE include)
