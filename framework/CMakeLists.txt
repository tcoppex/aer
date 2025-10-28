# -----------------------------------------------------------------------------
# 
# Framework
# 
# -----------------------------------------------------------------------------

set(target aer)

if(ANDROID)
  # We add a suffix to  avoid collision with the Desktop build.
  # This require to be also set in the framework build.gradle.
  set(target ${target}_android)
endif()

# -----------------------------------------------------------------------------
# Paths.
# -----------------------------------------------------------------------------

# Currently the framework spirv directory is also used by the Android gradle build
# to migrate them to samples APK assets directory, so we need to specify a distinct
# subdir for them not to be in conflict with others assets (shaders or otherwhise).
set(FRAMEWORK_SPIRV_ASSETS_SUBDIR   framework_shaders)
set(FRAMEWORK_COMPILED_SHADERS_DIR
  ${FRAMEWORK_COMPILED_SHADERS_DIR}/${FRAMEWORK_SPIRV_ASSETS_SUBDIR}
)

# -----------------------------------------------------------------------------
# Source dependencies.
# -----------------------------------------------------------------------------

file(GLOB_RECURSE Sources CONFIGURE_DEPENDS
  LIST_DIRECTORIES FALSE
  ${FRAMEWORK_SOURCE_DIR}/*.cc
  ${FRAMEWORK_SOURCE_DIR}/*.h
)

# -----------------------------------------------------------------------------
# Platform specific Source dependencies.
# -----------------------------------------------------------------------------

set(FRAMEWORK_PLATFORM_SRC_DIR ${FRAMEWORK_SOURCE_DIR}/aer/platform/impl)

# Exclude all platform specific codes..
list(FILTER Sources EXCLUDE REGEX "${FRAMEWORK_PLATFORM_SRC_DIR}/.*")

# Add back target platform specifics sources.
if(ANDROID)
  set(FRAMEWORK_PLATFORM_SRC_SUB_DIR ${FRAMEWORK_PLATFORM_SRC_DIR}/android)
else()
  set(FRAMEWORK_PLATFORM_SRC_SUB_DIR ${FRAMEWORK_PLATFORM_SRC_DIR}/desktop)
endif()

file(GLOB_RECURSE PlatformSources CONFIGURE_DEPENDS
  LIST_DIRECTORIES FALSE
  ${FRAMEWORK_PLATFORM_SRC_SUB_DIR}/*.cc
  ${FRAMEWORK_PLATFORM_SRC_SUB_DIR}/*.h
)

list(APPEND Sources ${PlatformSources})

# -----------------------------------------------------------------------------
# Target library compilation.
# -----------------------------------------------------------------------------

add_library(${target} STATIC ${Sources})
set(FRAMEWORK_LIBRARIES ${target} CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# Platform specific Third Parties Dependencies.
# -----------------------------------------------------------------------------

if(ANDROID)
  # [SHARED] Android specifics.
  find_library(ANDROID_LIBRARY     NAMES android)
  find_library(ANDROID_LOG_LIBRARY NAMES log)

  list(APPEND PlatformLibs
    ${ANDROID_LIBRARY}
    ${ANDROID_LOG_LIBRARY}
  )

  # {MACRO} Properly add the Native App Glue dependencies to its consuming target.
  function(helpers_addNativeAppGlue target)
    # [OBJECT] Android Native App Glue.
    find_path(ANDROID_NATIVE_APP_GLUE_PATH android_native_app_glue.h
      PATHS "${ANDROID_NDK}/sources/android/native_app_glue"
    )
    add_library(android_native_app_glue
      OBJECT
        "${ANDROID_NATIVE_APP_GLUE_PATH}/android_native_app_glue.c"
    )
    target_include_directories(android_native_app_glue
      PUBLIC
        "${ANDROID_NATIVE_APP_GLUE_PATH}"
    )
    target_compile_options(android_native_app_glue
      PRIVATE
        -Wno-unused-parameter
    )
    target_include_directories(${target} PUBLIC ${ANDROID_NATIVE_APP_GLUE_PATH})
    target_link_libraries(${target} PUBLIC $<TARGET_OBJECTS:android_native_app_glue>)
  endfunction()

  # (we might prefer to link the native app to the sample in the future)
  helpers_addNativeAppGlue(${target})

  # (Alternatively just uncomment the following line.)
  # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
else() # DESKTOP
  # GLFW
  CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.4
    OPTIONS
      "GLFW_BUILD_DOCS OFF"
      "GLFW_BUILD_EXAMPLES OFF"
      "GLFW_BUILD_TESTS OFF"
      "GLFW_BUILD_WAYLAND OFF"
      "GLFW_INSTALL OFF"
  )
  list(APPEND PlatformLibs glfw)
  list(APPEND PlatformIncludeDirs ${glfw_SOURCE_DIR}/include)
endif()

# -----------------------------------------------------------------------------
# OpenXR SDK Loader
# -----------------------------------------------------------------------------

if(TRUE)
  CPMAddPackage(
    NAME sanitizers
    GITHUB_REPOSITORY arsenm/sanitizers-cmake
    GIT_TAG master
    DOWNLOAD_ONLY YES
  )
  list(APPEND CMAKE_MODULE_PATH ${sanitizers_SOURCE_DIR}/cmake)

  CPMAddPackage(
    NAME OPENXR_SDK
    GITHUB_REPOSITORY KhronosGroup/OpenXR-SDK
    GIT_TAG release-1.1.52
    # DOWNLOAD_ONLY YES
  )
  set(OPENXR_INCLUDE_DIRS ${OPENXR_SDK_SOURCE_DIR}/include)
  set(OPENXR_LOADER_SHARED_LIBRARY OpenXR::openxr_loader)

  # ----

  target_include_directories(${target} PUBLIC ${OPENXR_INCLUDE_DIRS})
  target_link_libraries(${target} PRIVATE ${OPENXR_LOADER_SHARED_LIBRARY})
  target_compile_definitions(${target}
    PUBLIC
      USE_OPENXR=1 #!
      XR_USE_GRAPHICS_API_VULKAN=1
  )
  if(OS_ANDROID)
    target_compile_definitions(${target}
      PUBLIC
        XR_USE_PLATFORM_ANDROID=1
    )
  endif()

  # # Add OpenXR specific sources.
  # file(GLOB XRSources LIST_DIRECTORIES FALSE
  #   ${FRAMEWORK_PLATFORM_SRC_DIR}/openxr/*.cc   ## !
  # )
  # target_sources(${target} PRIVATE ${XRSources})
endif()

# -----------------------------------------------------------------------------
# Third Parties Dependencies.
# -----------------------------------------------------------------------------

# Vulkan
# Headers + Utility Libraries + Volk (dynamic extension loader)
set(VULKAN_SDK_TAG vulkan-sdk-1.4.321.0)
CPMAddPackage(
  NAME VulkanHeaders
  GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
  GIT_TAG ${VULKAN_SDK_TAG}
)
CPMAddPackage(
  NAME VulkanUtilityLibraries
  GITHUB_REPOSITORY KhronosGroup/Vulkan-Utility-Libraries
  GIT_TAG ${VULKAN_SDK_TAG}
)
CPMAddPackage(
  NAME volk
  GITHUB_REPOSITORY zeux/volk
  GIT_TAG ${VULKAN_SDK_TAG}
  # OPTIONS
  #   VULKAN_HEADERS_INSTALL_DIR "${VulkanHeaders_SOURCE_DIR}/include"
)

# To be sure to find vk_platform.h (on Windows).
target_include_directories(volk PUBLIC "${VulkanHeaders_SOURCE_DIR}/include")
# target_include_directories(volk PUBLIC "$ENV{VULKAN_SDK}/Include")

list(APPEND VULKAN_INCLUDE_DIR
  ${volk_SOURCE_DIR}
  ${VulkanHeaders_SOURCE_DIR}/include
  ${VulkanUtilityLibraries_SOURCE_DIR}/include
  $ENV{VULKAN_SDK}/Include
)

# Vulkan Memory Allocator (VMA)
CPMAddPackage(
  NAME VulkanMemoryAllocator
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_TAG v3.2.0
)
set(VMA_INCLUDE_DIR  ${VulkanMemoryAllocator_SOURCE_DIR}/include)

# CGLTF
CPMAddPackage(
  NAME cgltf
  GITHUB_REPOSITORY jkuhlmann/cgltf
  GIT_TAG v1.14
)
set(CGLTF_INCLUDE_DIR ${cgltf_SOURCE_DIR})

# Draco (not currently supported)
# see https://github.com/google/draco/blob/1.5.7/BUILDING.md#cmake-build-configuration
# option(FRAMEWORK_HAS_DRACO OFF)
if(FRAMEWORK_HAS_DRACO)
  # CPMAddPackage(
  #   NAME Draco
  #   GITHUB_REPOSITORY google/draco
  #   GIT_TAG 1.5.7
  #   OPTIONS
  #     "DRACO_TRANSCODER_SUPPORTED OFF"
  # )
  # set(DRACO_INCLUDE_DIR  ${Draco_SOURCE_DIR})

  # list(APPEND SharedIncludeDirs ${DRACO_INCLUDE_DIR})
  # list(APPEND SharedLibs draco)
endif()

# MikkTSpace (build as Static Lib).
CPMAddPackage(
  NAME mikktspace
  GITHUB_REPOSITORY mmikk/MikkTSpace
  GIT_TAG 3e895b49d05ea07e4c2133156cfa94369e19e409
  DOWNLOAD_ONLY YES
)
add_library(mikktspace STATIC
  ${mikktspace_SOURCE_DIR}/mikktspace.c
  ${mikktspace_SOURCE_DIR}/mikktspace.h
)
set(MIKKTSPACE_INCLUDE_DIR ${mikktspace_SOURCE_DIR})

# libfmt, formatting library emulating std20.
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 12.0.0
)
set(FMT_INCLUDE_DIR ${fmt_SOURCE_DIR}/include)

# linalg (single header linear algebra library).
CPMAddPackage(
  NAME linalg
  GITHUB_REPOSITORY sgorsten/linalg
  GIT_TAG v2.2
  DOWNLOAD_ONLY YES
)
set(LINALG_INCLUDE_DIR ${linalg_SOURCE_DIR})

# LibM.
findLibM()

# ImGui Docking.
if(TRUE)
  CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.92.3-docking # updating this version will break
  )

  if(ANDROID)
    set(IMGUI_WM_BACKEND android)
  else()
    set(IMGUI_WM_BACKEND glfw)
  endif()

  if(OS_LINUX)
    # Required to build imgui_glfw_impl
    find_package(X11 REQUIRED)
  endif()

  list(APPEND IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_${IMGUI_WM_BACKEND}.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_${IMGUI_WM_BACKEND}.h
  )
  source_group("Imgui" FILES ${IMGUI_SOURCES})
  # Tell Imgui to use volk's vulkan header.
  set_source_files_properties(${IMGUI_SOURCES}
    PROPERTIES
      COMPILE_DEFINITIONS
        IMGUI_IMPL_VULKAN_USE_VOLK
  )
  if(OS_LINUX)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_FLAGS
      "-Wno-null-dereference"
    )
  endif()

  target_sources(${target} PRIVATE ${IMGUI_SOURCES})
  set(IMGUI_INCLUDE_DIR ${imgui_SOURCE_DIR})
endif()

# Earcut (triangulation ehader).
CPMAddPackage(
  NAME earcut
  GITHUB_REPOSITORY mapbox/earcut.hpp
  GIT_TAG v2.2.4
  DOWNLOAD_ONLY YES
)
set(EARCUT_INCLUDE_DIR ${earcut_SOURCE_DIR}/include)

# UTF8
CPMAddPackage(
  NAME utfcpp
  GITHUB_REPOSITORY nemtrif/utfcpp
  GIT_TAG v4.0.8
  DOWNLOAD_ONLY YES
)
set(UTFCPP_INCLUDE_DIR ${utfcpp_SOURCE_DIR}/source)

# STB libs
set(STB_LIB_URL https://raw.githubusercontent.com/nothings/stb/refs/heads/master/)
list(APPEND STB_LIBS_TO_FETCH
  stb_truetype.h # (currently v1.26)
)

foreach (stb_lib ${STB_LIBS_TO_FETCH})
  download_file(
    ${PROJECT_THIRD_PARTY_DIR}/stb
    ${STB_LIB_URL}/${stb_lib}
  )
endforeach()

# -----------------------------------------------------------------------------
# Target properties.
# -----------------------------------------------------------------------------

target_link_libraries(${target}
  PUBLIC
    ${PlatformLibs} #!
  PRIVATE
    ${X11_LIBRARIES}
    volk
    fmt
    VulkanMemoryAllocator
    mikktspace
    ${LIBM_LIBRARIES}
)

target_include_directories(${target}
  PUBLIC
    ${FRAMEWORK_INCLUDE_DIR}
    ${PROJECT_THIRD_PARTY_DIR}
    ${PlatformIncludeDirs}
    ${VULKAN_INCLUDE_DIR}
    ${VMA_INCLUDE_DIR}
    ${FMT_INCLUDE_DIR}
    ${LINALG_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}
    ${UTFCPP_INCLUDE_DIR}
  PRIVATE
    ${FRAMEWORK_SHADERS_DIR}
    ${CGLTF_INCLUDE_DIR}
    ${MIKKTSPACE_INCLUDE_DIR}
    ${EARCUT_INCLUDE_DIR}
)

target_compile_definitions(${target}
  PUBLIC
    ${CustomDefinitions}
    # Define VK_NO_PROTOTYPES to avoid including Vulkan prototypes
    # This is necessary because we are using volk to load Vulkan functions
    VK_NO_PROTOTYPES=1
  PRIVATE
    FRAMEWORK_HAS_DRACO=0
)

if(ANDROID)
  target_compile_definitions(${target}
    PUBLIC
      ASSETS_DIR=""
      FRAMEWORK_COMPILED_SHADERS_DIR="${FRAMEWORK_SPIRV_ASSETS_SUBDIR}/"
      VK_USE_PLATFORM_ANDROID_KHR=1
      ANDROID_NDK=1
  )
else()
  target_compile_definitions(${target}
    PUBLIC
      ASSETS_DIR="${PROJECT_ASSETS_DIR}/"
      FRAMEWORK_COMPILED_SHADERS_DIR="${FRAMEWORK_COMPILED_SHADERS_DIR}/" #!
      
  )
endif()

target_compile_options(${target} PRIVATE
  "${CXX_FLAGS}"
  "$<$<CONFIG:Debug>:${CXX_FLAGS_DEBUG}>"
  "$<$<CONFIG:Release>:${CXX_FLAGS_RELEASE}>"
  "$<$<CONFIG:DebugWithRelInfo>:${CXX_FLAGS_RELWITHDEBINFO}>"
)

set_target_properties(${target}
  PROPERTIES
    LINK_FLAGS "${CXX_LINK_FLAGS}"
)

# -----------------------------------------------------------------------------
# Compile Framework shaders.
# -----------------------------------------------------------------------------

# SPIR-V Shaders.
compile_shaders(
  ${FRAMEWORK_SHADERS_DIR}
  ${FRAMEWORK_COMPILED_SHADERS_DIR}
  shaders_bin
  shaders_src
  ${FRAMEWORK_SHADERS_DIR}
)

add_custom_target(
  BuildFrameworkShaders ALL
  DEPENDS
    ${shaders_bin}
    ${shaders_src}
)
add_dependencies(${target} BuildFrameworkShaders)

# -----------------------------------------------------------------------------
