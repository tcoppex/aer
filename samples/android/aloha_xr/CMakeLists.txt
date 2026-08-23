# -----------------------------------------------------------------------------

set(SAMPLE_ROOT_PATH        ${CMAKE_CURRENT_SOURCE_DIR})
set(SAMPLE_SOURCE_DIR       ${SAMPLE_ROOT_PATH}/src/cpp)

get_filename_component(target ${SAMPLE_ROOT_PATH} NAME)

# -----------------------------------------------------------------------------

file(GLOB_RECURSE Sources
  ${SAMPLE_SOURCE_DIR}/*.cc
  ${SAMPLE_SOURCE_DIR}/*.h
)

add_library(${target} MODULE ${Sources})

# -----------------------------------------------------------------------------

# Here the sample's shaders will be compiled by gradle but we NEED to copy the
# framework shaders to the assets for the package to hold them.
# For now it is handle by gradle in a semi reliable way but we might
# have need to rethink it ulteriorly.

# compile_shaders(
#   ${SAMPLE_PATH}/shaders/glsl
#   ${SAMPLE_PATH}/shaders/spirv
#   shaders_bin
#   shaders_src
#   ${FRAMEWORK_SHADERS_DIR}
# )

if(TRUE)
  add_dependencies(${target} BuildFrameworkShaders)
else()
  # # Find the framework AAR from gradle config.
  # find_package(vkframework REQUIRED CONFIG)
  # target_link_libraries(${target} PRIVATE vkframework::vkframework)
endif()

# -----------------------------------------------------------------------------

helpers_setupTarget(
  TARGET
    ${target}

  INCLUDE_DIRECTORIES
    ${SAMPLE_SOURCE_DIR}

  LIBRARIES
    ${FRAMEWORK_LIBRARIES}
    # android_native_app_glue

  DEFINITIONS
    SAMPLE_SPIRV_DIR="shaders/"
    AER_USE_OPENXR=1
)

# -----------------------------------------------------------------------------
