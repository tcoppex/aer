cmake_minimum_required(VERSION 3.22.1)

# -----------------------------------------------------------------------------
# Project.
# -----------------------------------------------------------------------------

set(PROJECT_NAME aer_framework)

project(${PROJECT_NAME}
  VERSION
    1.0
  LANGUAGES
    CXX C
  HOMEPAGE_URL
    "https://github.com/tcoppex/aer"
)

# -----------------------------------------------------------------------------
# Paths.
# -----------------------------------------------------------------------------

set(PROJECT_ROOT_PATH               ${CMAKE_CURRENT_SOURCE_DIR})

set(ANDROID_BUILD_DIR               ${PROJECT_ROOT_PATH}/android)
set(PROJECT_ASSETS_DIR              ${PROJECT_ROOT_PATH}/assets)
set(SAMPLES_PATH                    ${PROJECT_ROOT_PATH}/samples)
set(PROJECT_BINARY_DIR              ${PROJECT_ROOT_PATH}/bin)
set(PROJECT_THIRD_PARTY_DIR         ${PROJECT_ROOT_PATH}/third_party)
set(DEFAULT_CPM_SOURCE_CACHE        ${PROJECT_THIRD_PARTY_DIR}/.cpmlocalcache)

set(FRAMEWORK_PATH                  ${PROJECT_ROOT_PATH}/framework)
set(FRAMEWORK_SOURCE_DIR            ${FRAMEWORK_PATH}/src)
set(FRAMEWORK_INCLUDE_DIR           ${FRAMEWORK_PATH}/src) #!
set(FRAMEWORK_SHADERS_DIR           ${FRAMEWORK_SOURCE_DIR}/aer/shaders) #!
set(FRAMEWORK_COMPILED_SHADERS_DIR  ${PROJECT_BINARY_DIR}/spirv)

# -----------------------------------------------------------------------------
# CMake settings.
# -----------------------------------------------------------------------------

# Default CMake behavior.
set(CMAKE_VERBOSE_MAKEFILE          OFF)
set(CMAKE_COLOR_MAKEFILE            ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD   ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS   ON)
set(CMAKE_DISABLE_SOURCE_CHANGES    OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default standards with no extensions.
set(CMAKE_C_STANDARD                11)
set(CMAKE_CXX_STANDARD              20)
set(CMAKE_CXX_STANDARD_REQUIRED     ON)
set(CMAKE_CXX_EXTENSIONS            OFF)

# -----------------------------------------------------------------------------
# CMake includes
# -----------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Vulkan SDK helpers
include(VulkanSDK)

# Download CPM.cmake
set(CPM_DOWNLOAD_VERSION 0.40.3)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  message(STATUS "Downloading CPM.cmake...")
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
  )
endif()

# Set a share cache for CPM if none exist.
if(NOT DEFINED CPM_SOURCE_CACHE)
  if(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_SOURCE_CACHE "$ENV{CPM_SOURCE_CACHE}")
  else()
    set(CPM_SOURCE_CACHE ${DEFAULT_CPM_SOURCE_CACHE})
  endif()
  set(CPM_SOURCE_CACHE "${CPM_SOURCE_CACHE}" CACHE PATH "CPM cache location")
endif()

include(${CPM_DOWNLOAD_LOCATION})

# -----------------------------------------------------------------------------
# Custom functions
# -----------------------------------------------------------------------------

function(set_global_output_directory OUTPUT_DIR)
  # Default output directory.
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR} CACHE PATH "")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR} CACHE PATH "")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR} CACHE PATH "")
endfunction()

function(set_target_output_directory target output_dir)
  # Force output directory destination, especially for MSVC (@so7747857).
  foreach(type RUNTIME LIBRARY ARCHIVE)
    set_target_properties(${target} PROPERTIES
      ${type}_OUTPUT_DIRECTORY         ${output_dir}
      ${type}_OUTPUT_DIRECTORY_DEBUG   ${output_dir}
      ${type}_OUTPUT_DIRECTORY_RELEASE ${output_dir}
    )
  endforeach()
endfunction()

function(findLibM)
  # Chuck Atkins @ https://cmake.org/pipermail/cmake/2019-March/069168.html
  include(CheckCSourceCompiles)
  set(LIBM_TEST_SOURCE "#include<math.h>\nint main(){float f = sqrtf(4.0f);}")
  check_c_source_compiles("${LIBM_TEST_SOURCE}" HAVE_MATH)
  if(HAVE_MATH)
    set(LIBM_LIBRARIES)
  else()
    set(CMAKE_REQUIRED_LIBRARIES m)
    check_c_source_compiles("${LIBM_TEST_SOURCE}" HAVE_LIBM_MATH)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT HAVE_LIBM_MATH)
      message(FATAL_ERROR "Unable to use C math library functions.")
    endif()
    set(LIBM_LIBRARIES m)
  endif()
endfunction(findLibM)

# -----------------------------------------------------------------------------
# Custom macros
# -----------------------------------------------------------------------------

## {MACRO} Setup the default parameters for a target.
##   @TARGET        : Unique name referencing the library as dependency.
##   @INCLUDE_DIRS  : List of path for include files.
##   @LIBRARIES     : List of libraries to link against.
##   @DEFINITIONS   : List of definitions.
macro(helpers_setupTarget)
  set(oneValueArgs TARGET)
  set(multiValueArgs INCLUDE_DIRECTORIES LIBRARIES DEFINITIONS)
  cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" "${ARGN}")
  target_compile_options(${ARG_TARGET}
    PRIVATE
      "${CXX_FLAGS}"
      "$<$<CONFIG:Debug>:${CXX_FLAGS_DEBUG}>"
      "$<$<CONFIG:Release>:${CXX_FLAGS_RELEASE}>"
      "$<$<CONFIG:DebugWithRelInfo>:${CXX_FLAGS_RELWITHDEBINFO}>"
  )
  target_include_directories(${ARG_TARGET} PRIVATE "${ARG_INCLUDE_DIRECTORIES}")
  target_link_libraries(${ARG_TARGET} PRIVATE "${ARG_LIBRARIES}")
  target_compile_definitions(${ARG_TARGET} PRIVATE "${ARG_DEFINITIONS}")
  set_target_properties(${ARG_TARGET} PROPERTIES LINK_FLAGS "${CXX_LINK_FLAGS}")
endmacro()

# -----------------------------------------------------------------------------
# Internal configuration attributes.
# -----------------------------------------------------------------------------

# Detects if the build is flagged for full debug mode.
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(DEBUG TRUE)
  message(STATUS "DEBUG build.")
else()
  set(DEBUG FALSE)
endif()

# Detects the build platform.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT WEBGL)
  set(OS_NAME "Linux")
  set(OS_LINUX TRUE)
elseif(WIN32)
  set(OS_NAME "Windows")
  set(OS_WIN32 TRUE)
elseif(ANDROID)
  set(OS_NAME "Android")
  set(OS_ANDROID TRUE)
else()
  message(WARNING "The \"${CMAKE_SYSTEM_NAME}\" platform is not supported.")
endif()

message(STATUS "Configured for system \"${OS_NAME}\".")

# Customize based on platform.
if(ANDROID)
  if (${ANDROID_PLATFORM_LEVEL} LESS 29)
    message(FATAL_ERROR "Android is not supported before API level 29.")
  endif()

  # Capture renamed definitions.
  if(NOT DEFINED ANDROID_ABI)
    set(ANDROID_ABI ${CMAKE_ANDROID_ARCH_ABI})
  endif()
  if(NOT DEFINED ANDROID_NDK)
    set(ANDROID_NDK ${CMAKE_ANDROID_NDK})
  endif()

  # Setup clang tidy.
  if(DEBUG)
    # message(STATUS "clang-tidy enabled.")
    # set(CLANG_TIDY_PATH
    #   "${ANDROID_TOOLCHAIN_ROOT}/bin/clang-tidy${ANDROID_TOOLCHAIN_SUFFIX}"
    # )
    # set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_PATH} "-checks=android-*")
  endif()
else()
  # On Desktop we force all outputs to the binary directory.
  set_global_output_directory(${PROJECT_BINARY_DIR})
endif()

# Detects the current compiler.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(USE_GCC TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
  set(USE_CLANG TRUE)
elseif(MSVC)
  set(USE_MSVC TRUE)
else()
  message(WARNING "The \"${CMAKE_CXX_COMPILER_ID}\" compiler is not officially supported.")
endif()

# -----------------------------------------------------------------------------
# Generic compiler settings.
# -----------------------------------------------------------------------------

if(USE_GCC OR USE_CLANG)
  # This is a mix of warning options not available in all GCC / Clang version.
  list(APPEND CXX_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wcast-align
    -Wlogical-op
    -Wnull-dereference
    -Wdisabled-optimization
    -Wvector-operation-performance
    -Wformat=2 -Wformat-truncation -Wformat-overflow
    -Wno-unknown-pragmas
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-invalid-offsetof
  )

  # Clang specific warnings.
  if(USE_CLANG)
    list(APPEND CXX_FLAGS
      -Wno-unknown-warning-option
      -Wno-nullability-extension
      -Wno-unused-lambda-capture
      -Wno-missing-braces
    )
  endif()

  # Features.
  list(APPEND CXX_FLAGS
    -fdiagnostics-color=always
    -ffunction-sections
    -fdata-sections
    -funroll-loops
    -fno-common
    # -fno-strict-aliasing
    # -fno-builtin-memcmp
    # -fno-exceptions
    # -ftree-loop-distribution
  )

  list(APPEND CXX_FLAGS_DEBUG           -O0 -g3)
  list(APPEND CXX_FLAGS_RELEASE         -O2 -msse4.1 -s)
  list(APPEND CXX_FLAGS_RELWITHDEBINFO  -O2 -msse4.1 -g1)

  # Linker flags.
  list(APPEND CustomLinkFlags
    -Wl,--build-id
    -Wl,--gc-sections
  )
elseif(USE_MSVC)
  list(APPEND CXX_FLAGS
    # /permissive-
    # /GR-         # disable RTTI
    # /EHsc-       # no exception
    # /W4           # Warning level 4 (strict warnings, less noisy than /Wall)
    /MP           # build with multiple process.
    /w14640       # Warn about thread-unsafe static member initialization
    /w14928       # Warn about illegal copy-initialization
    /w14265       # class has virtual functions but non-virtual destructor
    /w14555       # Warn about expressions with no effect/side-effect
    /wd4100       # Disable: "unused parameter" warnings
    /utf-8
  )
  list(APPEND CXX_FLAGS_DEBUG           /Zi)
  list(APPEND CXX_FLAGS_RELEASE         /Ox /INCREMENTAL:NO /DEBUG:NONE /DNDEBUG)
  # list(APPEND CXX_FLAGS_RELWITHDEBINFO  ${CXX_FLAGS_RELEASE} /Ox /Zi)

  list(APPEND CustomDefinitions
    # _HAS_EXCEPTIONS=0
    NOMINMAX
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES=1
    VC_EXTRALEAN
    WIN32_LEAN_AND_MEAN
  )
endif()

# Transform linker flags from a list to a string.
foreach(flag ${CustomLinkFlags})
  set(CXX_LINK_FLAGS "${CXX_LINK_FLAGS} ${flag}")
endforeach()

# -----------------------------------------------------------------------------

add_subdirectory(${PROJECT_ASSETS_DIR})
add_subdirectory(${FRAMEWORK_PATH})
add_subdirectory(${SAMPLES_PATH})

# -----------------------------------------------------------------------------
