cmake_minimum_required(VERSION 3.4.1)

add_library(
             native-lib
             SHARED
             native-lib.cpp
             ImGui/imgui.cpp
             ImGui/imgui_impl_android.cpp
             ImGui/imgui_impl_vulkan.cpp
             ImGui/imgui_tables.cpp
             ImGui/imgui_widgets.cpp
             ImGui/imgui_demo.cpp
             ImGui/imgui_draw.cpp
             http/cpr/async.cpp
             http/cpr/auth.cpp
             http/cpr/bearer.cpp
             http/cpr/cookies.cpp
             http/cpr/cprtypes.cpp
             http/cpr/curl_container.cpp
             http/cpr/curlholder.cpp
             http/cpr/error.cpp
             http/cpr/multipart.cpp
             http/cpr/parameters.cpp
             http/cpr/payload.cpp
             http/cpr/proxies.cpp
             http/cpr/proxyauth.cpp
             http/cpr/session.cpp
             http/cpr/threadpool.cpp
             http/cpr/timeout.cpp
             http/cpr/unix_socket.cpp
             http/cpr/util.cpp
             http/cpr/response.cpp
             http/cpr/redirect.cpp
             http/cpr/interceptor.cpp
             Obfuscation/instr.cpp
             Memory/KittyArm64.cpp
             Memory/KittyMemory.cpp
             Memory/KittyScanner.cpp
             Memory/KittyUtils.cpp
             Memory/MemoryBackup.cpp
             Memory/MemoryPatch.cpp)

target_include_directories(
        native-lib
        PRIVATE
        ${BNM_INCLUDE}
        ${BNM_EXTERNAL_INCLUDE})

set(DOBBY_LIB ${CMAKE_CURRENT_SOURCE_DIR}/libs/${CMAKE_ANDROID_ARCH_ABI}/libdobby.a)
set(BNM_LIB   ${CMAKE_CURRENT_SOURCE_DIR}/libs/${CMAKE_ANDROID_ARCH_ABI}/libBNM.a)

if(NOT EXISTS ${DOBBY_LIB})
    message(FATAL_ERROR "libdobby.a not found at: ${DOBBY_LIB}")
endif()
if(NOT EXISTS ${BNM_LIB})
    message(FATAL_ERROR "libBNM.a not found at: ${BNM_LIB}")
endif()

find_library(log-lib log)
find_library(android android)
find_library(LZ_LIB z)
find_package(CURL REQUIRED CONFIG)

target_link_libraries(
        native-lib
        ${LZ_LIB}
        -Wl,--whole-archive
        ${BNM_LIB}
        ${DOBBY_LIB}
        -Wl,--no-whole-archive
        ${android}
        ${log-lib}
        CURL::libcurl)