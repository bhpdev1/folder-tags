cmake_minimum_required(VERSION 3.20)
# Product version, shared with the setup (scripts/build.ps1 -Version). Code signing requires the
# same product name and version in every signed file.
set(FOLDERTAGS_VERSION "1.2.0" CACHE STRING "Product version (x.y.z)")
project(FolderTags VERSION ${FOLDERTAGS_VERSION} LANGUAGES CXX RC)
configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/version.rc @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(FolderTags SHARED
    src/dllmain.cpp
    src/context_menu.cpp
    src/tags.cpp
    src/draw.cpp
    src/tags_window.cpp
    src/index.cpp
    src/overlay.cpp
    src/FolderTags.def
    ${CMAKE_BINARY_DIR}/version.rc
)

target_compile_definitions(FolderTags PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(FolderTags PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(FolderTags PRIVATE shlwapi shell32 ole32 gdiplus dwmapi user32 gdi32 advapi32)
