cmake_minimum_required(VERSION 3.25)

project(CANUpgradeTool VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE)

set(WEBVIEW2_VERSION "1.0.2792.45")
set(WEBVIEW2_URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}")
set(WEBVIEW2_DIR "${CMAKE_BINARY_DIR}/webview2-sdk")

if(NOT EXISTS "${WEBVIEW2_DIR}/build/native/include/WebView2.h")
    message(STATUS "Downloading WebView2 SDK ${WEBVIEW2_VERSION}...")
    file(DOWNLOAD ${WEBVIEW2_URL} "${CMAKE_BINARY_DIR}/webview2.zip" SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/webview2.zip" DESTINATION "${WEBVIEW2_DIR}")
    file(REMOVE "${CMAKE_BINARY_DIR}/webview2.zip")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_ARCH "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WEBVIEW2_ARCH "x86")
else()
    set(WEBVIEW2_ARCH "x64")
endif()

set(SOURCES
    src/main.cpp
    src/CanManager.cpp
)

set(RESOURCES
    resources/resource.rc
)

add_executable(can-webview2 ${SOURCES} ${RESOURCES})

target_include_directories(can-webview2 PRIVATE
    include
    "${WEBVIEW2_DIR}/build/native/include"
)

if(MSVC)
    target_compile_options(can-webview2 PRIVATE /utf-8 /EHsc)
    set_target_properties(can-webview2 PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")

    if(EXISTS "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
        target_link_libraries(can-webview2 PRIVATE
            "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"
            advapi32 ole32 oleaut32 shell32 shlwapi user32 version comctl32 comdlg32
        )
    else()
        target_link_libraries(can-webview2 PRIVATE
            "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.lib"
            advapi32 ole32 oleaut32 shell32 shlwapi user32 version comctl32 comdlg32
        )
        add_custom_command(TARGET can-webview2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.dll"
                $<TARGET_FILE_DIR:can-webview2>
        )
    endif()
elseif(MINGW)
    target_compile_options(can-webview2 PRIVATE
        -Os -ffunction-sections -fdata-sections
    )
    target_link_options(can-webview2 PRIVATE
        -mwindows -static-libgcc -s -Wl,--gc-sections -Wl,--strip-all
    )
    target_link_libraries(can-webview2 PRIVATE
        "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.lib"
        -lole32 -loleaut32 -lshell32 -lshlwapi -luser32 -ladvapi32 -lversion -lcomctl32 -lcomdlg32
    )
    add_custom_command(TARGET can-webview2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.dll"
            $<TARGET_FILE_DIR:can-webview2>
    )
endif()
