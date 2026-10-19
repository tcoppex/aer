# -----------------------------------------------------------------------------
# Android Sample
# -----------------------------------------------------------------------------

set(SAMPLE_ROOT_PATH        ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SAMPLE_MAIN_DIR         ${SAMPLE_ROOT_PATH}/main)
set(SAMPLE_CPP_DIR          ${SAMPLE_MAIN_DIR}/cpp)
set(SAMPLE_ASSETS_DIR       ${SAMPLE_MAIN_DIR}/assets)

set(SAMPLE_SHADERS_DIR      ${SAMPLE_ROOT_PATH}/shaders)
set(SAMPLE_SPIRV_DIR        ${SAMPLE_ASSETS_DIR}/shaders)

# -----------------------------------------------------------------------------
# Sample Target
# -----------------------------------------------------------------------------

get_filename_component(target ${CMAKE_CURRENT_SOURCE_DIR} NAME)

file(GLOB_RECURSE Sources
  ${SAMPLE_CPP_DIR}/*.cc
  ${SAMPLE_CPP_DIR}/*.h
)

add_library(${target} MODULE ${Sources})

# -----------------------------------------------------------------------------
# Shaders Compilation (Slang -> Android Assets)
# -----------------------------------------------------------------------------

compile_sample_shaders(
  ${SAMPLE_SHADERS_DIR}
  ${SAMPLE_SPIRV_DIR}
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

if(TRUE)
  add_dependencies(${target}
    BuildFrameworkShaders
    ${BuildShaderTarget}
  )
else()
  # # Find the framework AAR from gradle config.
  # find_package(vkframework REQUIRED CONFIG)
  # target_link_libraries(${target} PRIVATE vkframework::vkframework)
endif()

# -----------------------------------------------------------------------------
# Setup target
# -----------------------------------------------------------------------------

helpers_setupTarget(
  TARGET
    ${target}

  INCLUDE_DIRECTORIES
    ${SAMPLE_CPP_DIR}
    ${SAMPLE_MAIN_DIR} # to include interop shaders ..

  LIBRARIES
    aer::framework

  DEFINITIONS
    SAMPLE_SPIRV_DIR="shaders/"
    AER_USE_OPENXR=1
)

# -----------------------------------------------------------------------------
