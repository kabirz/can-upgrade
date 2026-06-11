cmake_minimum_required(VERSION 3.25)

project(can-webkitgtk VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBKITGTK REQUIRED webkitgtk-6.0)
pkg_check_modules(GTK4 REQUIRED gtk4)

set(HTML_INPUT "${CMAKE_SOURCE_DIR}/assets/index.html")
set(HTML_HEADER "${CMAKE_BINARY_DIR}/generated/index_html.h")

add_custom_command(
    OUTPUT "${HTML_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT="${HTML_INPUT}"
        -DOUTPUT="${HTML_HEADER}"
        -DVARNAME="INDEX_HTML"
        -P "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
    DEPENDS "${HTML_INPUT}"
    COMMENT "Embedding index.html into C header"
)

add_custom_target(generate_html DEPENDS "${HTML_HEADER}")

add_executable(can-webkitgtk
    src/main.cpp
    src/CanManager.cpp
)
add_dependencies(can-webkitgtk generate_html)

target_include_directories(can-webkitgtk PRIVATE
    src
    "${CMAKE_BINARY_DIR}/generated"
    ${WEBKITGTK_INCLUDE_DIRS}
    ${GTK4_INCLUDE_DIRS}
)

target_compile_options(can-webkitgtk PRIVATE ${WEBKITGTK_CFLAGS_OTHER})

target_link_libraries(can-webkitgtk PRIVATE
    ${WEBKITGTK_LIBRARIES}
    ${GTK4_LIBRARIES}
    pthread
)

target_link_directories(can-webkitgtk PRIVATE
    ${WEBKITGTK_LIBRARY_DIRS}
    ${GTK4_LIBRARY_DIRS}
)

install(TARGETS can-webkitgtk DESTINATION bin)
