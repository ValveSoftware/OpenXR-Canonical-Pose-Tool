# Copyright (c) 2023 Valve Corporation
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
# Initial Author: Daniel Willmott <danw@valvesoftware.com>

cmake_minimum_required(VERSION 3.7.0)
set(CMAKE_CXX_STANDARD 20)

set(PROJECT_NAME "oxr_canonical_pose_tool")

project(${PROJECT_NAME})

add_subdirectory(lib/OpenXR-SDK)
add_subdirectory(lib/pugixml)

set(SOURCE_FILES src/main.cpp
        src/items/item.h
        src/xr/xrp.h
        src/xr/xrp.cpp
        src/items/inputs/inputs.cpp
        src/items/inputs/inputs.h
        src/items/inputs/action_pose.cpp
        src/items/inputs/action_pose.h
        src/util/util_file.cpp src/util/util_file.h)

if (ANDROID)
    find_library(ANDROID_LIBRARY NAMES android)
    find_library(ANDROID_LOG_LIBRARY NAMES log)

    add_library(${PROJECT_NAME} MODULE
            ${SOURCE_FILES}
            ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
            )

    add_library(meta_openxr_loader SHARED IMPORTED)
    set_property(
            TARGET meta_openxr_loader
            PROPERTY IMPORTED_LOCATION
            "${CMAKE_SOURCE_DIR}/lib/openxr_meta/OpenXR/Libs/Android/arm64-v8a/Debug/libopenxr_loader.so")

    target_include_directories(${PROJECT_NAME} PRIVATE src lib/rawdraw ${ANDROID_NDK}/sources/android/native_app_glue)

    target_link_libraries(${PROJECT_NAME}
            PUBLIC
            openxr_loader
            meta_openxr_loader
            pugixml
            EGL
            GLESv2
            ${ANDROID_LIBRARY}
            ${ANDROID_LOG_LIBRARY})

    target_compile_definitions(${PROJECT_NAME} PRIVATE XR_USE_PLATFORM_ANDROID=1)
else ()
    add_executable(${PROJECT_NAME}
            ${SOURCE_FILES}
            )

    target_include_directories(${PROJECT_NAME} PRIVATE src lib/rawdraw)
    target_link_libraries(${PROJECT_NAME} PRIVATE openxr_loader pugixml)

    if (WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 d3d12 dxgi)
        target_compile_definitions(${PROJECT_NAME} PRIVATE XR_USE_PLATFORM_WIN32)
    elseif (UNIX)
        # OpenGL
        set(OpenGL_GL_PREFERENCE "LEGACY")
        find_package(OpenGL REQUIRED)
        find_package(X11 REQUIRED)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${X11_LIBRARIES} OpenGL::GL)
        target_compile_definitions(${PROJECT_NAME} PRIVATE XR_USE_PLATFORM_XLIB)
    endif ()

    add_custom_command(
            TARGET ${PROJECT_NAME}
            PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/dist
            ${CMAKE_CURRENT_BINARY_DIR}/
    )
endif ()

