cmake_minimum_required(VERSION 3.20)
project(mongo_gpu_poc CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(VKSDK "$ENV{HOME}/VulkanSDK/1.3.283.0/macOS" CACHE PATH "Vulkan SDK root")
# volk (header + volk.c): from the SDK by default; CI passes -DVOLK_DIR=<checkout of zeux/volk>.
set(VOLK_DIR "${VKSDK}/include/volk" CACHE PATH "Directory holding volk.h and volk.c")
if(EXISTS ${VKSDK}/include)
  include_directories(${VKSDK}/include)
endif()
include_directories(${VOLK_DIR} ${CMAKE_SOURCE_DIR})
add_library(volk STATIC ${VOLK_DIR}/volk.c)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

find_program(GLSLC glslc HINTS ${VKSDK}/bin REQUIRED)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
file(GLOB SHADERS ${CMAKE_SOURCE_DIR}/shaders/*.comp)
# df64 is built in three variants for the fast-math experiment (see RESULTS.md).
list(REMOVE_ITEM SHADERS ${CMAKE_SOURCE_DIR}/shaders/df64.comp)
set(DFV ${CMAKE_BINARY_DIR}/shaders)
add_custom_command(OUTPUT ${DFV}/df64_precise.spv
  COMMAND ${GLSLC} --target-env=vulkan1.2 -O0 -o ${DFV}/df64_precise.spv ${CMAKE_SOURCE_DIR}/shaders/df64.comp
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/df64.comp ${CMAKE_SOURCE_DIR}/shaders/df64_ops.glsl VERBATIM)
add_custom_command(OUTPUT ${DFV}/df64_plain.spv
  COMMAND ${GLSLC} --target-env=vulkan1.2 -O0 -DNOPRECISE -o ${DFV}/df64_plain.spv ${CMAKE_SOURCE_DIR}/shaders/df64.comp
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/df64.comp ${CMAKE_SOURCE_DIR}/shaders/df64_ops.glsl VERBATIM)
add_custom_command(OUTPUT ${DFV}/df64_precise_szinp.spv
  COMMAND ${CMAKE_SOURCE_DIR}/tools/add_szinp.sh ${DFV}/df64_precise.spv ${DFV}/df64_precise_szinp.spv
  DEPENDS ${DFV}/df64_precise.spv ${CMAKE_SOURCE_DIR}/tools/add_szinp.sh VERBATIM)
add_custom_command(OUTPUT ${DFV}/df64_nofma.spv
  COMMAND ${GLSLC} --target-env=vulkan1.2 -O0 -DNOFMA -o ${DFV}/df64_nofma.spv ${CMAKE_SOURCE_DIR}/shaders/df64.comp
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/df64.comp ${CMAKE_SOURCE_DIR}/shaders/df64_ops.glsl VERBATIM)
set(SPV ${DFV}/df64_precise.spv ${DFV}/df64_plain.spv ${DFV}/df64_precise_szinp.spv ${DFV}/df64_nofma.spv)
foreach(S ${SHADERS})
  get_filename_component(N ${S} NAME_WE)
  set(O ${CMAKE_BINARY_DIR}/shaders/${N}.spv)
  add_custom_command(OUTPUT ${O}
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O0 -o ${O} ${S}
    DEPENDS ${S} VERBATIM)
  list(APPEND SPV ${O})
  if(N STREQUAL "selftest")  # also without fma (POC 8)
    set(OF ${CMAKE_BINARY_DIR}/shaders/selftest_nofma.spv)
    add_custom_command(OUTPUT ${OF}
      COMMAND ${GLSLC} --target-env=vulkan1.2 -O0 -DNOFMA -o ${OF} ${S}
      DEPENDS ${S} ${CMAKE_SOURCE_DIR}/shaders/split_double.glsl ${CMAKE_SOURCE_DIR}/shaders/df64_ops.glsl VERBATIM)
    list(APPEND SPV ${OF})
  endif()
  if(N MATCHES "^sum_")  # also build without `precise`, to compare against fast math off (POC 2)
    set(OP ${CMAKE_BINARY_DIR}/shaders/${N}_plain.spv)
    add_custom_command(OUTPUT ${OP}
      COMMAND ${GLSLC} --target-env=vulkan1.2 -O0 -DNOPRECISE -o ${OP} ${S}
      DEPENDS ${S} ${CMAKE_SOURCE_DIR}/shaders/sum_common.glsl ${CMAKE_SOURCE_DIR}/shaders/df64_ops.glsl VERBATIM)
    list(APPEND SPV ${OP})
  endif()
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV})

file(GLOB POCS ${CMAKE_SOURCE_DIR}/poc*.cpp)
foreach(P ${POCS})
  get_filename_component(N ${P} NAME_WE)
  add_executable(${N} ${P})
  target_link_libraries(${N} volk)
  add_dependencies(${N} shaders)
endforeach()
