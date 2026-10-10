cmake_minimum_required(VERSION 3.20)
project(gkcore VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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

set(GKCORE_MODEL_ANIMATION_LOADER_SOURCES
    src/model/animation/FModelAnimationAsset.cpp
    src/model/animation/ModelAnimationResources.cpp
    src/model/animation/ModelPose.cpp
    src/model/animation/GlbAnimation.cpp
    src/model/animation/GlbMorphGeometry.cpp
    src/model/animation/FbxAnimation.cpp)

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
    src/resources/TextureSampler.cpp
    src/resources/ResourceIO.cpp
    src/image/Image.cpp
    src/image/ImageLoader.cpp
    src/model/Model.cpp
    src/model/ModelMaterial.cpp
    src/model/ModelLoader.cpp
    src/model/ObjLoader.cpp
    src/model/GlbLoader.cpp
    src/model/ModelTangents.cpp
    src/model/ModelNormals.cpp
    src/model/MikkTSpace.cpp
    src/model/Cgltf.cpp
    src/model/FbxLoader.cpp
    src/model/FbxGeometry.cpp
    src/model/FbxMaterial.cpp
    src/model/Ufbx.cpp
    ${GKCORE_MODEL_ANIMATION_LOADER_SOURCES}
    src/model/animation/ModelIk.cpp
    src/model/animation/ModelSecondaryMotionSolver.cpp
    src/model/animation/ModelSecondaryMotionCollision.cpp
    src/model/animation/ModelSecondaryMotionColliders.cpp
    src/model/animation/ModelSecondaryMotion.cpp
    src/model/animation/FModelSecondaryMotionState.cpp
    src/model/animation/ObjSequence.cpp
    src/model/animation/FModelPlayback.cpp
    src/model/animation/ModelAnimationBinding.cpp
    src/model/animation/ModelSnapshot.cpp
    src/model/animation/FModelDeferredPose.cpp
    src/model/animation/ModelAnimation.cpp
    src/model/animation/HumanoidMapping.cpp
    src/model/animation/HumanoidBoneMap.cpp
    src/text/Text.cpp
    src/text/TextLayout.cpp
    src/text/TextCache.cpp
    src/effects/Effects.cpp
    src/effects/Lighting.cpp
    src/effects/Shaders.cpp
    src/render/Geometry.cpp
    src/render/FModelSkinningRenderer.cpp
    src/render/FModelSkinningGeometryCache.cpp
    src/render/RectangleGeometry.cpp
    src/render/Shaders.cpp
    src/render/ShaderAbi.cpp
    src/render/PostProcess.cpp
    src/render/PostProcessPlan.cpp
    src/render/PostEffectPlan.cpp
    src/render/ModelDrawPlan.cpp
    src/render/ModelTransparency.cpp
    src/render/LightingAbi.cpp
    src/render/WorldGeometry.cpp
    src/render/ModelGeometry.cpp
    src/render/ModelDrawConstants.cpp
    src/render/ModelPoseGeometry.cpp
    src/render/CustomShaderPolicy.cpp
    src/render/TextureCachePolicy.cpp
    src/render/TextureMipChain.cpp)

function(gkcore_enable_warnings target)
    # 自作ソースはmodule-rootから参照し、開発用includeは公開SDKへ伝播させない。
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/third_party" "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()


if(GKCORE_BUILD_RUNTIME)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The Runtime SDK requires Windows and MSVC. Linux and other platforms cannot build the Windows Runtime.")
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
        "${GKCORE_SHADER_BUILD_DIR}/CompiledShaders/DIRECT3D12/gkcore_model_skinning.comp"
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
    set(_gkcore_runtime_gpu_config "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gpu.cfg")
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
        src/render/FModelGeometryCache.cpp
        src/render/FModelSparseMapCache.cpp
        src/render/ModelLocalGeometry.cpp
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


    add_executable(gkcore_mixed_scene examples/mixed_scene.cpp)
    target_compile_features(gkcore_mixed_scene PRIVATE cxx_std_17)
    target_link_libraries(gkcore_mixed_scene PRIVATE gkcore::gkcore)

    add_executable(gkcore_rectangle_outline examples/rectangle_outline.cpp)
    target_compile_features(gkcore_rectangle_outline PRIVATE cxx_std_17)
    target_link_libraries(gkcore_rectangle_outline PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_rectangle_outline)

    set(_gkcore_custom_post_shader "${CMAKE_CURRENT_BINARY_DIR}/post_effect_tint.frag")
    add_custom_command(OUTPUT "${_gkcore_custom_post_shader}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_pixel_shader.py"
            --dxc-root "${GKCORE_DXC_ROOT}"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/post_effect_tint.hlsl"
            --output "${_gkcore_custom_post_shader}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/post_effect_tint.hlsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_pixel_shader.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_dxc.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dxc-lock.json"
        VERBATIM)
    add_custom_target(gkcore_custom_post_shader DEPENDS "${_gkcore_custom_post_shader}")
    add_executable(gkcore_custom_post_effect examples/custom_post_effect.cpp)
    add_dependencies(gkcore_custom_post_effect gkcore_custom_post_shader)
    target_compile_features(gkcore_custom_post_effect PRIVATE cxx_std_17)
    target_link_libraries(gkcore_custom_post_effect PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_custom_post_effect)
    add_custom_command(TARGET gkcore_custom_post_effect POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_gkcore_custom_post_shader}"
            "$<TARGET_FILE_DIR:gkcore_custom_post_effect>/post_effect_tint.frag")

    add_executable(gkcore_model_lighting examples/model_lighting.cpp)
    target_compile_features(gkcore_model_lighting PRIVATE cxx_std_17)
    target_link_libraries(gkcore_model_lighting PRIVATE gkcore::gkcore)
    add_executable(gkcore_model_viewer examples/model_viewer.cpp examples/support/FHumanoidMapOptions.cpp examples/support/ModelMappingReport.cpp examples/support/FModelArmIkPreview.cpp examples/support/FModelMotionView.cpp examples/support/ModelSecondaryMotionPreview.cpp)
    target_compile_features(gkcore_model_viewer PRIVATE cxx_std_17)
    target_link_libraries(gkcore_model_viewer PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_model_viewer)


    gkcore_enable_warnings(gkcore_model_lighting)
    add_custom_command(TARGET gkcore_model_lighting POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/assets/model_lighting.glb"
            "$<TARGET_FILE_DIR:gkcore_model_lighting>/model_lighting.glb")


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









    if(TARGET gkcore_model_viewer)
        add_dependencies(gkcore_model_viewer gkcore_stage_runtime_payload)
    endif()

    install(TARGETS gkcore EXPORT gkcoreTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES include/gkcore.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Runtime)
    install(FILES include/gkcore/Handle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/ModelAnimation.h include/gkcore/HumanoidBoneTypes.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/ModelSecondaryMotion.h include/gkcore/FModelSecondaryMotionSettings.h include/gkcore/FModelSecondaryMotionCollider.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/ModelAnimationMappingInfo.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/ModelMaterial.h include/gkcore/ModelMaterialSettings.h include/gkcore/ModelAlphaModeTypes.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
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
    install(FILES
        third_party/mikktspace/LICENSE
        DESTINATION ${_gkcore_licenses} RENAME mikktspace-LICENSE.txt COMPONENT Runtime)
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

else()
    install(CODE "message(FATAL_ERROR \"gkcore Runtime is unavailable: no real The Forge-backed runtime was built.\")" COMPONENT Runtime)
endif()
