# -----------------------------------------------------------------------------
#
# Desktop Samples
#
# -----------------------------------------------------------------------------

## Add  Sample target and setup its properties.
function(add_sample dirname)
  set(SAMPLE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${dirname})
  set(target ${dirname})

  file(GLOB Source
    ${SAMPLE_PATH}/*.cc
    ${SAMPLE_PATH}/*.h
  )

  # (work for both, but currently only used for desktop target)
  if(ANDROID)
    add_library(${target} MODULE ${Source})
  else()
    add_executable(${target} ${Source})
  endif()

  set_target_output_directory(${target} ${PROJECT_BINARY_DIR})


# GStreamer
set(GSTREAMER_LOCAL_PATH ~/Bin/gstreamer/usr/local)
set(ENV{PKG_CONFIG_PATH} "${GSTREAMER_LOCAL_PATH}/lib/x86_64-linux-gnu/pkgconfig:$ENV{PKG_CONFIG_PATH}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-vulkan-1.0)

include_directories(${GSTREAMER_LOCAL_PATH}/include/gstreamer-1.0)

message(STATUS "GST_LIBRARIES: ${GST_LIBRARIES}")
message(STATUS "GST_LIBRARY_DIRS: ${GST_LIBRARY_DIRS}")


  helpers_setupTarget(
    TARGET
      ${target}
    INCLUDE_DIRECTORIES
      ${SAMPLE_PATH}
      ${GST_INCLUDE_DIRS}
    LIBRARIES
      ${FRAMEWORK_LIBRARIES}
      ${GST_LIBRARIES}
    DEFINITIONS
      COMPILED_SHADERS_DIR="${SAMPLE_PATH}/shaders/spirv/"
  )

  target_link_directories(${target} PRIVATE "${GST_LIBRARY_DIRS}")

  # Compile shaders dependencies.
  compile_shaders(
    ${SAMPLE_PATH}/shaders/glsl
    ${SAMPLE_PATH}/shaders/spirv
    shaders_bin
    shaders_src
    ${FRAMEWORK_SHADERS_DIR}
  )
  set(BuildShaderTarget BuildSampleShaders_${target})
  add_custom_target(${BuildShaderTarget} ALL
    DEPENDS
      ${shaders_bin}
      ${shaders_src}
  )
  add_dependencies(${target} ${BuildShaderTarget})
endfunction(add_sample)

# -----------------------------------------------------------------------------

# add_sample(00_hello)
# add_sample(01_triangle)
# add_sample(02_push_constant)
# add_sample(03_descriptor_set)
# add_sample(04_texturing)
# add_sample(05_stencil_op)
# add_sample(06_blend_op)
# add_sample(07_compute)
# add_sample(08_hdr_envmap)
# add_sample(09_post_process)
# add_sample(10_material)
# add_sample(11_raytracing)
# add_sample(12_font)

add_sample(15_video)

# -----------------------------------------------------------------------------