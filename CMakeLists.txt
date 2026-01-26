cmake_minimum_required(VERSION 3.16)
project(RaidBuilder LANGUAGES CXX)

add_executable(RaidBuilder
    src/main.cpp
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
)
