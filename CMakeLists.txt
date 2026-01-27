cmake_minimum_required(VERSION 3.16)
project(RaidBuilder LANGUAGES CXX)

add_executable(RaidBuilder
    src/main.cpp
    src/block_sml.cpp
    src/gltf_loader.cpp
    src/rotation_controls.cpp
)

if(APPLE)
    target_sources(RaidBuilder PRIVATE src/mac_menu.mm)
    target_link_libraries(RaidBuilder PRIVATE "-framework Cocoa")
endif()

target_link_libraries(RaidBuilder
    PRIVATE VoxelEngine SMLParser SMLUI
)

target_compile_features(RaidBuilder PUBLIC cxx_std_11)

target_include_directories(RaidBuilder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinygltf/tinygltf-2.9.7
)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_SOURCES
    "${SHADER_DIR}/world.vert"
    "${SHADER_DIR}/world.frag"
    "${SHADER_DIR}/pick.vert"
    "${SHADER_DIR}/pick.frag"
)
set(SHADER_OUTPUTS "")
foreach(shader ${SHADER_SOURCES})
    list(APPEND SHADER_OUTPUTS "${shader}.spv")
endforeach()

add_custom_command(
    OUTPUT ${SHADER_OUTPUTS}
    COMMAND glslc -o "${SHADER_DIR}/world.vert.spv" "${SHADER_DIR}/world.vert"
    COMMAND glslc -o "${SHADER_DIR}/world.frag.spv" "${SHADER_DIR}/world.frag"
    COMMAND glslc -o "${SHADER_DIR}/pick.vert.spv" "${SHADER_DIR}/pick.vert"
    COMMAND glslc -o "${SHADER_DIR}/pick.frag.spv" "${SHADER_DIR}/pick.frag"
    DEPENDS ${SHADER_SOURCES}
    VERBATIM
)

add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(RaidBuilder Shaders)

add_executable(BlockBake
    tools/block_bake.cpp
    src/block_sml.cpp
)

target_include_directories(BlockBake PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../SMLParser/include
)

target_link_libraries(BlockBake PRIVATE SMLParser)
target_compile_features(BlockBake PUBLIC cxx_std_11)

file(GLOB BLOCK_SMLS "${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks/*.sml")
set(BLOCK_OUT_DIR "${CMAKE_BINARY_DIR}/blocks_cache")
set(BLOCK_GLBS "")
foreach(sml ${BLOCK_SMLS})
    get_filename_component(name "${sml}" NAME_WE)
    list(APPEND BLOCK_GLBS "${BLOCK_OUT_DIR}/${name}.glb")
endforeach()

add_custom_command(
    OUTPUT ${BLOCK_GLBS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BLOCK_OUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E echo "Baking blocks to ${BLOCK_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "BlockBake: ${BLOCK_SMLS}"
    COMMAND ${CMAKE_COMMAND} -E env
            "$<TARGET_FILE:BlockBake>" ${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks/block.sml ${BLOCK_OUT_DIR}/block.glb
    COMMAND ${CMAKE_COMMAND} -E env
            "$<TARGET_FILE:BlockBake>" ${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks/stairs.sml ${BLOCK_OUT_DIR}/stairs.glb
    COMMAND ${CMAKE_COMMAND} -E env
            "$<TARGET_FILE:BlockBake>" ${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks/stairs_concave.sml ${BLOCK_OUT_DIR}/stairs_concave.glb
    COMMAND ${CMAKE_COMMAND} -E env
            "$<TARGET_FILE:BlockBake>" ${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks/stairs_convex.sml ${BLOCK_OUT_DIR}/stairs_convex.glb
    COMMAND ${CMAKE_COMMAND} -E env
            "$<TARGET_FILE:BlockBake>" ${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks/vines.sml ${BLOCK_OUT_DIR}/vines.glb
    DEPENDS BlockBake ${BLOCK_SMLS}
    VERBATIM
)

add_custom_target(BakeBlocks ALL DEPENDS ${BLOCK_GLBS})
add_dependencies(RaidBuilder BakeBlocks)
