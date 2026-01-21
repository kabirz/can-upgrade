cmake_minimum_required(VERSION 3.20)
project(can-upgrade-gtk4 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find GTK4
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

include_directories(${GTK4_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS})

set(SOURCES
    src/main.c
    src/can_socket.c
    src/window.c
)

add_executable(can-upgrade-gtk4 ${SOURCES})

target_compile_options(can-upgrade-gtk4 PRIVATE ${GTK4_CFLAGS_OTHER})
target_link_libraries(can-upgrade-gtk4 ${GTK4_LIBRARIES} pthread)
target_link_options(can-upgrade-gtk4 PRIVATE "-s")

# 安装
install(TARGETS can-upgrade-gtk4 DESTINATION bin)
