cmake_minimum_required(VERSION 3.20)
project(gkcore VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(GKCORE_BUILD_TESTS "Build dependency-light development tests" ON)
option(GKCORE_RUN_BACKEND_SMOKE "Run the Windows Direct3D 12 backend smoke test on the host GPU" OFF)
if(WIN32)
    set(_gkcore_runtime_default ON)
else()
    set(_gkcore_runtime_default OFF)
endif()
option(GKCORE_BUILD_RUNTIME "Build the distributable The Forge-backed Runtime SDK" ${_gkcore_runtime_default})
set(GKCORE_FORGE_ROOT "" CACHE PATH "Path to the verified pinned The Forge source tree; never fetched by CMake")
set(GKCORE_FORGE_BUILD_DIR "${CMAKE_BINARY_DIR}/forge" CACHE PATH "Development-only output from tools/build_forge.py")
set(GKCORE_DXC_ROOT "" CACHE PATH "Path to the matching pinned DirectX Shader Compiler package")
set(GKCORE_SHADER_BUILD_DIR "${CMAKE_BINARY_DIR}/gkcore_shaders" CACHE PATH "Development-only output from tools/build_gkcore_shaders.py")

set(GKCORE_CORE_SOURCES
    src/api/gkcore.cpp
    src/core/Context.cpp
    src/core/Frame.cpp
    src/core/Input.cpp
    src/platform/FKeyboardState.cpp
    src/draw/Draw2D.cpp
    src/draw/Draw3D.cpp
    src/foundation/Memory.cpp
    src/foundation/Array.cpp
    src/foundation/String.cpp
    src/foundation/RefCount.cpp
    src/resources/Resources.cpp
    src/resources/ResourceIO.cpp
    src/image/Image.cpp
    src/image/ImageLoader.cpp
    src/model/Model.cpp
    src/model/ModelLoader.cpp
    src/model/ObjLoader.cpp
    src/model/GlbLoader.cpp
    src/model/Cgltf.cpp
    src/model/FbxLoader.cpp
    src/model/FbxGeometry.cpp
    src/model/FbxMaterial.cpp
    src/model/Ufbx.cpp
    src/text/Text.cpp
    src/text/TextLayout.cpp
    src/text/TextCache.cpp
    src/effects/Effects.cpp
    src/effects/Lighting.cpp
    src/effects/Shaders.cpp
    src/render/Geometry.cpp
    src/render/RectangleGeometry.cpp
    src/render/Shaders.cpp
    src/render/ShaderAbi.cpp
    src/render/PostProcess.cpp
    src/render/PostProcessPlan.cpp
    src/render/PostEffectPlan.cpp
    src/render/ModelDrawPlan.cpp
    src/render/LightingAbi.cpp
    src/render/WorldGeometry.cpp
    src/render/ModelGeometry.cpp
    src/render/CustomShaderPolicy.cpp
    src/render/TextureCachePolicy.cpp)

function(gkcore_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(GKCORE_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Arrayが通常境界を超える型を拒否し、意図した診断で止まることを確認する。
    set(_gkcore_foundation_alignment_compile_options)
    if(MSVC)
        list(APPEND _gkcore_foundation_alignment_compile_options /utf-8)
    endif()
    try_compile(GKCORE_FOUNDATION_OVERALIGNED_COMPILE_SUCCEEDED
        "${CMAKE_CURRENT_BINARY_DIR}/foundation_overaligned_compile_check"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/foundation_overaligned_compile_fail.cpp"
        COMPILE_DEFINITIONS ${_gkcore_foundation_alignment_compile_options}
        CMAKE_FLAGS
            "-DCMAKE_CXX_STANDARD=17"
            "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
        OUTPUT_VARIABLE GKCORE_FOUNDATION_OVERALIGNED_COMPILE_OUTPUT)
    if(GKCORE_FOUNDATION_OVERALIGNED_COMPILE_SUCCEEDED)
        message(FATAL_ERROR "gk::Array accepted an over-aligned value type")
    endif()
    if(NOT GKCORE_FOUNDATION_OVERALIGNED_COMPILE_OUTPUT MATCHES "gk::Array does not support over-aligned values")
        message(FATAL_ERROR "The over-aligned Array compile check failed for an unexpected reason:\n${GKCORE_FOUNDATION_OVERALIGNED_COMPILE_OUTPUT}")
    endif()

    add_test(NAME gkcore.package_allowlist
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_allowlist.py)
    add_test(NAME gkcore.package_consumer_plan
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_consumer_smoke_plan.py)
    add_test(NAME gkcore.forge_checkout
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_forge_checkout.py)
    add_test(NAME gkcore.forge_build_plan
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_forge_build_plan.py)
    add_test(NAME gkcore.shader_build_plan
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_shader_build_plan.py)
    add_test(NAME gkcore.pixel_shader_compiler
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_pixel_shader_compiler.py)
    add_executable(gkcore_shader_interface_tests
        tests/shader_interface_tests.cpp
        src/render/ShaderInterface.cpp
        src/foundation/Memory.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_shader_interface_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_shader_interface_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_shader_interface_tests)
    add_test(NAME gkcore.shader_interface COMMAND gkcore_shader_interface_tests)
    add_executable(gkcore_custom_shader_policy_tests
        tests/custom_shader_policy_tests.cpp
        src/render/CustomShaderPolicy.cpp
        src/foundation/Memory.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_custom_shader_policy_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_custom_shader_policy_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_custom_shader_policy_tests)
    add_test(NAME gkcore.custom_shader_policy COMMAND gkcore_custom_shader_policy_tests)
    add_executable(gkcore_post_process_plan_tests
        tests/post_process_plan_tests.cpp
        src/render/PostProcessPlan.cpp
        src/render/PostProcess.cpp)
    target_compile_features(gkcore_post_process_plan_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_post_process_plan_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_post_process_plan_tests)
    add_test(NAME gkcore.post_process_plan COMMAND gkcore_post_process_plan_tests)
    add_executable(gkcore_post_effect_plan_tests
        tests/post_effect_plan_tests.cpp
        src/render/PostEffectPlan.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_post_effect_plan_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_post_effect_plan_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_post_effect_plan_tests)
    add_test(NAME gkcore.post_effect_plan COMMAND gkcore_post_effect_plan_tests)
    add_executable(gkcore_fxaa_tests
        tests/fxaa_tests.cpp
        src/render/Fxaa.cpp
        src/render/PostProcess.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_fxaa_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_fxaa_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_fxaa_tests)
    add_test(NAME gkcore.fxaa COMMAND gkcore_fxaa_tests)
    add_executable(gkcore_model_draw_plan_tests
        tests/model_draw_plan_tests.cpp
        src/render/ModelDrawPlan.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_model_draw_plan_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_draw_plan_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_model_draw_plan_tests)
    add_test(NAME gkcore.model_draw_plan COMMAND gkcore_model_draw_plan_tests)
    add_executable(gkcore_lighting_tests
        tests/lighting_tests.cpp
        src/effects/Lighting.cpp
        src/render/LightingAbi.cpp
        src/foundation/Memory.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_lighting_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_lighting_tests PRIVATE include src)
    target_compile_definitions(gkcore_lighting_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_lighting_tests)
    add_test(NAME gkcore.lighting COMMAND gkcore_lighting_tests)
    add_executable(gkcore_model_geometry_tests
        tests/model_geometry_tests.cpp
        src/render/ModelGeometry.cpp
        src/render/WorldGeometry.cpp
        src/render/PostProcess.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_model_geometry_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_geometry_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_geometry_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_geometry_tests)
    add_test(NAME gkcore.model_geometry COMMAND gkcore_model_geometry_tests)
    add_test(NAME gkcore.setup_plan
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_setup_plan.py)
    add_test(NAME gkcore.dxc_lock
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_dxc_lock.py)
    add_test(NAME gkcore.no_stl
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/no_stl_tests.py)
    add_test(NAME gkcore.shader_contract
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_contract_tests.py)
    if(NOT GKCORE_BUILD_RUNTIME)
        add_test(NAME gkcore.runtime_install_refuses_missing_renderer
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_runtime_refusal.py
                --cmake ${CMAKE_COMMAND}
                --build-dir ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    add_executable(gkcore_tests
        tests/core_tests.cpp
        tests/resource_tests.cpp
        tests/effect_tests.cpp
        tests/foundation_tests.cpp
        tests/shader_artifact_tests.cpp
        tests/post_process_tests.cpp
        ${GKCORE_CORE_SOURCES})
    target_compile_features(gkcore_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_tests PRIVATE include src)
    target_compile_definitions(gkcore_tests PRIVATE
        GKCORE_TESTING=1
        GKCORE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(gkcore_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(gkcore_tests PRIVATE -UNDEBUG)
    endif()
    gkcore_enable_warnings(gkcore_tests)
    add_test(NAME gkcore.core COMMAND gkcore_tests)
    add_executable(gkcore_keyboard_state_tests tests/keyboard_state_tests.cpp src/platform/FKeyboardState.cpp)
    target_compile_features(gkcore_keyboard_state_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_keyboard_state_tests PRIVATE src)
    gkcore_enable_warnings(gkcore_keyboard_state_tests)
    add_test(NAME gkcore.keyboard_state COMMAND gkcore_keyboard_state_tests)
    add_executable(gkcore_fbx_tests
        tests/fbx_tests.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp
        src/foundation/RefCount.cpp
        src/resources/Resources.cpp
        src/resources/ResourceIO.cpp
        src/image/Image.cpp
        src/image/ImageLoader.cpp
        src/model/Model.cpp
        src/model/ModelLoader.cpp
        src/model/ObjLoader.cpp
        src/model/GlbLoader.cpp
        src/model/Cgltf.cpp
        src/model/FbxLoader.cpp
        src/model/FbxGeometry.cpp
        src/model/FbxMaterial.cpp
        src/model/Ufbx.cpp)
    target_compile_features(gkcore_fbx_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_fbx_tests PRIVATE include src third_party/ufbx)
    target_compile_definitions(gkcore_fbx_tests PRIVATE
        GKCORE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    gkcore_enable_warnings(gkcore_fbx_tests)
    add_test(NAME gkcore.fbx COMMAND gkcore_fbx_tests)
    add_executable(gkcore_fbx_geometry_tests
        tests/fbx_geometry_tests.cpp
        src/model/FbxGeometry.cpp
        src/model/Ufbx.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp
        src/foundation/RefCount.cpp)
    target_compile_features(gkcore_fbx_geometry_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_fbx_geometry_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_fbx_geometry_tests)
    add_test(NAME gkcore.fbx_geometry COMMAND gkcore_fbx_geometry_tests)
    add_executable(gkcore_geometry_tests
        tests/geometry_tests.cpp
        src/render/Geometry.cpp
        src/render/RectangleGeometry.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_geometry_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_geometry_tests PRIVATE include src)
    target_compile_definitions(gkcore_geometry_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_geometry_tests)
    add_test(NAME gkcore.geometry COMMAND gkcore_geometry_tests)
    target_sources(gkcore_geometry_tests PRIVATE
        src/render/PostProcess.cpp
        src/render/WorldGeometry.cpp
        src/render/ModelDrawPlan.cpp)
    add_executable(gkcore_texture_cache_policy_tests
        tests/texture_cache_policy_tests.cpp
        src/render/TextureCachePolicy.cpp)
    target_compile_features(gkcore_texture_cache_policy_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_texture_cache_policy_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_texture_cache_policy_tests)
    add_test(NAME gkcore.texture_cache_policy COMMAND gkcore_texture_cache_policy_tests)
    add_executable(gkcore_shader_abi_tests
        tests/shader_abi_tests.cpp
        src/render/ShaderAbi.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_shader_abi_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_shader_abi_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_shader_abi_tests)
    add_test(NAME gkcore.shader_abi COMMAND gkcore_shader_abi_tests)
    add_executable(gkcore_text_layout_tests
        tests/text_layout_tests.cpp
        src/text/TextLayout.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_text_layout_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_text_layout_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_text_layout_tests)
    add_test(NAME gkcore.text_layout COMMAND gkcore_text_layout_tests)
    if(WIN32)
        add_library(gkcore_windows_header_checks OBJECT
            tests/windows_header_order_tests.cpp
            tests/windows_gkcore_header_order_tests.cpp)
        target_compile_features(gkcore_windows_header_checks PRIVATE cxx_std_17)
        target_include_directories(gkcore_windows_header_checks PRIVATE include)
        gkcore_enable_warnings(gkcore_windows_header_checks)
        add_executable(gkcore_windows_text_tests
            tests/windows_text_tests.cpp
            src/platform/WindowsText.cpp
            src/text/TextLayout.cpp
            src/image/Image.cpp
            src/foundation/Memory.cpp
            src/foundation/Array.cpp
            src/foundation/String.cpp
            src/foundation/RefCount.cpp)
        target_compile_features(gkcore_windows_text_tests PRIVATE cxx_std_17)
        target_include_directories(gkcore_windows_text_tests PRIVATE include src)
        target_link_libraries(gkcore_windows_text_tests PRIVATE gdi32)
        gkcore_enable_warnings(gkcore_windows_text_tests)
        add_test(NAME gkcore.windows_text COMMAND gkcore_windows_text_tests)
    endif()
endif()

if(GKCORE_BUILD_RUNTIME)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The Runtime SDK requires Windows and MSVC. Linux and other platforms can build the development tests with GKCORE_BUILD_RUNTIME=OFF.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The pinned The Forge project files support x64 only.")
    endif()
    if(NOT MSVC_VERSION EQUAL 1929)
        message(FATAL_ERROR "The pinned The Forge headers require the v142 14.29 compiler (MSVC 1929). Under Visual Studio 2022 or 2026 configure with `-T v142`; PRE_SETUP.bat selects the matching instance.")
    endif()
    if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "10.0.22621.0")
        message(FATAL_ERROR "The pinned The Forge Direct3D 12 GUID definitions conflict with dxguid from newer Windows SDKs. The Runtime requires Windows SDK 10.0.22621.0, but CMake selected ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}. Rerun PRE_SETUP.bat and configure with a fresh build directory to discard any cached SDK selection.")
    endif()
    if(NOT GKCORE_FORGE_ROOT OR NOT IS_DIRECTORY "${GKCORE_FORGE_ROOT}")
        message(FATAL_ERROR "Set GKCORE_FORGE_ROOT to a verified The Forge checkout. Run PRE_SETUP.bat to fetch and prepare the pinned dependency; CMake never fetches it.")
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/forge_checkout.py --root ${GKCORE_FORGE_ROOT}
        RESULT_VARIABLE _gkcore_forge_verify_result
        OUTPUT_VARIABLE _gkcore_forge_verify_output
        ERROR_VARIABLE _gkcore_forge_verify_error)
    if(NOT _gkcore_forge_verify_result EQUAL 0)
        message(FATAL_ERROR "The Forge source tree is not the locked upstream commit. ${_gkcore_forge_verify_output}${_gkcore_forge_verify_error}")
    endif()

    if(NOT EXISTS "${GKCORE_FORGE_BUILD_DIR}/Renderer/Renderer.lib" OR NOT EXISTS "${GKCORE_FORGE_BUILD_DIR}/OS/OS.lib")
        message(FATAL_ERROR "The Forge x64 libraries are missing. Build Debug or Release to match the Runtime configuration, and set GKCORE_FORGE_BUILD_DIR to that output directory.")
    endif()
    set(_gkcore_shader_artifacts
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_color.vert"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_color.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_sprite.vert"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_sprite.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_model.vert"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_model.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_post.vert"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_bloom_extract.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_bloom_blur.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_post_composite.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_fxaa.frag"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/default.rootsig"
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/compute.rootsig")
    foreach(_shader IN LISTS _gkcore_shader_artifacts)
        if(NOT EXISTS "${_shader}")
            message(FATAL_ERROR "A required compiled gkcore Runtime shader is missing: ${_shader}. Run tools/build_gkcore_shaders.py with the pinned Forge and DXC roots first.")
        endif()
    endforeach()
    if(NOT GKCORE_DXC_ROOT OR NOT IS_DIRECTORY "${GKCORE_DXC_ROOT}")
        message(FATAL_ERROR "Set GKCORE_DXC_ROOT to the verified DirectX Shader Compiler 1.8.2405 package. Run PRE_SETUP.bat to prepare it; CMake never fetches dependencies.")
    endif()
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_dxc.py --root ${GKCORE_DXC_ROOT}
        RESULT_VARIABLE _gkcore_dxc_verify_result
        OUTPUT_VARIABLE _gkcore_dxc_verify_output
        ERROR_VARIABLE _gkcore_dxc_verify_error)
    if(NOT _gkcore_dxc_verify_result EQUAL 0)
        message(FATAL_ERROR "The DXC package does not match the locked 1.8.2405 release. ${_gkcore_dxc_verify_output}${_gkcore_dxc_verify_error}")
    endif()

    set(_gkcore_forge_common "${GKCORE_FORGE_ROOT}/Common_3")
    set(_gkcore_forge_libs
        "${GKCORE_DXC_ROOT}/lib/x64/dxcompiler.lib"
        "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/ags/ags_lib/lib/amd_ags_x64.lib"
        "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/nvapi/amd64/nvapi64.lib"
        "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/winpixeventruntime/bin/WinPixEventRuntime.lib")
    foreach(_lib IN LISTS _gkcore_forge_libs)
        if(NOT EXISTS "${_lib}")
            message(FATAL_ERROR "A required The Forge link library is missing from the pinned tree: ${_lib}")
        endif()
    endforeach()

    set(_gkcore_runtime_dlls
        "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/Direct3d12Agility/bin/x64/D3D12Core.dll"
        "${GKCORE_DXC_ROOT}/bin/x64/dxcompiler.dll"
        "${GKCORE_DXC_ROOT}/bin/x64/dxil.dll"
        "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/ags/ags_lib/lib/amd_ags_x64.dll"
        "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/winpixeventruntime/bin/WinPixEventRuntime.dll")
    if(CMAKE_CONFIGURATION_TYPES STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_gkcore_runtime_debug_layer
            "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/Direct3d12Agility/bin/x64/d3d12SDKLayers.dll")
        if(NOT EXISTS "${_gkcore_runtime_debug_layer}")
            message(FATAL_ERROR "The pinned The Forge D3D12 debug layer is missing: ${_gkcore_runtime_debug_layer}")
        endif()
        list(APPEND _gkcore_runtime_dlls "${_gkcore_runtime_debug_layer}")
    endif()
    foreach(_dll IN LISTS _gkcore_runtime_dlls)
        if(NOT EXISTS "${_dll}")
            message(FATAL_ERROR "A required The Forge Runtime DLL is missing from the pinned tree: ${_dll}")
        endif()
    endforeach()
    set(_gkcore_runtime_gpu_data "${_gkcore_forge_common}/OS/Windows/pc_gpu.data")
    set(_gkcore_runtime_gpu_config "${GKCORE_FORGE_ROOT}/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg")
    foreach(_runtime_config IN ITEMS "${_gkcore_runtime_gpu_data}" "${_gkcore_runtime_gpu_config}")
        if(NOT EXISTS "${_runtime_config}")
            message(FATAL_ERROR "A required The Forge Runtime GPU configuration file is missing: ${_runtime_config}")
        endif()
    endforeach()

    add_library(Forge::Renderer STATIC IMPORTED GLOBAL)
    set_target_properties(Forge::Renderer PROPERTIES
        IMPORTED_LOCATION "${GKCORE_FORGE_BUILD_DIR}/Renderer/Renderer.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${GKCORE_FORGE_ROOT};${_gkcore_forge_common}")
    add_library(Forge::OS STATIC IMPORTED GLOBAL)
    set_target_properties(Forge::OS PROPERTIES
        IMPORTED_LOCATION "${GKCORE_FORGE_BUILD_DIR}/OS/OS.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${GKCORE_FORGE_ROOT};${_gkcore_forge_common}")

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/ForgeBackend.cpp")
        message(FATAL_ERROR "The real The Forge backend source is missing; refusing to package a placeholder Runtime.")
    endif()

    add_library(gkcore SHARED
        ${GKCORE_CORE_SOURCES}
        src/platform/WindowsWindow.cpp
        src/platform/WindowsText.cpp
        src/render/ForgeRenderer.cpp
        src/render/TextureCache.cpp
        src/render/PostProcessRenderer.cpp
        src/render/PostEffectRenderer.cpp
        src/render/CustomShaders.cpp
        src/render/ModelLightingRenderer.cpp
        src/render/ShaderReflection.cpp
        src/render/ShaderInterface.cpp
        src/backends/ForgeBackend.cpp)
    add_library(gkcore::gkcore ALIAS gkcore)
    target_compile_features(gkcore PUBLIC cxx_std_17)
    target_include_directories(gkcore
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${GKCORE_FORGE_ROOT}
            ${_gkcore_forge_common}
            ${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/inc)
    target_compile_definitions(gkcore
        PUBLIC GKCORE_SHARED=1
        PRIVATE GKCORE_BUILDING_LIBRARY=1 _WINDOWS DIRECT3D12 D3D12_AGILITY_SDK=1 D3D12_AGILITY_SDK_VERSION=715 _HAS_EXCEPTIONS=0)
    target_compile_options(gkcore INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    gkcore_enable_warnings(gkcore)
    target_link_libraries(gkcore PRIVATE
        Forge::Renderer
        Forge::OS
        ${_gkcore_forge_libs}
        d3d12 dxgi dxguid d3dcompiler user32 gdi32 shell32 shlwapi ole32 advapi32 winmm setupapi ws2_32 Xinput9_1_0)
    set_target_properties(gkcore PROPERTIES
        OUTPUT_NAME gkcore
        EXPORT_NAME gkcore
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)

    if(GKCORE_BUILD_TESTS AND GKCORE_RUN_BACKEND_SMOKE)
        # 配布DLLには画像取得コードを含めず、同じソースから検査用DLLを別に作る。
        get_target_property(_gkcore_capture_sources gkcore SOURCES)
        add_library(gkcore_capture_runtime SHARED ${_gkcore_capture_sources} tests/support/FrameCapture.cpp)
        foreach(_capture_property IN ITEMS INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES)
            get_target_property(_capture_value gkcore ${_capture_property})
            set_property(TARGET gkcore_capture_runtime PROPERTY ${_capture_property} "${_capture_value}")
        endforeach()
        target_compile_features(gkcore_capture_runtime PUBLIC cxx_std_17)
        target_compile_definitions(gkcore_capture_runtime PRIVATE GKCORE_TEST_FRAME_CAPTURE=1)
        target_compile_definitions(gkcore_capture_runtime INTERFACE GKCORE_SHARED=1)
        target_include_directories(gkcore_capture_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_options(gkcore_capture_runtime INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    endif()

    add_executable(gkcore_mixed_scene examples/mixed_scene.cpp)
    target_compile_features(gkcore_mixed_scene PRIVATE cxx_std_17)
    target_link_libraries(gkcore_mixed_scene PRIVATE gkcore::gkcore)

    add_executable(gkcore_rectangle_outline examples/rectangle_outline.cpp)
    target_compile_features(gkcore_rectangle_outline PRIVATE cxx_std_17)
    target_link_libraries(gkcore_rectangle_outline PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_rectangle_outline)

    add_executable(gkcore_custom_post_effect examples/custom_post_effect.cpp)
    target_compile_features(gkcore_custom_post_effect PRIVATE cxx_std_17)
    target_link_libraries(gkcore_custom_post_effect PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_custom_post_effect)
    add_custom_command(TARGET gkcore_custom_post_effect POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/shaders/post_effect_tint.frag"
            "$<TARGET_FILE_DIR:gkcore_custom_post_effect>/post_effect_tint.frag")

    add_executable(gkcore_model_lighting examples/model_lighting.cpp)
    target_compile_features(gkcore_model_lighting PRIVATE cxx_std_17)
    target_link_libraries(gkcore_model_lighting PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_model_lighting)
    add_custom_command(TARGET gkcore_model_lighting POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/assets/model_lighting.glb"
            "$<TARGET_FILE_DIR:gkcore_model_lighting>/model_lighting.glb")

    if(GKCORE_BUILD_TESTS)
        add_executable(gkcore_shader_reflection_tests
            tests/shader_reflection_tests.cpp
            src/render/ShaderReflection.cpp
            src/render/ShaderInterface.cpp
            src/render/Shaders.cpp
            src/resources/ResourceIO.cpp
            src/foundation/Memory.cpp
            src/foundation/Array.cpp
            src/foundation/String.cpp)
        target_compile_features(gkcore_shader_reflection_tests PRIVATE cxx_std_17)
        target_include_directories(gkcore_shader_reflection_tests PRIVATE
            include src ${GKCORE_FORGE_ROOT} ${_gkcore_forge_common}
            ${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/inc)
        target_compile_definitions(gkcore_shader_reflection_tests PRIVATE
            DIRECT3D12 GKCORE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(gkcore_shader_reflection_tests PRIVATE
            "${GKCORE_DXC_ROOT}/lib/x64/dxcompiler.lib" dxguid ole32)
        gkcore_enable_warnings(gkcore_shader_reflection_tests)
        add_custom_command(TARGET gkcore_shader_reflection_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GKCORE_DXC_ROOT}/bin/x64/dxcompiler.dll"
                "$<TARGET_FILE_DIR:gkcore_shader_reflection_tests>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GKCORE_DXC_ROOT}/bin/x64/dxil.dll"
                "$<TARGET_FILE_DIR:gkcore_shader_reflection_tests>")
        add_test(NAME gkcore.shader_reflection COMMAND gkcore_shader_reflection_tests)
        add_test(NAME gkcore.model_shader_vertex_reflection
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_contract_tests.py
                --artifact ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/shaders/gkcore_model.vert
                --dxc ${GKCORE_DXC_ROOT}/bin/x64/dxc.exe
                --require-reflection --model-vertex)
        add_test(NAME gkcore.model_shader_pixel_reflection
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_contract_tests.py
                --artifact ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/shaders/gkcore_model.frag
                --dxc ${GKCORE_DXC_ROOT}/bin/x64/dxc.exe
                --require-reflection --model-pixel)

        add_executable(gkcore_backend_smoke tests/backend_contract_tests.cpp)
        target_compile_features(gkcore_backend_smoke PRIVATE cxx_std_17)
        target_compile_definitions(gkcore_backend_smoke PRIVATE
            GKCORE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(gkcore_backend_smoke PRIVATE gkcore::gkcore)
        if(GKCORE_RUN_BACKEND_SMOKE)
            add_executable(gkcore_render_capture_tests tests/render_capture_tests.cpp)
            target_compile_features(gkcore_render_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_render_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_render_capture_tests)
            add_executable(gkcore_frame_stress_tests tests/frame_stress_tests.cpp)
            target_compile_features(gkcore_frame_stress_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_frame_stress_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_frame_stress_tests)
            add_executable(gkcore_post_effect_capture_tests tests/post_effect_capture_tests.cpp)
            target_compile_features(gkcore_post_effect_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_post_effect_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_post_effect_capture_tests)
            add_executable(gkcore_image_lifetime_capture_tests tests/image_lifetime_capture_tests.cpp)
            target_compile_features(gkcore_image_lifetime_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_image_lifetime_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_image_lifetime_capture_tests)
            # 定数と画像の検査用shaderは開発用buildにだけ出力する。
            set(_gkcore_binding_shader "${CMAKE_CURRENT_BINARY_DIR}/shader-binding-fixture/gkcore_binding_capture.frag")
            add_custom_command(OUTPUT "${_gkcore_binding_shader}"
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_pixel_shader.py
                    --dxc-root "${GKCORE_DXC_ROOT}"
                    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/shaders/gkcore_binding_capture.hlsl
                    --output "${_gkcore_binding_shader}"
                DEPENDS tests/assets/shaders/gkcore_binding_capture.hlsl include/gkcore/Shader.hlsl
                    tools/compile_pixel_shader.py tools/verify_dxc.py cmake/dxc-lock.json
                VERBATIM)
            add_custom_target(gkcore_shader_binding_fixture DEPENDS "${_gkcore_binding_shader}")
            add_executable(gkcore_shader_binding_capture_tests tests/shader_binding_capture_tests.cpp)
            target_compile_features(gkcore_shader_binding_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_shader_binding_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_shader_binding_capture_tests)
            add_dependencies(gkcore_shader_binding_capture_tests gkcore_shader_binding_fixture)
            add_test(NAME gkcore.backend_smoke COMMAND gkcore_backend_smoke)
            set_tests_properties(gkcore.backend_smoke PROPERTIES LABELS "windows;gpu" TIMEOUT 60 RUN_SERIAL TRUE)
            add_test(NAME gkcore.render_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_render_capture_tests>
                    --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/render-captures/$<CONFIG>)
            set_tests_properties(gkcore.render_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 180 RUN_SERIAL TRUE)
            add_test(NAME gkcore.frame_stress
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/frame_stress_tests.py
                    --executable $<TARGET_FILE:gkcore_frame_stress_tests>
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/frame-stress/$<CONFIG>)
            set_tests_properties(gkcore.frame_stress PROPERTIES LABELS "windows;gpu" TIMEOUT 150 RUN_SERIAL TRUE)
            add_test(NAME gkcore.post_effect_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/post_effect_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_post_effect_capture_tests>
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/post-effect-captures/$<CONFIG>)
            set_tests_properties(gkcore.post_effect_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 180 RUN_SERIAL TRUE)
            add_test(NAME gkcore.image_lifetime_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/image_lifetime_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_image_lifetime_capture_tests>
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/image-lifetime-captures/$<CONFIG>)
            set_tests_properties(gkcore.image_lifetime_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 180 RUN_SERIAL TRUE)
            add_test(NAME gkcore.shader_binding_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_binding_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_shader_binding_capture_tests>
                    --shader "${_gkcore_binding_shader}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/shader-binding-captures/$<CONFIG>)
            set_tests_properties(gkcore.shader_binding_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 180 RUN_SERIAL TRUE)
        endif()
    endif()

    add_custom_target(gkcore_stage_runtime_payload ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gkcore>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gkcore>/CompiledShaders/DIRECT3D12"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_gkcore_runtime_dlls}
            "$<TARGET_FILE_DIR:gkcore>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_gkcore_runtime_gpu_data}"
            "$<TARGET_FILE_DIR:gkcore>/gpu.data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_gkcore_runtime_gpu_config}"
            "$<TARGET_FILE_DIR:gkcore>/gpu.cfg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_gkcore_shader_artifacts}
            "$<TARGET_FILE_DIR:gkcore>/CompiledShaders/DIRECT3D12"
        VERBATIM)
    add_dependencies(gkcore_stage_runtime_payload gkcore)
    add_dependencies(gkcore_mixed_scene gkcore_stage_runtime_payload)
    add_dependencies(gkcore_rectangle_outline gkcore_stage_runtime_payload)
    add_dependencies(gkcore_custom_post_effect gkcore_stage_runtime_payload)
    add_dependencies(gkcore_model_lighting gkcore_stage_runtime_payload)
    if(TARGET gkcore_shader_reflection_tests)
        add_dependencies(gkcore_shader_reflection_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_backend_smoke)
        add_dependencies(gkcore_backend_smoke gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_render_capture_tests)
        add_dependencies(gkcore_render_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_frame_stress_tests)
        add_dependencies(gkcore_frame_stress_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_post_effect_capture_tests)
        add_dependencies(gkcore_post_effect_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_image_lifetime_capture_tests)
        add_dependencies(gkcore_image_lifetime_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_shader_binding_capture_tests)
        add_dependencies(gkcore_shader_binding_capture_tests gkcore_stage_runtime_payload)
    endif()

    install(TARGETS gkcore EXPORT gkcoreTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES include/gkcore.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Runtime)
    install(FILES include/gkcore/Handle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/Shader.hlsl DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES docs/THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/gkcore COMPONENT Runtime)
    install(FILES ${_gkcore_runtime_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
    install(FILES "${_gkcore_runtime_gpu_data}" DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME gpu.data COMPONENT Runtime)
    install(FILES "${_gkcore_runtime_gpu_config}" DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
    install(FILES ${_gkcore_shader_artifacts} DESTINATION ${CMAKE_INSTALL_BINDIR}/CompiledShaders/DIRECT3D12 COMPONENT Runtime)
    set(_gkcore_licenses "${CMAKE_INSTALL_DATADIR}/licenses/gkcore")
    install(FILES "${GKCORE_FORGE_ROOT}/LICENSE" DESTINATION ${_gkcore_licenses} RENAME The-Forge-LICENSE.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/D3D12MemoryAllocator/LICENSE.txt" DESTINATION ${_gkcore_licenses} RENAME D3D12MemoryAllocator-LICENSE.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/D3D12MemoryAllocator/NOTICES.txt" DESTINATION ${_gkcore_licenses} RENAME D3D12MemoryAllocator-NOTICES.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/Direct3d12Agility/LICENSE.txt" DESTINATION ${_gkcore_licenses} RENAME Direct3d12Agility-LICENSE.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/Direct3d12Agility/LICENSE-CODE.txt" DESTINATION ${_gkcore_licenses} RENAME Direct3d12Agility-LICENSE-CODE.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/ags/ags_lib/LICENSE.txt" DESTINATION ${_gkcore_licenses} RENAME AMD-AGS-LICENSE.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/winpixeventruntime/ThirdPartyNotices.txt" DESTINATION ${_gkcore_licenses} RENAME WinPix-ThirdPartyNotices.txt COMPONENT Runtime)
    install(FILES "${_gkcore_forge_common}/Graphics/ThirdParty/OpenSource/nvapi/docs/NVAPI_SDKs_Samples_and_Tools_License_Agreement(Public).pdf" DESTINATION ${_gkcore_licenses} RENAME NVAPI-SDK-License.pdf COMPONENT Runtime)
    install(FILES
        "${GKCORE_DXC_ROOT}/LICENSE-LLVM.txt"
        "${GKCORE_DXC_ROOT}/LICENSE-MIT.txt"
        "${GKCORE_DXC_ROOT}/LICENSE-MS.txt"
        DESTINATION ${_gkcore_licenses} COMPONENT Runtime)
    install(FILES
        third_party/stb/LICENSE
        DESTINATION ${_gkcore_licenses} RENAME stb-image-LICENSE.txt COMPONENT Runtime)
    install(FILES
        third_party/cgltf/LICENSE
        DESTINATION ${_gkcore_licenses} RENAME cgltf-LICENSE.txt COMPONENT Runtime)
    install(FILES
        third_party/ufbx/LICENSE
        DESTINATION ${_gkcore_licenses} RENAME ufbx-LICENSE.txt COMPONENT Runtime)
    install(EXPORT gkcoreTargets NAMESPACE gkcore:: FILE gkcoreTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gkcore COMPONENT Runtime)
    configure_package_config_file(cmake/gkcoreConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/gkcoreConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gkcore)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/gkcoreConfigVersion.cmake
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/gkcoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/gkcoreConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gkcore COMPONENT Runtime)

    if(GKCORE_BUILD_TESTS)
        set(_gkcore_package_gpu_args)
        if(GKCORE_RUN_BACKEND_SMOKE)
            list(APPEND _gkcore_package_gpu_args --gpu-smoke)
        endif()
        add_test(NAME gkcore.package
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/verify_runtime_package.py
                --config $<CONFIG>
                --generator "${CMAKE_GENERATOR}"
                --install-script "${CMAKE_COMMAND}"
                --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
                --source-dir "${CMAKE_CURRENT_SOURCE_DIR}" ${_gkcore_package_gpu_args})
        set_tests_properties(gkcore.package PROPERTIES TIMEOUT 120)
    endif()
else()
    install(CODE "message(FATAL_ERROR \"gkcore Runtime is unavailable: no real The Forge-backed runtime was built.\")" COMPONENT Runtime)
endif()
