cmake_minimum_required(VERSION 3.13)

project(ounce_probe CXX C)

set(AKIRA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(PROBE_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${PROBE_SHADER_DIR})

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(PROBE_SHADER_HEADERS)
foreach(shader fullscreen.vert nv12.frag)
    string(REPLACE "." "_" variable ${shader})
    set(header ${PROBE_SHADER_DIR}/${variable}.h)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.3 --vn ${variable} -o ${header} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
        VERBATIM)
    list(APPEND PROBE_SHADER_HEADERS ${header})
endforeach()

add_executable(ounce_probe
    source/main.cpp
    source/h264_clip.cpp
    source/vulkan_presenter.cpp
    ${AKIRA_ROOT}/source/stream/hardware_decoder.cpp
    ${PROBE_SHADER_HEADERS})
set_target_properties(ounce_probe PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(ounce_probe PRIVATE source ${AKIRA_ROOT}/include ${PROBE_SHADER_DIR})
target_compile_options(ounce_probe PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_compile_definitions(ounce_probe PRIVATE VK_USE_PLATFORM_VI_NN)
target_link_libraries(ounce_probe PRIVATE vulkan nx)

nx_generate_nacp(OUTPUT ounce_probe.nacp NAME "ounce probe" AUTHOR "akira" VERSION "0.1.0")
nx_create_nro(ounce_probe NACP ounce_probe.nacp ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs)
