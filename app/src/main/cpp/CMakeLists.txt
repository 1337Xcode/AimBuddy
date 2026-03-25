# CMakeLists.txt for ESP Native Library
# Configures NCNN with Vulkan backend and ImGui with OpenGL ES 3.1

cmake_minimum_required(VERSION 3.18.1)
project(esp_native VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Compiler flags — generic ARM64 baseline, works on ALL arm64-v8a devices.
# The CPU-specific extensions (dotprod, fp16, i8mm) are probed at runtime by
# NCNN's own CPU-capability detection, so we do NOT compile for a specific
# micro-arch at the CMake level. This keeps the binary compatible with every
# phone (Snapdragon 670, 765, 855, 865, 888, 8 Gen 1/2/3, Dimensity, Exynos).
# =============================================================================

# Baseline: armv8-a with crypto extensions — works on every arm64-v8a device.
# NEON/SIMD is mandatory in AArch64 and does NOT require -mfpu=neon (that flag is ARM32-only).
# +crypto: hardware AES/SHA (present on virtually all ARMv8 phones)
set(ARM64_BASE_FLAGS "-march=armv8-a+crypto")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARM64_BASE_FLAGS}")

# Core size & speed optimisations (apply to all build types)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
    -O3 \
    -ffast-math \
    -fno-math-errno \
    -fno-trapping-math \
    -fstrict-aliasing \
    -fomit-frame-pointer \
    -funroll-loops \
    -frtti \
    -fexceptions \
    -ffunction-sections \
    -fdata-sections \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden")

# Warnings — enable as many as useful but suppress noise from third-party headers
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
# Suppress warnings from third-party code we don't control (NCNN/ImGui/OpenMP)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
    -Wno-unused-parameter \
    -Wno-unused-variable \
    -Wno-unused-private-field \
    -Wno-missing-field-initializers")

# =============================================================================
# Per-configuration flags
# =============================================================================
# ThinLTO: faster than full LTO, still gives cross-module dead-code elimination
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -flto=thin")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DNDEBUG -flto=thin")

# =============================================================================
# Linker flags
# =============================================================================
# --gc-sections / --icf: remove dead code/data and merge identical sections
# --as-needed: only link shared libs that are actually referenced
# --strip-all: strip all symbols in release (reduces .so size)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} \
    -Wl,--gc-sections \
    -Wl,--icf=safe \
    -Wl,--as-needed")

set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} \
    -flto=thin \
    -Wl,--strip-all")
set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} \
    -flto=thin")

# =============================================================================
# NCNN Library
# =============================================================================
set(NCNN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ncnn)
set(NCNN_INCLUDE_DIR ${NCNN_DIR}/include)
set(NCNN_LIB_DIR ${NCNN_DIR}/lib)

# Explicitly link static libraries (find_library can be flaky with static libs on NDK)
set(NCNN_LIBRARY ${NCNN_LIB_DIR}/libncnn.a)
set(GLSLANG_LIBRARY ${NCNN_LIB_DIR}/libglslang.a)
set(SPIRV_LIBRARY ${NCNN_LIB_DIR}/libSPIRV.a)
set(OS_DEPENDENT_LIBRARY ${NCNN_LIB_DIR}/libOSDependent.a)
set(MACHINE_INDEPENDENT_LIBRARY ${NCNN_LIB_DIR}/libMachineIndependent.a)
set(GENERIC_CODEGEN_LIBRARY ${NCNN_LIB_DIR}/libGenericCodeGen.a)
set(GLSLANG_DEFAULT_RESOURCE_LIMITS_LIBRARY ${NCNN_LIB_DIR}/libglslang-default-resource-limits.a)

# Check if required NCNN core library exists
if(NOT EXISTS ${NCNN_LIBRARY})
    message(FATAL_ERROR "NCNN library not found at ${NCNN_LIBRARY}. Ensure ncnn static libs are present in ${NCNN_LIB_DIR}.")
endif()

set(NCNN_DEPENDENCY_LIBS
    ${NCNN_LIBRARY}
    ${GLSLANG_LIBRARY}
    ${SPIRV_LIBRARY}
    ${OS_DEPENDENT_LIBRARY}
    ${MACHINE_INDEPENDENT_LIBRARY}
    ${GENERIC_CODEGEN_LIBRARY}
    ${GLSLANG_DEFAULT_RESOURCE_LIMITS_LIBRARY}
)

set(NCNN_EXISTING_LIBS "")
foreach(NCNN_LIB ${NCNN_DEPENDENCY_LIBS})
    if(EXISTS ${NCNN_LIB})
        list(APPEND NCNN_EXISTING_LIBS ${NCNN_LIB})
    else()
        message(STATUS "Optional NCNN dependency library missing (will skip): ${NCNN_LIB}")
    endif()
endforeach()

message(STATUS "NCNN library: ${NCNN_LIBRARY}")
message(STATUS "NCNN include: ${NCNN_INCLUDE_DIR}")

# =============================================================================
# ImGui Sources
# =============================================================================
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/imgui_impl_android.cpp
)

# =============================================================================
# ESP Native Library
# =============================================================================
set(ESP_SOURCES
    esp_jni.cpp
    detector/yolo_detector.cpp
    aimbot/aimbot_controller.cpp
    aimbot/target_tracker.cpp
    renderer/overlay_window.cpp
    renderer/esp_renderer.cpp
    renderer/imgui_menu.cpp
    input/touch_helper.cpp
)

add_library(esp_native SHARED
    ${ESP_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(esp_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NCNN_INCLUDE_DIR}
    ${IMGUI_DIR}
)

# =============================================================================
# Find Android Libraries
# =============================================================================
find_library(LOG_LIB log)
find_library(ANDROID_LIB android)
find_library(EGL_LIB EGL)
find_library(GLES3_LIB GLESv3)
find_library(JNIGRAPHICS_LIB jnigraphics)
find_library(NATIVEWINDOW_LIB nativewindow)
find_library(VULKAN_LIB vulkan)

# =============================================================================
# Link Libraries
# =============================================================================
target_link_libraries(esp_native
    ${LOG_LIB}
    ${ANDROID_LIB}
    ${EGL_LIB}
    ${GLES3_LIB}
    ${JNIGRAPHICS_LIB}
    ${NATIVEWINDOW_LIB}
    ${VULKAN_LIB}
    ${NCNN_EXISTING_LIBS}
)

# Vulkan / media NDK support
find_library(MEDIANDK_LIB mediandk)
if(MEDIANDK_LIB)
    target_link_libraries(esp_native ${MEDIANDK_LIB})
endif()

# Link OpenMP if available (NCNN uses it for multi-core CPU inference)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(esp_native OpenMP::OpenMP_CXX)
endif()

message(STATUS "ESP native library configured successfully")
