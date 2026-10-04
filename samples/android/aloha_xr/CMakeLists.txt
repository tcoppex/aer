# -----------------------------------------------------------------------------
# Android Sample
# -----------------------------------------------------------------------------

set(SAMPLE_ROOT_PATH        ${CMAKE_CURRENT_SOURCE_DIR})
set(SAMPLE_SOURCE_DIR       ${SAMPLE_ROOT_PATH}/src/cpp)

get_filename_component(target ${SAMPLE_ROOT_PATH} NAME)

file(GLOB_RECURSE Sources
  ${SAMPLE_SOURCE_DIR}/*.cc
  ${SAMPLE_SOURCE_DIR}/*.h
)

add_library(${target} MODULE ${Sources})

# -----------------------------------------------------------------------------
# Shaders Compilation (Slang -> Android Assets)
# -----------------------------------------------------------------------------

set(ANDROID_ASSETS_SHADERS_DIR "${SAMPLE_ROOT_PATH}/src/main/assets/shaders")

compile_sample_shaders(
  ${SAMPLE_ROOT_PATH}/src/shaders
  ${ANDROID_ASSETS_SHADERS_DIR}
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
    ${SAMPLE_SOURCE_DIR}

  LIBRARIES
    ${FRAMEWORK_LIBRARIES}
    # android_native_app_glue

  DEFINITIONS
    SAMPLE_SPIRV_DIR="shaders/"
    AER_USE_OPENXR=1
)

# -----------------------------------------------------------------------------
