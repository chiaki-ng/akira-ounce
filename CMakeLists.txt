cmake_minimum_required(VERSION 3.13)

# Force Switch platform
set(PLATFORM_SWITCH ON CACHE BOOL "Build for Nintendo Switch" FORCE)
set(PLATFORM_SWITCH2 ON CACHE BOOL "Build for Switch 2 with libounce" FORCE)

# Borealis paths
set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis CACHE PATH "Borealis source tree")
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

set(USE_VULKAN ON CACHE BOOL "Use Vulkan for rendering" FORCE)

# Platform common options + toolchain from borealis
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(akira)

add_compile_options(-fno-omit-frame-pointer)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Project metadata + git-derived version
include(version)

# Platform options
list(APPEND APP_PLATFORM_OPTION
    -DBUILD_PACKAGE_NAME=${PACKAGE_NAME}
)

# Threads
find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

# Resources are embedded via --romfsdir in elf2nro
set(BRLS_RESOURCES_DIR "romfs:/")

include(dependencies)
include(core)
include(core_includes)
find_package(CURL REQUIRED)

# Vendored C libraries (wireguard target + lwIP sources)
include(vendored_libs)

###########################################
# Source files
###########################################
file(GLOB_RECURSE MAIN_SRC CONFIGURE_DEPENDS source/*.cpp)

# Switch wrapper for libnx initialization
list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)

# Sub-libraries
add_subdirectory(${BOREALIS_DIR}/library borealis)
add_subdirectory(library/tomlplusplus EXCLUDE_FROM_ALL)

###########################################
# Main target
###########################################
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)
include(vulkan_shaders)

# Borealis uses DEBUG only to attach nxlinkStdio() in its Switch wrapper.
# Keep that capture path in optimized builds without compiling the whole app
# (or its libraries) with debug-only behavior. Runtime log levels and file
# logging are configured independently in source/main.cpp.
set_source_files_properties(
    ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c
    PROPERTIES COMPILE_DEFINITIONS DEBUG
)

target_sources(${PROJECT_NAME} PRIVATE ${LWIP_SRCS} ${LWIP_RELAY_SRCS})
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/library/qrcodegen/qrcodegen.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/library/qrcodegen
    # Generated version header (configure_file in cmake/version.cmake)
    ${CMAKE_BINARY_DIR}/generated
    # Borealis includes
    ${BOREALIS_LIBRARY}/include
    # WireGuard
    ${WIREGUARD_DIR}/include
    ${WIREGUARD_DIR}/src
    ${WIREGUARD_DIR}/lwip-relay/include
    # lwIP includes
    ${LWIP_DIR}/src/include
    ${APP_PLATFORM_INCLUDE}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -ffunction-sections
    -fdata-sections
    -Wunused-variable
    ${APP_PLATFORM_OPTION}
)

option(MUTE_CHIAKI_LOGS "Drop every core log line" OFF)
target_compile_definitions(${PROJECT_NAME} PRIVATE AKIRA_MUTE_CORE_LOGS=$<BOOL:${MUTE_CHIAKI_LOGS}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE AKIRA_PS5_CLOUD_LAUNCH=$<BOOL:${AKIRA_PS5_CLOUD_LAUNCH}>)


target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    libchiaking_chiaki_ng
    libchiaking_curl
    libchiaking_crypto_libnx
    libchiaking_media_opus
    libchiaking_simdutf
    CURL::libcurl
    tomlplusplus::tomlplusplus
    PkgConfig::SDL2
    PkgConfig::FFMPEG
    PkgConfig::JSONC
    wireguard
    ${APP_PLATFORM_LIB}
)

target_link_options(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_LINK_OPTION})
if (PLATFORM_SWITCH)
    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,--wrap=svcSetThreadCoreMask
        -Wl,--wrap=abort
        -Wl,--wrap=__assert_func
        -Wl,--wrap=fatalThrow
        -Wl,--wrap=fatalThrowWithPolicy
        -Wl,--wrap=getaddrinfo
        -Wl,--wrap=freeaddrinfo)
endif()

# NRO packaging
include(packaging)
