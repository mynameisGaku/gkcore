cmake_minimum_required(VERSION 3.20)
project(gkcore VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(GKCORE_BUILD_TESTS "Build dependency-light development tests" ON)
option(GKCORE_RUN_BACKEND_SMOKE "Run the Windows Direct3D 12 backend smoke test on the host GPU" OFF)
option(GKCORE_RENDER_PERFORMANCE_METRICS "Compile development-only render timing instrumentation" OFF)
option(GKCORE_RUN_INTERACTIVE_TESTS "Run tests that open visible application windows" OFF)
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

if(GKCORE_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME gkcore.game_repository COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_game_repository.py")

    set(_gkcore_animation_math_sources src/model/animation/ModelPose.cpp src/model/animation/ModelIk.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    foreach(_animation_test pose ik)
        add_executable(gkcore_model_${_animation_test}_tests tests/model_${_animation_test}_tests.cpp ${_gkcore_animation_math_sources})
        target_compile_features(gkcore_model_${_animation_test}_tests PRIVATE cxx_std_17)
        target_include_directories(gkcore_model_${_animation_test}_tests PRIVATE include src)
        gkcore_enable_warnings(gkcore_model_${_animation_test}_tests)
        add_test(NAME gkcore.model_${_animation_test} COMMAND gkcore_model_${_animation_test}_tests)
    endforeach()
    add_executable(gkcore_model_animation_binding_tests tests/model_animation_binding_tests.cpp
        src/model/animation/ModelAnimationBinding.cpp src/model/animation/FModelAnimationAsset.cpp
        src/model/animation/ModelPose.cpp src/foundation/RefCount.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_model_animation_binding_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_animation_binding_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_model_animation_binding_tests)
    add_test(NAME gkcore.model_animation_binding COMMAND gkcore_model_animation_binding_tests)
    add_executable(gkcore_model_humanoid_mapping_tests tests/model_humanoid_mapping_tests.cpp
        src/model/animation/HumanoidMapping.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_model_humanoid_mapping_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_humanoid_mapping_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_model_humanoid_mapping_tests)
    add_test(NAME gkcore.model_humanoid_mapping COMMAND gkcore_model_humanoid_mapping_tests)

    add_executable(gkcore_humanoid_bone_map_tests tests/humanoid_bone_map_tests.cpp
        src/model/animation/HumanoidBoneMap.cpp src/resources/ResourceIO.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_humanoid_bone_map_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_humanoid_bone_map_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_humanoid_bone_map_tests)
    add_test(NAME gkcore.humanoid_bone_map COMMAND gkcore_humanoid_bone_map_tests)

    add_executable(gkcore_humanoid_map_options_tests tests/humanoid_map_options_tests.cpp examples/support/FHumanoidMapOptions.cpp)
    target_compile_features(gkcore_humanoid_map_options_tests PRIVATE cxx_std_17)
    gkcore_enable_warnings(gkcore_humanoid_map_options_tests)
    add_test(NAME gkcore.humanoid_map_options COMMAND gkcore_humanoid_map_options_tests)

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
            "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_CURRENT_SOURCE_DIR}/src"
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
        src/resources/TextureSampler.cpp
        src/render/ModelDrawPlan.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_model_draw_plan_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_draw_plan_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_model_draw_plan_tests)
    add_test(NAME gkcore.model_draw_plan COMMAND gkcore_model_draw_plan_tests)
    add_executable(gkcore_model_transparency_tests tests/model_transparency_tests.cpp
        src/render/ModelTransparency.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp
        src/render/ModelDrawPlan.cpp src/resources/TextureSampler.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_model_transparency_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_transparency_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_model_transparency_tests)
    add_test(NAME gkcore.model_transparency COMMAND gkcore_model_transparency_tests)
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
    add_executable(gkcore_model_draw_constants_tests
        tests/model_draw_constants_tests.cpp
        src/render/ModelDrawConstants.cpp
        src/render/WorldGeometry.cpp
        src/render/PostProcess.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_model_draw_constants_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_draw_constants_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_model_draw_constants_tests)
    add_test(NAME gkcore.model_draw_constants COMMAND gkcore_model_draw_constants_tests)
    add_executable(gkcore_model_pose_geometry_tests
        tests/model_pose_geometry_tests.cpp
        src/render/ModelPoseGeometry.cpp
        src/foundation/Memory.cpp
        src/foundation/Array.cpp
        src/foundation/String.cpp)
    target_compile_features(gkcore_model_pose_geometry_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_pose_geometry_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_pose_geometry_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_pose_geometry_tests)
    add_test(NAME gkcore.model_pose_geometry COMMAND gkcore_model_pose_geometry_tests)
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
    add_executable(gkcore_model_animation_api_tests tests/model_animation_api_tests.cpp ${GKCORE_CORE_SOURCES})
    target_compile_features(gkcore_model_animation_api_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_animation_api_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_animation_api_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_animation_api_tests)
    add_test(NAME gkcore.model_animation_api COMMAND gkcore_model_animation_api_tests "${CMAKE_CURRENT_BINARY_DIR}/model-animation-api-fixtures")
    add_executable(gkcore_model_animation_rig_api_tests tests/model_animation_rig_api_tests.cpp ${GKCORE_CORE_SOURCES})
    target_compile_features(gkcore_model_animation_rig_api_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_animation_rig_api_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_animation_rig_api_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_animation_rig_api_tests)
    add_test(NAME gkcore.model_animation_rig_api COMMAND gkcore_model_animation_rig_api_tests)
    add_executable(gkcore_model_material_api_tests tests/model_material_api_tests.cpp ${GKCORE_CORE_SOURCES})
    target_compile_features(gkcore_model_material_api_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_material_api_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_material_api_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_material_api_tests)
    add_test(NAME gkcore.model_material_api COMMAND gkcore_model_material_api_tests "${CMAKE_CURRENT_BINARY_DIR}/model-material-api-fixtures")
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
        src/resources/TextureSampler.cpp
        src/resources/ResourceIO.cpp
        src/image/Image.cpp
        src/image/ImageLoader.cpp
        src/model/Model.cpp
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
        ${GKCORE_MODEL_ANIMATION_LOADER_SOURCES})
    target_compile_features(gkcore_fbx_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_fbx_tests PRIVATE include src third_party/ufbx)
    target_compile_definitions(gkcore_fbx_tests PRIVATE
        GKCORE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    gkcore_enable_warnings(gkcore_fbx_tests)
    add_test(NAME gkcore.fbx COMMAND gkcore_fbx_tests)
    get_target_property(_gkcore_animation_loader_sources gkcore_fbx_tests SOURCES)
    list(REMOVE_ITEM _gkcore_animation_loader_sources tests/fbx_tests.cpp)
    add_executable(gkcore_local_model_inspect tests/local_model_inspect.cpp ${_gkcore_animation_loader_sources})
    target_compile_features(gkcore_local_model_inspect PRIVATE cxx_std_17)
    target_compile_definitions(gkcore_local_model_inspect PRIVATE GKCORE_TESTING=1)
    target_include_directories(gkcore_local_model_inspect PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_local_model_inspect)
    foreach(_format glb fbx)
        set(_animation_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-${_format}-animation-fixtures")
        if(_format STREQUAL "glb")
            set(_animation_script tests/support/model_animation_fixtures.py)
            set(_animation_fixture_options)
            set(_animation_fixture_dependencies tests/support/model_normal_fixtures.py)
        else()
            set(_animation_script tests/support/fbx_animation_fixtures.py)
            set(_animation_fixture_options --source-model "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/models/gkcore_ascii.fbx")
            set(_animation_fixture_dependencies tests/assets/models/gkcore_ascii.fbx)
        endif()
        add_custom_target(gkcore_model_${_format}_animation_fixtures
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/${_animation_script} --output-dir "${_animation_fixture_dir}" ${_animation_fixture_options}
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_animation_script}" ${_animation_fixture_dependencies})
        add_executable(gkcore_model_${_format}_animation_tests tests/model_${_format}_animation_tests.cpp ${_gkcore_animation_loader_sources})
        target_compile_features(gkcore_model_${_format}_animation_tests PRIVATE cxx_std_17)
        if(_format STREQUAL "fbx")
            target_compile_definitions(gkcore_model_${_format}_animation_tests PRIVATE GKCORE_TESTING=1)
        endif()
        target_include_directories(gkcore_model_${_format}_animation_tests PRIVATE include src third_party/ufbx)
        gkcore_enable_warnings(gkcore_model_${_format}_animation_tests)
        add_dependencies(gkcore_model_${_format}_animation_tests gkcore_model_${_format}_animation_fixtures)
        add_test(NAME gkcore.model_${_format}_animation COMMAND gkcore_model_${_format}_animation_tests "${_animation_fixture_dir}")
    endforeach()
    # GLBの材質が選んだ画像座標を、描画部を使わずに検査する。
    set(_gkcore_glb_texture_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/glb-texture-fixtures")
    set(_gkcore_glb_texture_fixture_files)
    foreach(_fixture_name IN ITEMS default uv0 uv1 uv2 normalized-uv1 normalized-byte-uv1 missing-selected negative-selected wrong-selected-count unnormalized-uv1 signed-uv1 float-normalized-uv1 transformed-selected)
        list(APPEND _gkcore_glb_texture_fixture_files "${_gkcore_glb_texture_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_glb_texture_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/glb_texture_fixtures.py
            --output-dir "${_gkcore_glb_texture_fixture_dir}"
        DEPENDS tests/support/glb_texture_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_glb_texture_fixtures DEPENDS ${_gkcore_glb_texture_fixture_files})
    get_target_property(_gkcore_resource_test_sources gkcore_fbx_tests SOURCES)
    list(REMOVE_ITEM _gkcore_resource_test_sources tests/fbx_tests.cpp)
    add_executable(gkcore_glb_texture_coordinate_tests tests/glb_texture_coordinate_tests.cpp ${_gkcore_resource_test_sources})
    target_compile_features(gkcore_glb_texture_coordinate_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_glb_texture_coordinate_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_glb_texture_coordinate_tests)
    add_dependencies(gkcore_glb_texture_coordinate_tests gkcore_glb_texture_fixtures)
    add_test(NAME gkcore.glb_texture_coordinates COMMAND gkcore_glb_texture_coordinate_tests "${_gkcore_glb_texture_fixture_dir}")
    # アルファ抜きの材質情報と、描画へ渡す値を同じGLBで確認する。
    set(_gkcore_model_alpha_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-alpha-fixtures")
    set(_gkcore_model_alpha_fixture_files)
    foreach(_fixture_name IN ITEMS opaque-default opaque opaque-cutoff-two mask-default mask-zero mask-one mask-two mask-factor-half mask-factor-quarter mask-equality mask-no-texture mixed-materials blend negative-cutoff nonfinite-cutoff)
        list(APPEND _gkcore_model_alpha_fixture_files "${_gkcore_model_alpha_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_alpha_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_alpha_fixtures.py
            --output-dir "${_gkcore_model_alpha_fixture_dir}"
        DEPENDS tests/support/model_alpha_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_alpha_fixtures DEPENDS ${_gkcore_model_alpha_fixture_files})
    add_executable(gkcore_model_alpha_tests tests/model_alpha_tests.cpp ${_gkcore_resource_test_sources}
        src/render/ModelDrawPlan.cpp src/render/ModelGeometry.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp)
    target_compile_features(gkcore_model_alpha_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_alpha_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_alpha_tests)
    add_dependencies(gkcore_model_alpha_tests gkcore_model_alpha_fixtures)
    add_test(NAME gkcore.model_alpha COMMAND gkcore_model_alpha_tests "${_gkcore_model_alpha_fixture_dir}")
    # 基本色と金属度・粗さの画像を独立して保持するGLBを検査する。
    set(_gkcore_model_material_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-material-fixtures")
    set(_gkcore_model_material_fixture_files)
    foreach(_fixture_name IN ITEMS no-mr-default uniform-mr factor-reference ignored-ra scaled-mr scaled-reference shared-image shared-reference mr-only mr-only-reference uv1-pattern uv1-reference mixed-pairs mixed-reference masked-mr missing-mr-uv transform-mr bad-mr-image bad-mr-index)
        list(APPEND _gkcore_model_material_fixture_files "${_gkcore_model_material_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_material_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_material_fixtures.py
            --output-dir "${_gkcore_model_material_fixture_dir}"
        DEPENDS tests/support/model_material_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_material_fixtures DEPENDS ${_gkcore_model_material_fixture_files})
    add_executable(gkcore_model_material_tests tests/model_material_tests.cpp ${_gkcore_resource_test_sources}
        src/render/ModelDrawPlan.cpp src/render/ModelGeometry.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp)
    target_compile_features(gkcore_model_material_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_material_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_material_tests)
    add_dependencies(gkcore_model_material_tests gkcore_model_material_fixtures)
    add_test(NAME gkcore.model_material COMMAND gkcore_model_material_tests "${_gkcore_model_material_fixture_dir}")

    set(_gkcore_model_normal_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-normal-fixtures")
    set(_gkcore_model_normal_fixture_files)
    foreach(_fixture_name IN ITEMS no-normal uniform-normal normal-reference alpha-ignored scale-zero scale-two scale-two-reference scale-negative scale-negative-reference mirrored-tangent mirrored-reference uv1-pattern uv1-reference shared-image shared-reference node-mirror-normal node-mirror-reference tiny-node-scale mixed-sign-triangles shared-image-aliases distinct-image-records mixed-role-image-alias alias-missing-uv alias-transform missing-tangent missing-normal missing-normal-uv bad-tangent-w tangent-w-mismatch parallel-tangent zero-tangent singular-node bad-normal-scale transform-normal)
        list(APPEND _gkcore_model_normal_fixture_files "${_gkcore_model_normal_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_normal_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_normal_fixtures.py
            --output-dir "${_gkcore_model_normal_fixture_dir}"
        DEPENDS tests/support/model_normal_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_normal_fixtures DEPENDS ${_gkcore_model_normal_fixture_files})
    add_executable(gkcore_model_normal_tests tests/model_normal_tests.cpp ${_gkcore_resource_test_sources}
        src/render/ModelDrawPlan.cpp src/render/ModelGeometry.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp)
    target_compile_features(gkcore_model_normal_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_normal_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_normal_tests)
    add_dependencies(gkcore_model_normal_tests gkcore_model_normal_fixtures)
    add_test(NAME gkcore.model_normal COMMAND gkcore_model_normal_tests "${_gkcore_model_normal_fixture_dir}")

    set(_gkcore_model_sampler_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-sampler-fixtures")
    set(_gkcore_model_sampler_fixture_files)
    foreach(_fixture_name IN ITEMS sampler-defaults sampler-explicit sampler-min-fallbacks-a sampler-min-fallbacks-b bad-sampler-wrap bad-sampler-mag bad-sampler-min bad-sampler-reference sampler-wrap-repeat sampler-wrap-repeat-reference sampler-wrap-mirror sampler-wrap-mirror-reference sampler-wrap-clamp sampler-wrap-clamp-reference sampler-wrap-u-repeat-v-clamp sampler-wrap-u-repeat-v-clamp-reference sampler-wrap-u-clamp-v-repeat sampler-wrap-u-clamp-v-repeat-reference sampler-wrap-default-repeat sampler-wrap-default-repeat-reference sampler-wrap-negative-integer sampler-wrap-negative-integer-reference sampler-wrap-positive-integer sampler-wrap-positive-integer-reference sampler-batch-mixed sampler-batch-mixed-reference sampler-role-mixed sampler-role-mixed-reference sampler-mag-nearest sampler-mag-nearest-reference sampler-mag-linear sampler-mag-linear-reference sampler-min-nearest sampler-min-nearest-reference sampler-min-linear sampler-min-linear-reference)
        list(APPEND _gkcore_model_sampler_fixture_files "${_gkcore_model_sampler_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_sampler_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_sampler_fixtures.py
            --output-dir "${_gkcore_model_sampler_fixture_dir}"
        DEPENDS tests/support/model_sampler_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_sampler_fixtures DEPENDS ${_gkcore_model_sampler_fixture_files})
    add_executable(gkcore_model_sampler_tests tests/model_sampler_tests.cpp ${_gkcore_resource_test_sources}
        src/render/ModelDrawPlan.cpp src/render/ModelGeometry.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp)
    target_compile_features(gkcore_model_sampler_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_sampler_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_sampler_tests)
    add_dependencies(gkcore_model_sampler_tests gkcore_model_sampler_fixtures)
    add_test(NAME gkcore.model_sampler COMMAND gkcore_model_sampler_tests "${_gkcore_model_sampler_fixture_dir}")

    set(_gkcore_model_emissive_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-emissive-fixtures")
    set(_gkcore_model_emissive_fixture_files)
    foreach(_fixture_name IN ITEMS emissive-batch-mixed emissive-batch-mixed-reference emissive-default emissive-default-reference emissive-factor emissive-factor-product emissive-factor-product-reference emissive-factor-reference emissive-hdr-bloom emissive-hdr-bloom-reference emissive-invalid-factor emissive-invalid-factor-overflow emissive-invalid-image-ref emissive-invalid-strength emissive-invalid-strength-overflow emissive-invalid-texcoord emissive-invalid-texture-ref emissive-mask emissive-mask-reference emissive-mip-9987 emissive-mip-9987-no-mip emissive-mip-9987-reference emissive-missing-uv emissive-shared-base emissive-shared-base-reference emissive-srgb-mid emissive-srgb-mid-reference emissive-stress emissive-stress-reference emissive-transform emissive-uniform-texture-alpha-ignored emissive-uniform-texture-alpha-ignored-reference emissive-uv1-pattern emissive-uv1-pattern-reference emissive-wrap-mirror emissive-wrap-mirror-reference emissive-zero-strength emissive-zero-strength-reference emissive-sampler-mixed emissive-sampler-mixed-reference emissive-factor-mixed emissive-factor-mixed-reference)
        list(APPEND _gkcore_model_emissive_fixture_files "${_gkcore_model_emissive_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_emissive_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_emissive_fixtures.py
            --output-dir "${_gkcore_model_emissive_fixture_dir}"
        DEPENDS tests/support/model_emissive_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_emissive_fixtures DEPENDS ${_gkcore_model_emissive_fixture_files})
    add_executable(gkcore_model_emissive_tests tests/model_emissive_tests.cpp ${_gkcore_resource_test_sources}
        src/render/ModelDrawPlan.cpp src/render/ModelGeometry.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp)
    target_compile_features(gkcore_model_emissive_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_emissive_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_emissive_tests)
    add_dependencies(gkcore_model_emissive_tests gkcore_model_emissive_fixtures)
    add_test(NAME gkcore.model_emissive COMMAND gkcore_model_emissive_tests "${_gkcore_model_emissive_fixture_dir}")

    set(_gkcore_model_occlusion_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-occlusion-fixtures")
    set(_gkcore_model_occlusion_fixture_files)
    foreach(_fixture_name IN ITEMS occlusion-default occlusion-default-reference occlusion-default-repeat occlusion-default-repeat-reference occlusion-direct-only occlusion-direct-only-reference occlusion-emission-only occlusion-emission-only-reference occlusion-invalid-image-ref occlusion-invalid-strength-high occlusion-invalid-strength-low occlusion-invalid-strength-overflow occlusion-invalid-texture-ref occlusion-mask occlusion-mask-reference occlusion-mip-9987 occlusion-mip-9987-no-mip occlusion-mip-9987-no-mip-reference occlusion-mip-9987-reference occlusion-missing-uv occlusion-mixed-lighting occlusion-mixed-lighting-reference occlusion-no-texture occlusion-no-texture-reference occlusion-r-values occlusion-r-values-reference occlusion-sampler-mixed occlusion-sampler-mixed-reference occlusion-shared-roles occlusion-shared-roles-reference occlusion-strength-0 occlusion-strength-0-reference occlusion-strength-1 occlusion-strength-1-reference occlusion-strength-half occlusion-strength-half-reference occlusion-strength-mixed occlusion-strength-mixed-reference occlusion-stress occlusion-stress-reference occlusion-transform occlusion-uv1-pattern occlusion-uv1-pattern-reference)
        list(APPEND _gkcore_model_occlusion_fixture_files "${_gkcore_model_occlusion_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_occlusion_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_occlusion_fixtures.py
            --output-dir "${_gkcore_model_occlusion_fixture_dir}"
        DEPENDS tests/support/model_occlusion_fixtures.py tests/support/model_emissive_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_occlusion_fixtures DEPENDS ${_gkcore_model_occlusion_fixture_files})
    add_executable(gkcore_model_occlusion_tests tests/model_occlusion_tests.cpp ${_gkcore_resource_test_sources}
        src/render/ModelDrawPlan.cpp src/render/ModelGeometry.cpp src/render/WorldGeometry.cpp src/render/PostProcess.cpp)
    target_compile_features(gkcore_model_occlusion_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_occlusion_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_occlusion_tests)
    add_dependencies(gkcore_model_occlusion_tests gkcore_model_occlusion_fixtures)
    add_test(NAME gkcore.model_occlusion COMMAND gkcore_model_occlusion_tests "${_gkcore_model_occlusion_fixture_dir}")

    set(_gkcore_model_mip_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-mip-fixtures")
    set(_gkcore_model_mip_fixture_files)
    foreach(_fixture_name IN ITEMS mip-min-9984 mip-min-9984-reference mip-min-9985 mip-min-9985-reference mip-min-9986 mip-min-9986-reference mip-min-9987 mip-min-9987-reference mip-no-mipmap mip-no-mipmap-reference mip-shared-roles mip-shared-roles-reference mip-linear-mr mip-linear-mr-reference mip-linear-normal mip-linear-normal-reference mip-npot-5x3 mip-npot-5x3-reference mip-npot-1x7 mip-npot-1x7-reference mip-sampler-stress mip-sampler-stress-reference mip-shared-roles-no-mipmap mip-linear-mr-no-mipmap mip-linear-normal-no-mipmap)
        list(APPEND _gkcore_model_mip_fixture_files "${_gkcore_model_mip_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_mip_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_mip_fixtures.py
            --output-dir "${_gkcore_model_mip_fixture_dir}"
        DEPENDS tests/support/model_mip_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_mip_fixtures DEPENDS ${_gkcore_model_mip_fixture_files})

    set(_gkcore_model_tangent_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-tangent-fixtures")
    set(_gkcore_model_tangent_fixture_files)
    foreach(_fixture_name IN ITEMS tangent-canonical tangent-canonical-reference tangent-degenerate-uv tangent-malformed-authored tangent-mirror-u tangent-mirror-u-reference tangent-missing-normal tangent-missing-normal-uv tangent-no-normal-map tangent-node-transform tangent-node-transform-reference tangent-non-indexed tangent-non-indexed-reference tangent-nonfinite-normal tangent-rotated-uv tangent-rotated-uv-reference tangent-shared-seam tangent-shared-seam-reference tangent-tiny-node-scale tangent-tiny-node-scale-reference tangent-unused-source-vertex tangent-unused-source-vertex-reference tangent-uv1 tangent-uv1-reference tangent-weighted-node tangent-weighted-node-reference tangent-weighted-node-wrong-order tangent-weighted-source tangent-weighted-source-reference tangent-zero-area)
        list(APPEND _gkcore_model_tangent_fixture_files "${_gkcore_model_tangent_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_tangent_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_tangent_fixtures.py
            --output-dir "${_gkcore_model_tangent_fixture_dir}"
        DEPENDS tests/support/model_tangent_fixtures.py tests/support/model_normal_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_tangent_fixtures DEPENDS ${_gkcore_model_tangent_fixture_files})
    add_executable(gkcore_model_tangent_tests tests/model_tangent_tests.cpp ${_gkcore_resource_test_sources})
    target_compile_features(gkcore_model_tangent_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_tangent_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_tangent_tests)
    add_dependencies(gkcore_model_tangent_tests gkcore_model_tangent_fixtures)
    add_test(NAME gkcore.model_tangent COMMAND gkcore_model_tangent_tests "${_gkcore_model_tangent_fixture_dir}")

    set(_gkcore_model_generated_normal_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/model-generated-normal-fixtures")
    set(_gkcore_model_generated_normal_fixture_files)
    foreach(_fixture_name IN ITEMS generated-normal-authored-normal-nonfinite generated-normal-authored-normal-zero generated-normal-authored-singular-no-map generated-normal-coplanar-shared generated-normal-coplanar-shared-reference generated-normal-degenerate-position generated-normal-degenerate-uv-no-map generated-normal-degenerate-uv-no-map-reference generated-normal-degenerate-uv-with-map generated-normal-flat generated-normal-flat-reference generated-normal-no-map generated-normal-no-map-reference generated-normal-no-map-tangent-ignored generated-normal-no-map-tangent-ignored-reference generated-normal-node-transform generated-normal-node-transform-reference generated-normal-non-indexed generated-normal-non-indexed-reference generated-normal-sharp-fold generated-normal-sharp-fold-averaged generated-normal-sharp-fold-reference generated-normal-singular-no-map generated-normal-tangent-ignored generated-normal-tangent-ignored-reference generated-normal-tiny-node generated-normal-tiny-node-reference generated-normal-unused-source-vertex generated-normal-unused-source-vertex-reference generated-normal-uv1 generated-normal-uv1-reference)
        list(APPEND _gkcore_model_generated_normal_fixture_files "${_gkcore_model_generated_normal_fixture_dir}/${_fixture_name}.glb")
    endforeach()
    add_custom_command(OUTPUT ${_gkcore_model_generated_normal_fixture_files}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/model_generated_normal_fixtures.py
            --output-dir "${_gkcore_model_generated_normal_fixture_dir}"
        DEPENDS tests/support/model_generated_normal_fixtures.py tests/support/model_tangent_fixtures.py tests/support/model_normal_fixtures.py
        VERBATIM)
    add_custom_target(gkcore_model_generated_normal_fixtures DEPENDS ${_gkcore_model_generated_normal_fixture_files})
    add_executable(gkcore_model_generated_normal_tests tests/model_generated_normal_tests.cpp ${_gkcore_resource_test_sources})
    target_compile_features(gkcore_model_generated_normal_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_generated_normal_tests PRIVATE include src third_party/ufbx)
    gkcore_enable_warnings(gkcore_model_generated_normal_tests)
    add_dependencies(gkcore_model_generated_normal_tests gkcore_model_generated_normal_fixtures)
    add_test(NAME gkcore.model_generated_normal COMMAND gkcore_model_generated_normal_tests "${_gkcore_model_generated_normal_fixture_dir}")

    add_test(NAME gkcore.mikktspace_lock COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/test_mikktspace_lock.py)

    add_executable(gkcore_model_normal_generation_tests
        tests/model_normal_generation_tests.cpp src/model/ModelNormals.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_model_normal_generation_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_normal_generation_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_normal_generation_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_normal_generation_tests)
    add_test(NAME gkcore.model_normal_generation COMMAND gkcore_model_normal_generation_tests)

    add_executable(gkcore_model_tangent_generation_tests
        tests/model_tangent_generation_tests.cpp src/model/ModelTangents.cpp src/model/MikkTSpace.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_model_tangent_generation_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_model_tangent_generation_tests PRIVATE include src)
    target_compile_definitions(gkcore_model_tangent_generation_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_model_tangent_generation_tests)
    add_test(NAME gkcore.model_tangent_generation COMMAND gkcore_model_tangent_generation_tests)

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
        src/render/ModelDrawPlan.cpp
        src/resources/TextureSampler.cpp)
    add_executable(gkcore_texture_cache_policy_tests
        tests/texture_cache_policy_tests.cpp
        src/render/TextureCachePolicy.cpp)
    target_compile_features(gkcore_texture_cache_policy_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_texture_cache_policy_tests PRIVATE include src)
    gkcore_enable_warnings(gkcore_texture_cache_policy_tests)
    add_test(NAME gkcore.texture_cache_policy COMMAND gkcore_texture_cache_policy_tests)
    add_executable(gkcore_texture_mip_chain_tests tests/texture_mip_chain_tests.cpp
        src/render/TextureMipChain.cpp src/render/PostProcess.cpp
        src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp)
    target_compile_features(gkcore_texture_mip_chain_tests PRIVATE cxx_std_17)
    target_include_directories(gkcore_texture_mip_chain_tests PRIVATE include src)
    target_compile_definitions(gkcore_texture_mip_chain_tests PRIVATE GKCORE_TESTING=1)
    gkcore_enable_warnings(gkcore_texture_mip_chain_tests)
    add_test(NAME gkcore.texture_mip_chain COMMAND gkcore_texture_mip_chain_tests)

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
    if(GKCORE_RENDER_PERFORMANCE_METRICS)
        target_compile_definitions(gkcore PRIVATE GKCORE_RENDER_PERFORMANCE_METRICS=1)
    endif()
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
        add_library(gkcore_capture_runtime SHARED ${_gkcore_capture_sources} tests/support/FrameCapture.cpp tests/support/FModelSkinningReadback.cpp)
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
    add_executable(gkcore_model_viewer examples/model_viewer.cpp examples/support/FHumanoidMapOptions.cpp)
    target_compile_features(gkcore_model_viewer PRIVATE cxx_std_17)
    target_link_libraries(gkcore_model_viewer PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_model_viewer)

    add_executable(gkcore_model_benchmark examples/model_viewer.cpp examples/support/FHumanoidMapOptions.cpp)
    target_compile_features(gkcore_model_benchmark PRIVATE cxx_std_17)
    target_compile_definitions(gkcore_model_benchmark PRIVATE GKCORE_MODEL_BENCHMARK=1)
    if(TARGET gkcore_capture_runtime)
        target_link_libraries(gkcore_model_benchmark PRIVATE gkcore_capture_runtime)
    else()
        target_link_libraries(gkcore_model_benchmark PRIVATE gkcore::gkcore)
    endif()
    gkcore_enable_warnings(gkcore_model_benchmark)

    add_executable(gkcore_model_benchmark_visible examples/model_viewer.cpp examples/support/FHumanoidMapOptions.cpp)
    target_compile_features(gkcore_model_benchmark_visible PRIVATE cxx_std_17)
    target_compile_definitions(gkcore_model_benchmark_visible PRIVATE GKCORE_MODEL_BENCHMARK=1)
    target_link_libraries(gkcore_model_benchmark_visible PRIVATE gkcore::gkcore)
    gkcore_enable_warnings(gkcore_model_benchmark_visible)
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
            add_executable(gkcore_glb_texture_capture_tests tests/glb_texture_capture_tests.cpp)
            target_compile_features(gkcore_glb_texture_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_glb_texture_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_glb_texture_capture_tests)
            add_dependencies(gkcore_glb_texture_capture_tests gkcore_glb_texture_fixtures)
            add_executable(gkcore_model_alpha_capture_tests tests/model_alpha_capture_tests.cpp)
            target_compile_features(gkcore_model_alpha_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_alpha_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_alpha_capture_tests)
            add_dependencies(gkcore_model_alpha_capture_tests gkcore_model_alpha_fixtures)
            add_executable(gkcore_model_animation_capture_tests tests/model_animation_capture_tests.cpp)
            target_compile_features(gkcore_model_animation_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_animation_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_animation_capture_tests)
            add_dependencies(gkcore_model_animation_capture_tests gkcore_model_fbx_animation_fixtures)
            add_test(NAME gkcore.model_animation_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_animation_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_animation_capture_tests>
                    --fixture-dir "${CMAKE_CURRENT_BINARY_DIR}/model-animation-capture-fixtures"
                    --fbx-fixture-dir "${CMAKE_CURRENT_BINARY_DIR}/model-fbx-animation-fixtures"
                    --output-dir "${CMAKE_CURRENT_BINARY_DIR}/model-animation-captures/$<CONFIG>")
            set_tests_properties(gkcore.model_animation_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 240 RUN_SERIAL TRUE)
            add_executable(gkcore_model_gpu_skinning_error_tests tests/model_gpu_skinning_error_tests.cpp)
            target_compile_features(gkcore_model_gpu_skinning_error_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_gpu_skinning_error_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_gpu_skinning_error_tests)
            add_dependencies(gkcore_model_gpu_skinning_error_tests gkcore_model_fbx_animation_fixtures)
            add_test(NAME gkcore.model_gpu_skinning_error
                COMMAND gkcore_model_gpu_skinning_error_tests "${CMAKE_CURRENT_BINARY_DIR}/model-fbx-animation-fixtures/fbx-animation-gpu-degenerate-triangle.fbx")
            set_tests_properties(gkcore.model_gpu_skinning_error PROPERTIES LABELS "windows;gpu" TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "GKCORE_VERIFY_GPU_SKINNING=0")
            add_test(NAME gkcore.model_gpu_skinning_overflow
                COMMAND gkcore_model_gpu_skinning_error_tests "${CMAKE_CURRENT_BINARY_DIR}/model-fbx-animation-fixtures/fbx-animation-gpu-overflow-triangle.fbx")
            set_tests_properties(gkcore.model_gpu_skinning_overflow PROPERTIES LABELS "windows;gpu" TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "GKCORE_VERIFY_GPU_SKINNING=0")
            add_test(NAME gkcore.model_gpu_skinning_large_finite
                COMMAND gkcore_model_gpu_skinning_error_tests "${CMAKE_CURRENT_BINARY_DIR}/model-fbx-animation-fixtures/fbx-animation-gpu-large-finite-triangle.fbx" --expect-success)
            set_tests_properties(gkcore.model_gpu_skinning_large_finite PROPERTIES LABELS "windows;gpu" TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "GKCORE_VERIFY_GPU_SKINNING=0")
            add_executable(gkcore_model_gpu_skinning_multi_tests tests/model_gpu_skinning_multi_tests.cpp)
            target_compile_features(gkcore_model_gpu_skinning_multi_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_gpu_skinning_multi_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_gpu_skinning_multi_tests)
            add_dependencies(gkcore_model_gpu_skinning_multi_tests gkcore_model_fbx_animation_fixtures)
            add_test(NAME gkcore.model_gpu_skinning_multi
                COMMAND gkcore_model_gpu_skinning_multi_tests "${CMAKE_CURRENT_BINARY_DIR}/model-fbx-animation-fixtures")
            set_tests_properties(gkcore.model_gpu_skinning_multi PROPERTIES LABELS "windows;gpu;fp64" TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "GKCORE_VERIFY_GPU_SKINNING=1")
            add_executable(gkcore_model_blend_capture_tests tests/model_blend_capture_tests.cpp)
            add_executable(gkcore_real_model_capture_tests tests/real_model_capture_tests.cpp examples/support/FHumanoidMapOptions.cpp)
            target_compile_features(gkcore_real_model_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_real_model_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_real_model_capture_tests)
            target_compile_features(gkcore_model_blend_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_blend_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_blend_capture_tests)
            add_test(NAME gkcore.model_blend_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_blend_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_blend_capture_tests>
                    --fixture-dir "${CMAKE_CURRENT_BINARY_DIR}/model-blend-fixtures"
                    --output-dir "${CMAKE_CURRENT_BINARY_DIR}/model-blend-captures/$<CONFIG>")
            set_tests_properties(gkcore.model_blend_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 240 RUN_SERIAL TRUE)
            add_executable(gkcore_model_material_capture_tests tests/model_material_capture_tests.cpp)
            target_compile_features(gkcore_model_material_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_material_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_material_capture_tests)
            add_dependencies(gkcore_model_material_capture_tests gkcore_model_material_fixtures)
            add_executable(gkcore_model_normal_capture_tests tests/model_normal_capture_tests.cpp)
            target_compile_features(gkcore_model_normal_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_normal_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_normal_capture_tests)
            add_dependencies(gkcore_model_normal_capture_tests gkcore_model_normal_fixtures)
            add_executable(gkcore_model_sampler_capture_tests tests/model_sampler_capture_tests.cpp)
            target_compile_features(gkcore_model_sampler_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_sampler_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_sampler_capture_tests)
            add_dependencies(gkcore_model_sampler_capture_tests gkcore_model_sampler_fixtures)
            add_executable(gkcore_model_mip_capture_tests tests/model_mip_capture_tests.cpp)
            target_compile_features(gkcore_model_mip_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_mip_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_mip_capture_tests)
            add_dependencies(gkcore_model_mip_capture_tests gkcore_model_mip_fixtures)
            add_executable(gkcore_model_emissive_capture_tests tests/model_emissive_capture_tests.cpp)
            target_compile_features(gkcore_model_emissive_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_emissive_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_emissive_capture_tests)
            add_dependencies(gkcore_model_emissive_capture_tests gkcore_model_emissive_fixtures)
            add_executable(gkcore_model_occlusion_capture_tests tests/model_occlusion_capture_tests.cpp)
            target_compile_features(gkcore_model_occlusion_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_occlusion_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_occlusion_capture_tests)
            add_dependencies(gkcore_model_occlusion_capture_tests gkcore_model_occlusion_fixtures)
            add_executable(gkcore_model_tangent_capture_tests tests/model_tangent_capture_tests.cpp)
            target_compile_features(gkcore_model_tangent_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_tangent_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_tangent_capture_tests)
            add_dependencies(gkcore_model_tangent_capture_tests gkcore_model_tangent_fixtures)
            add_executable(gkcore_model_generated_normal_capture_tests tests/model_generated_normal_capture_tests.cpp)
            target_compile_features(gkcore_model_generated_normal_capture_tests PRIVATE cxx_std_17)
            target_link_libraries(gkcore_model_generated_normal_capture_tests PRIVATE gkcore_capture_runtime)
            gkcore_enable_warnings(gkcore_model_generated_normal_capture_tests)
            add_dependencies(gkcore_model_generated_normal_capture_tests gkcore_model_generated_normal_fixtures)
            # 表示を伴う検査は、非表示の自動captureと分けて明示的に選ぶ。
            if(GKCORE_RUN_INTERACTIVE_TESTS)
                add_test(NAME gkcore.backend_smoke COMMAND gkcore_backend_smoke)
                set_tests_properties(gkcore.backend_smoke PROPERTIES LABELS "windows;gpu;interactive" TIMEOUT 60 RUN_SERIAL TRUE)
            endif()
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
            add_test(NAME gkcore.glb_texture_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/glb_texture_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_glb_texture_capture_tests>
                    --fixture-dir "${_gkcore_glb_texture_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/glb-texture-captures/$<CONFIG>)
            set_tests_properties(gkcore.glb_texture_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 180 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_alpha_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_alpha_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_alpha_capture_tests>
                    --fixture-dir "${_gkcore_model_alpha_fixture_dir}"
                    --shader ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/shaders/gkcore_user_tint.frag
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-alpha-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_alpha_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 180 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_material_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_material_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_material_capture_tests>
                    --fixture-dir "${_gkcore_model_material_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-material-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_material_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 240 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_normal_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_normal_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_normal_capture_tests>
                    --fixture-dir "${_gkcore_model_normal_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-normal-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_normal_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 240 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_sampler_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_sampler_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_sampler_capture_tests>
                    --fixture-dir "${_gkcore_model_sampler_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-sampler-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_sampler_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 300 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_mip_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_mip_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_mip_capture_tests>
                    --fixture-dir "${_gkcore_model_mip_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-mip-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_mip_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 300 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_emissive_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_emissive_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_emissive_capture_tests>
                    --fixture-dir "${_gkcore_model_emissive_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-emissive-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_emissive_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 300 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_occlusion_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_occlusion_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_occlusion_capture_tests>
                    --fixture-dir "${_gkcore_model_occlusion_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-occlusion-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_occlusion_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 300 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_tangent_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_tangent_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_tangent_capture_tests>
                    --fixture-dir "${_gkcore_model_tangent_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-tangent-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_tangent_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 300 RUN_SERIAL TRUE)
            add_test(NAME gkcore.model_generated_normal_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_generated_normal_capture_tests.py
                    --executable $<TARGET_FILE:gkcore_model_generated_normal_capture_tests>
                    --fixture-dir "${_gkcore_model_generated_normal_fixture_dir}"
                    --output-dir ${CMAKE_CURRENT_BINARY_DIR}/model-generated-normal-captures/$<CONFIG>)
            set_tests_properties(gkcore.model_generated_normal_capture PROPERTIES LABELS "windows;gpu" TIMEOUT 300 RUN_SERIAL TRUE)
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

    if(TARGET gkcore_glb_texture_capture_tests)
        add_dependencies(gkcore_glb_texture_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_model_alpha_capture_tests)
        add_dependencies(gkcore_model_alpha_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_model_material_capture_tests)
        add_dependencies(gkcore_model_material_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_normal_capture_tests)
        add_dependencies(gkcore_model_normal_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_sampler_capture_tests)
        add_dependencies(gkcore_model_sampler_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_mip_capture_tests)
        add_dependencies(gkcore_model_mip_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_model_emissive_capture_tests)
        add_dependencies(gkcore_model_emissive_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_model_occlusion_capture_tests)
        add_dependencies(gkcore_model_occlusion_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_model_tangent_capture_tests)
        add_dependencies(gkcore_model_tangent_capture_tests gkcore_stage_runtime_payload)
    endif()

    if(TARGET gkcore_model_generated_normal_capture_tests)
        add_dependencies(gkcore_model_generated_normal_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_animation_capture_tests)
        add_dependencies(gkcore_model_animation_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_blend_capture_tests)
        add_dependencies(gkcore_model_blend_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_gpu_skinning_error_tests)
        add_dependencies(gkcore_model_gpu_skinning_error_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_gpu_skinning_multi_tests)
        add_dependencies(gkcore_model_gpu_skinning_multi_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_real_model_capture_tests)
        add_dependencies(gkcore_real_model_capture_tests gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_viewer)
        add_dependencies(gkcore_model_viewer gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_benchmark)
        add_dependencies(gkcore_model_benchmark gkcore_stage_runtime_payload)
    endif()
    if(TARGET gkcore_model_benchmark_visible)
        add_dependencies(gkcore_model_benchmark_visible gkcore_stage_runtime_payload)
    endif()

    install(TARGETS gkcore EXPORT gkcoreTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES include/gkcore.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Runtime)
    install(FILES include/gkcore/Handle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/ModelAnimation.h include/gkcore/EHumanoidBone.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
    install(FILES include/gkcore/ModelMaterial.h include/gkcore/FModelMaterialSettings.h include/gkcore/EModelAlphaMode.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gkcore COMPONENT Runtime)
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

    if(GKCORE_BUILD_TESTS)
        set(_gkcore_package_gpu_args)
        if(GKCORE_RUN_BACKEND_SMOKE AND GKCORE_RUN_INTERACTIVE_TESTS)
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
