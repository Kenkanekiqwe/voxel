cmake_minimum_required(VERSION 3.20)
project(VoxelVoice LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus GIT_REPOSITORY https://github.com/xiph/opus.git GIT_TAG v1.5.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(opus)

set(MINIAUDIO_DIR "${CMAKE_BINARY_DIR}/_deps/miniaudio")
set(MINIAUDIO_HEADER "${MINIAUDIO_DIR}/miniaudio.h")
file(MAKE_DIRECTORY "${MINIAUDIO_DIR}")
if(NOT EXISTS "${MINIAUDIO_HEADER}")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h"
    "${MINIAUDIO_HEADER}"
    SHOW_PROGRESS
    STATUS MINIAUDIO_DOWNLOAD_STATUS
    TLS_VERIFY ON
  )
  list(GET MINIAUDIO_DOWNLOAD_STATUS 0 MINIAUDIO_STATUS_CODE)
  if(NOT MINIAUDIO_STATUS_CODE EQUAL 0)
    message(FATAL_ERROR "Failed to download miniaudio.h: ${MINIAUDIO_DOWNLOAD_STATUS}")
  endif()
endif()

add_executable(VoxelClient WIN32 client/main.cpp client/audio.cpp client/screen_capture.cpp)
target_include_directories(VoxelClient PRIVATE "${MINIAUDIO_DIR}" "${opus_SOURCE_DIR}/include" client common)
target_link_libraries(VoxelClient PRIVATE opus ws2_32 ole32 user32 gdi32 windowscodecs)
target_compile_definitions(VoxelClient PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(VoxelServer server/main.cpp)
target_include_directories(VoxelServer PRIVATE common)
target_link_libraries(VoxelServer PRIVATE ws2_32)
target_compile_definitions(VoxelServer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  target_compile_options(VoxelClient PRIVATE /W4 /permissive-)
  target_compile_options(VoxelServer PRIVATE /W4 /permissive-)
endif()
