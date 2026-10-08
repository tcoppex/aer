# -----------------------------------------------------------------------------
# 
# Framework
# 
# -----------------------------------------------------------------------------

set(target aer)

if(ANDROID)
  # We add a suffix to avoid collision with the Desktop build.
  # This require to be also set in the framework build.gradle.
  set(target ${target}_android)
endif()

# -----------------------------------------------------------------------------
# Paths.
# -----------------------------------------------------------------------------

# The framework spirv directory is also used by the Android gradle build to
# migrate them to samples APK assets directory, so we need a distinct subdir for
# them not to be in conflict with others assets (shaders or otherwhise).
set(FRAMEWORK_SPIRV_ASSETS_SUBDIR framework_shaders)
set(FRAMEWORK_COMPILED_SHADERS_DIR
  ${FRAMEWORK_COMPILED_SHADERS_DIR}/${FRAMEWORK_SPIRV_ASSETS_SUBDIR}
)

# -----------------------------------------------------------------------------
# Sources.
# -----------------------------------------------------------------------------

set(FRAMEWORK_PLATFORM_SRC_DIR ${FRAMEWORK_SOURCE_DIR}/aer/platform/impl)

if(ANDROID)
  set(FRAMEWORK_PLATFORM_SRC_SUB_DIR ${FRAMEWORK_PLATFORM_SRC_DIR}/android)
else()
  set(FRAMEWORK_PLATFORM_SRC_SUB_DIR ${FRAMEWORK_PLATFORM_SRC_DIR}/desktop)
endif()

file(GLOB_RECURSE AllSources CONFIGURE_DEPENDS
  LIST_DIRECTORIES FALSE
  ${FRAMEWORK_SOURCE_DIR}/*.cc
  ${FRAMEWORK_SOURCE_DIR}/*.h
)

set(Sources "")
foreach(src IN LISTS AllSources)
  cmake_path(IS_PREFIX FRAMEWORK_PLATFORM_SRC_DIR "${src}" NORMALIZE is_platform_src)
  if(NOT is_platform_src)
    list(APPEND Sources "${src}")
  endif()
endforeach()

file(GLOB_RECURSE PlatformSources CONFIGURE_DEPENDS
  LIST_DIRECTORIES FALSE
  ${FRAMEWORK_PLATFORM_SRC_SUB_DIR}/*.cc
  ${FRAMEWORK_PLATFORM_SRC_SUB_DIR}/*.h
)

# -----------------------------------------------------------------------------
# Target library.
# -----------------------------------------------------------------------------

add_library(${target} STATIC ${Sources} ${PlatformSources})
add_library(aer::framework ALIAS ${target})

# -----------------------------------------------------------------------------
# Third Parties Dependencies (FetchContent).
# -----------------------------------------------------------------------------

# --- Declarations ------------------------------------------------------------

# for OpenXR's find_package(Sanitizers)
aer_declare(sanitizers              arsenm/sanitizers-cmake     bcb1fc68616e9645ca5acea2992412606373ab04  SOURCE_ONLY)
aer_declare(OpenXR                  KhronosGroup/OpenXR-SDK     release-1.1.52)

set(VULKAN_SDK_TAG vulkan-sdk-1.4.321.0)
aer_declare(VulkanHeaders           KhronosGroup/Vulkan-Headers             ${VULKAN_SDK_TAG})
aer_declare(VulkanUtilityLibraries  KhronosGroup/Vulkan-Utility-Libraries   ${VULKAN_SDK_TAG}             SOURCE_ONLY)
aer_declare(volk                    zeux/volk                               ${VULKAN_SDK_TAG})
aer_declare(VulkanMemoryAllocator   GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator  v3.2.0)

aer_declare(fmt                     fmtlib/fmt                  12.0.0)
aer_declare(entt                    skypjack/entt               v3.16.0)
aer_declare(cgltf                   jkuhlmann/cgltf             v1.15                                     SOURCE_ONLY)
aer_declare(imgui                   ocornut/imgui               v1.92.3-docking                           SOURCE_ONLY)
aer_declare(linalg                  sgorsten/linalg             v2.2                                      SOURCE_ONLY)
aer_declare(earcut                  mapbox/earcut.hpp           v2.2.4                                    SOURCE_ONLY)
aer_declare(utfcpp                  nemtrif/utfcpp              v4.0.8                                    SOURCE_ONLY)
aer_declare(mikktspace              mmikk/MikkTSpace            3e895b49d05ea07e4c2133156cfa94369e19e409  SOURCE_ONLY)
aer_declare(miniply                 vilya/miniply               1a235c70390fadf789695c9ccbf285ae712416b3  SOURCE_ONLY)

set(AER_DEPENDENCIES
  VulkanHeaders
  VulkanUtilityLibraries
  volk
  VulkanMemoryAllocator
  OpenXR
  fmt
  entt
  cgltf
  linalg
  earcut
  utfcpp
  imgui
  mikktspace
  miniply
)

# GLFW (desktop only).
if(NOT ANDROID)
  set(GLFW_BUILD_DOCS        OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND     OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL           OFF CACHE BOOL "" FORCE)
  aer_declare(glfw glfw/glfw 3.4)
  list(APPEND AER_DEPENDENCIES glfw)
endif()

# --- Fetch -------------------------------------------------------------------

# OpenXR calls find_package(Sanitizers): make its Find module reachable first.
FetchContent_MakeAvailable(sanitizers)
list(APPEND CMAKE_MODULE_PATH ${sanitizers_SOURCE_DIR}/cmake)

FetchContent_MakeAvailable(${AER_DEPENDENCIES})

# --- Targets for the dependencies we only use as sources ---------------------

# Vulkan headers are deliberately NOT marked SYSTEM: the pinned headers must stay
# ahead of any Vulkan SDK found on the include path (-I always beats -isystem),
# otherwise SDK and pinned headers get mixed (version mismatch errors).
aer_header_only(aer_vk_utility NO_SYSTEM INCLUDE_DIRS ${vulkanutilitylibraries_SOURCE_DIR}/include)

get_target_property(vk_headers_real Vulkan::Headers ALIASED_TARGET)
if(NOT vk_headers_real)
  set(vk_headers_real Vulkan::Headers)
endif()
set_target_properties(${vk_headers_real} PROPERTIES SYSTEM FALSE)

# volk needs the Vulkan headers (vk_platform.h, notably on Windows).
target_link_libraries(volk PUBLIC Vulkan::Headers)

aer_header_only(aer_cgltf  INCLUDE_DIRS ${cgltf_SOURCE_DIR})
aer_header_only(aer_linalg INCLUDE_DIRS ${linalg_SOURCE_DIR})
aer_header_only(aer_earcut INCLUDE_DIRS ${earcut_SOURCE_DIR}/include)
aer_header_only(aer_utfcpp INCLUDE_DIRS ${utfcpp_SOURCE_DIR}/source)

# Disable all warnings for the OpenXR loader target.
if(TARGET openxr_loader)
  target_compile_options(openxr_loader PRIVATE -w)
endif()

# -----------------------------------------------------------------------------
# Single file download.
# -----------------------------------------------------------------------------

set(STB_COMMIT 2c980bb59875b0d32144a71867fbdebb2f77cd20)

foreach(stb_lib stb_truetype.h stb_image.h)
  download_file(
    ${PROJECT_THIRD_PARTY_DIR}/stb
    https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/${stb_lib}
  )
endforeach()

# -----------------------------------------------------------------------------
# Platform specifics.
# -----------------------------------------------------------------------------

if(ANDROID)
  find_library(ANDROID_LIBRARY     NAMES android)
  find_library(ANDROID_LOG_LIBRARY NAMES log)

  list(APPEND PlatformLibs
    ${ANDROID_LIBRARY}
    ${ANDROID_LOG_LIBRARY}
  )

  # Properly add the Native App Glue dependencies to its consuming target.
  function(helpers_addNativeAppGlue consumer)
    find_path(ANDROID_NATIVE_APP_GLUE_PATH android_native_app_glue.h
      PATHS "${ANDROID_NDK}/sources/android/native_app_glue"
    )
    # [OBJECT] Android Native App Glue.
    if(NOT TARGET android_native_app_glue)
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
    endif()
    target_include_directories(${consumer} PUBLIC ${ANDROID_NATIVE_APP_GLUE_PATH})
    target_link_libraries(${consumer} PUBLIC $<TARGET_OBJECTS:android_native_app_glue>)
  endfunction()

  # (we might prefer to link the native app to the sample in the future)
  helpers_addNativeAppGlue(${target})

  # (Alternatively just uncomment the following line.)
  # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

  set(IMGUI_WM_BACKEND android)
  set(IMGUI_WM_LIBRARY ${ANDROID_LIBRARY})
else()
  # glfw exports its own include directory.
  list(APPEND PlatformLibs glfw)

  set(IMGUI_WM_BACKEND glfw)
  set(IMGUI_WM_LIBRARY glfw)
endif()

if(OS_LINUX)
  # Required to build imgui_glfw_impl.
  find_package(X11 REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Embedded third parties libraries.
# -----------------------------------------------------------------------------

# miniply (.ply loader)
add_library(aer_miniply STATIC ${miniply_SOURCE_DIR}/miniply.cpp)
target_include_directories(aer_miniply SYSTEM PUBLIC ${miniply_SOURCE_DIR})
if(OS_LINUX)
  target_compile_options(aer_miniply PRIVATE -Wno-shadow)
endif()

# MikkTSpace (build as Static Lib).
add_library(mikktspace STATIC
  ${mikktspace_SOURCE_DIR}/mikktspace.c
  ${mikktspace_SOURCE_DIR}/mikktspace.h
)
target_include_directories(mikktspace SYSTEM PUBLIC ${mikktspace_SOURCE_DIR})

# ImGui Docking.
if(TRUE)
  set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_${IMGUI_WM_BACKEND}.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_${IMGUI_WM_BACKEND}.h
    # ${imgui_SOURCE_DIR}/imgui_demo.cpp ##
  )
  source_group("Imgui" FILES ${IMGUI_SOURCES})

  add_library(aer_imgui STATIC ${IMGUI_SOURCES})
  target_include_directories(aer_imgui
    SYSTEM PUBLIC
      ${imgui_SOURCE_DIR}
      ${imgui_SOURCE_DIR}/backends
  )
  target_compile_definitions(aer_imgui
    PUBLIC
      IMGUI_IMPL_VULKAN_USE_VOLK
  )
  target_link_libraries(aer_imgui
    PUBLIC
      volk
      Vulkan::Headers
    PRIVATE
      ${IMGUI_WM_LIBRARY}
  )
  if(OS_LINUX)
    target_compile_options(aer_imgui PRIVATE -Wno-null-dereference)
  endif()
endif()

# -----------------------------------------------------------------------------
# Target properties.
# -----------------------------------------------------------------------------

set(PublicLibs
  ${PlatformLibs}
  Vulkan::Headers
  volk
  VulkanMemoryAllocator
  fmt::fmt
  EnTT::EnTT
  OpenXR::headers
  aer_vk_utility
  aer_linalg
  aer_utfcpp
  aer_imgui
  aer_miniply
)

set(PrivateLibs
  OpenXR::openxr_loader
  mikktspace
  aer_cgltf
  aer_earcut
)
if(NOT WIN32)
  list(APPEND PrivateLibs m)
endif()
if(OS_LINUX)
  list(APPEND PrivateLibs X11::X11)
endif()

target_link_libraries(${target}
  PUBLIC  ${PublicLibs}
  PRIVATE ${PrivateLibs}
)

target_include_directories(${target}
  PUBLIC
    ${FRAMEWORK_INCLUDE_DIR}
    ${PROJECT_THIRD_PARTY_DIR}
  PRIVATE
    ${FRAMEWORK_SHADERS_DIR}
)

target_precompile_headers(${target}
  PRIVATE
    <volk.h>
    <entt/entt.hpp>
    <linalg.h>
    <lina/lina.h>
)

target_compile_definitions(${target}
  PUBLIC
    ${CustomDefinitions}
    # Define VK_NO_PROTOTYPES to avoid including Vulkan prototypes.
    # This is necessary because we are using volk to load Vulkan functions.
    VK_NO_PROTOTYPES=1
    # OpenXR
    USE_OPENXR=1
    XR_USE_GRAPHICS_API_VULKAN=1
  PRIVATE
    FRAMEWORK_HAS_DRACO=0
)

if(ANDROID)
  target_compile_definitions(${target}
    PUBLIC
      ASSETS_DIR=""
      FRAMEWORK_COMPILED_SHADERS_DIR="${FRAMEWORK_SPIRV_ASSETS_SUBDIR}/"
      VK_USE_PLATFORM_ANDROID_KHR=1
      XR_USE_PLATFORM_ANDROID=1
      ANDROID_NDK=1
  )
else()
  target_compile_definitions(${target}
    PUBLIC
      ASSETS_DIR="${PROJECT_ASSETS_DIR}/"
      FRAMEWORK_COMPILED_SHADERS_DIR="${FRAMEWORK_COMPILED_SHADERS_DIR}/"
  )
endif()

target_compile_options(${target} PRIVATE
  "${CXX_FLAGS}"
  "$<$<CONFIG:Debug>:${CXX_FLAGS_DEBUG}>"
  "$<$<CONFIG:Release>:${CXX_FLAGS_RELEASE}>"
  "$<$<CONFIG:RelWithDebInfo>:${CXX_FLAGS_RELWITHDEBINFO}>"
)

# -----------------------------------------------------------------------------
# Compile Framework shaders.
# -----------------------------------------------------------------------------

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