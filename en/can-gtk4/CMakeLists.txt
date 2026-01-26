cmake_minimum_required(VERSION 3.25)
project(can-upgrade LANGUAGES C)

# Size optimization flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -s")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 IMPORTED_TARGET REQUIRED gtk4)

add_executable(can-upgrade
    src/main.c
    src/can_manager.c
)

target_include_directories(can-upgrade PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(can-upgrade
    PkgConfig::GTK4
    Threads::Threads
)
install(FILES resources/main.ui DESTINATION bin/resources)
install(TARGETS can-upgrade DESTINATION bin)

get_target_property(GTK4_INCLUDES PkgConfig::GTK4 INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(GTK4_LIBS PkgConfig::GTK4 INTERFACE_LINK_LIBRARIES)
get_target_property(GTK4_COMPILE_OPTS PkgConfig::GTK4 INTERFACE_COMPILE_OPTIONS)

message(STATUS "GTK4 includes: ${GTK4_INCLUDES}")
message(STATUS "GTK4 libraries: ${GTK4_LIBS}")
message(STATUS "GTK4 compile options: ${GTK4_COMPILE_OPTS}")

