# -----------------------------------------------------------------------------
#
# CMake helpers commands to build GLSL and Slang shaders to spir-v.
# (bit of a janky mess atm)
#
# - GLSL expects 'glslc' and detects the shader stage automatically based
#     its the filename.
#
# - Slang use 'slangc' and will convert any shaders in a "shared" subfolder to
#     slang modules.
#
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------

## Search for the GLSL Compiler binary.
if (WIN32)
  if (CMAKE_CL_64)
    find_program(GLSLC glslc
      "$ENV{VULKAN_SDK}/Bin"
      "$ENV{VK_SDK_PATH}/Bin"
    )
  else()
    find_program(GLSLC glslc
      "$ENV{VULKAN_SDK}/Bin32"
      "$ENV{VK_SDK_PATH}/Bin32"
    )
  endif()
else()
    find_program(GLSLC glslc
      "$ENV{VULKAN_SDK}/bin"
    )
endif()

# -----------------------------------------------------------------------------

## Custom function to generate binary shaders using GLSL.
function(glsl2spirv input_glsl output_spirv shader_dir deps extra_args)
  # Retrieve the input file name
  get_filename_component(fn "${input_glsl}" NAME)
  
  # Detects shader type based on its suffix or prefix
  if (${fn} MATCHES "((vert|vs)_.+\\.glsl)|(.+\\.(vert|vs)(\\.glsl)?)")
    set(stage "vert")
  elseif(${fn} MATCHES "((tesc|tcs)_.+\\.glsl)|(.+\\.(tesc|tcs)(\\.glsl)?)")
    set(stage "tesc")
  elseif(${fn} MATCHES "((tese|tes)_.+\\.glsl)|(.+\\.(tese|tes)(\\.glsl)?)")
    set(stage "tese")
  elseif(${fn} MATCHES "((geom|gs)_.+\\.glsl)|(.+\\.(geom|gs)(\\.glsl)?)")
    set(stage "geom")
  elseif(${fn} MATCHES "((frag|fs)_.+\\.glsl)|(.+\\.(frag|fs)(\\.glsl)?)")
    set(stage "frag")
  elseif(${fn} MATCHES "((comp|cs)_.+\\.glsl)|(.+\\.(comp|cs)(\\.glsl)?)")
    set(stage "comp")
  elseif(${fn} MATCHES "((mesh|ms)_.+\\.glsl)|(.+\\.(mesh|ms)(\\.glsl)?)")
    set(stage "mesh")
  elseif(${fn} MATCHES "(.+\\.rgen)")
  elseif(${fn} MATCHES "(.+\\.rmiss)")
  elseif(${fn} MATCHES "(.+\\.rchit)")
  elseif(${fn} MATCHES "(.+\\.rahit)")
  else()
    message(WARNING "Unknown shader type for ${fn}")
    # return()
  endif()

  get_filename_component(output_dir "${output_spirv}" DIRECTORY)

  # ----------------------------
  # Create an identifer for the Output directory.
  string(MAKE_C_IDENTIFIER "${output_dir}" output_dir_id)
  set(var_name "${output_dir_id}_CREATED_BY_CMAKE")
  if(NOT DEFINED ${var_name})
    set(${var_name} OFF CACHE INTERNAL "Did CMake create ${output_dir}?")
  endif()

  # Create the Output directory.
  if(NOT IS_DIRECTORY "${output_dir}")
    file(MAKE_DIRECTORY "${output_dir}")
    set(${var_name} ON CACHE INTERNAL "Did CMake create ${output_dir}?")
  endif()

  # Mark it to be cleaned when it was not created by the user beforehand.
  if(${var_name})
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${output_dir}")
  endif()
  # ----------------------------

  if(NOT stage OR stage STREQUAL "")
    set(command "")
  else()
    set(command "-fshader-stage=${stage}")
  endif()

  # Compile to SPIR-V with include directory set to shaderdir
  add_custom_command(
    OUTPUT
      ${output_spirv}
    COMMAND
      ${GLSLC} --target-env=vulkan1.3 -D_GLSL_ ${command} -o ${output_spirv} ${input_glsl} -I ${shader_dir} ${extra_args}
    DEPENDS
      ${input_glsl}
      ${GLSLC}
      ${deps}
    WORKING_DIRECTORY
      ${CMAKE_SOURCE_DIR}
    COMMENT
      "Converting shader ${input_glsl} to ${output_spirv}"
    MAIN_DEPENDENCY
      ${input_glsl}
    VERBATIM
  )

  # Forcing compilation can be tricky.
  # To always recompile, use
  # add_custom_target( gen_${fn} ALL ..
  # otherwise, set the output files as dependencies.
endfunction(glsl2spirv)

# -----------------------------------------------------------------------------

function(compile_glsl_shaders
  GLOBAL_GLSL_DIR
  GLOBAL_SPIRV_DIR
  binaries
  sources
  extra_dir
)
  # Retrieve all SOURCE glsl shaders
  file(GLOB_RECURSE g_ShadersGLSL "${GLOBAL_GLSL_DIR}/*.*")

  # Only keep shaders of the form "filename.stage.glsl"
  set(RaytraceShadersREGEX ".+\\.rgen$|.+\\.rmiss$|.+\\.rchit|.+\\.rahit$")
  list(
    FILTER
      g_ShadersGLSL
    INCLUDE
    REGEX
    ".+\\..+\\.glsl$|${RaytraceShadersREGEX}"
  )

  file(GLOB_RECURSE ShadersDependencies
    "${GLOBAL_GLSL_DIR}/../interop.h" ##
    "${GLOBAL_GLSL_DIR}/../*.glsl"
  )

  file(GLOB_RECURSE ShadersDependencies_bis
    "${extra_dir}/*.h"
    "${extra_dir}/*.glsl"
  )
  list(APPEND ShadersDependencies ${ShadersDependencies_bis})

  # Transform shader path to relative
  foreach(glslshader IN LISTS g_ShadersGLSL)
    file(RELATIVE_PATH glslshader 
      ${GLOBAL_GLSL_DIR}
      ${glslshader}
    )
    list(APPEND ShadersGLSL ${glslshader})
  endforeach()

  # Convert each GLSL shaders into a SpirV binary
  foreach(glslshader IN LISTS ShadersGLSL)
    # set global output binary filename
    set(source "${GLOBAL_GLSL_DIR}/${glslshader}")

    # output binary final filename
    set(binary "${GLOBAL_SPIRV_DIR}/${glslshader}")

    # if (REMOVE_ORIGINAL_SHADER_EXTENSION)
    #   cmake_path(GET binary EXTENSION LAST_ONLY binary_ext)
    #   if ("${binary_ext}" STREQUAL ".glsl")
    #     cmake_path(REMOVE_EXTENSION binary LAST_ONLY)
    #   endif()
    # endif()

    set(binary "${binary}.spv")

    # compile GLSL to SPIRV
    glsl2spirv(${source} ${binary} ${GLOBAL_GLSL_DIR} "${ShadersDependencies}" -I${extra_dir})

    # return the list of compiled filed
    list(APPEND glslSHADERS ${source})
    list(APPEND spirvSHADERS ${binary})
  endforeach()

  set(${sources} "${glslSHADERS}" PARENT_SCOPE)
  set(${binaries} "${spirvSHADERS}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------

macro(slang2spirv shader)
  set(options "")
  set(oneValueArgs TARGET)
  set(multiValueArgs "")

  cmake_parse_arguments(SLANG
    "${options}" "${oneValueArgs}" "${multiValueArgs}"
    ${ARGN}
  )

  # Default target.
  if(NOT SLANG_TARGET)
    set(SLANG_TARGET "spirv")
  endif()

  # Determine output dir from relative path.
  file(RELATIVE_PATH shader_rel "${GLOBAL_SLANG_DIR}" "${shader}")
  get_filename_component(output_dir "${GLOBAL_SPIRV_DIR}/${shader_rel}" DIRECTORY)

  # Add Framework + user defined dir as include path.
  # (do not put in quotes)
  set(EXTRA_INCLUDE_DIRS -I${GLOBAL_SLANG_DIR} -I${extra_dir}) # -I${GLOBAL_SPIRV_DIR}

  if(SLANG_TARGET STREQUAL "slang-module")
    # special custom target to slang module.
    compile_slang(
      "${shader}"
      "${output_dir}"
      SLANG_MODULES_VAR
        binary #!!
      EXTRA_FLAGS
        "${EXTRA_INCLUDE_DIRS}"
      # VERBOSE ON
    )
  else()
    compile_slang(
      "${shader}"
      "${output_dir}"
      TARGET
        "${SLANG_TARGET}"
      SPVS_VAR
        binary
      EXTRA_FLAGS
        "${EXTRA_INCLUDE_DIRS}"
      # VERBOSE ON
    )
  endif()

  # if (REMOVE_ORIGINAL_SHADER_EXTENSION)
  #   cmake_path(REMOVE_EXTENSION binary LAST_ONLY)
  #   cmake_path(REMOVE_EXTENSION binary LAST_ONLY)
  #   set(binary "${binary}.spv")
  # endif()

  list(APPEND spvBinaries "${binary}")
endmacro()

# -----------------------------------------------------------------------------

function(compile_slang_shaders
  GLOBAL_SLANG_DIR
  GLOBAL_SPIRV_DIR
  binaries
  sources
  extra_dir
)
  if (FALSE)

    # # All framework's slang shader.
    # file(GLOB_RECURSE SHADER_SLANG_FILES FILES "${GLOBAL_SLANG_DIR}/*.slang")

    # foreach(shader IN LISTS SHADER_SLANG_FILES)
    #   slang2spirv("${shader}")
    # endforeach()

  else()

    set(SHARED_SHADER_DIRNAME "shared")

    # All shared/'module' slang shaders
    file(GLOB_RECURSE SHADER_SLANG_SHARED_FILES
      "${GLOBAL_SLANG_DIR}/${SHARED_SHADER_DIRNAME}/*.slang"
    )

    # Every shaders relative path.
    file(GLOB_RECURSE SHADER_SLANG_OTHER_FILES RELATIVE "${GLOBAL_SLANG_DIR}"
      "${GLOBAL_SLANG_DIR}/*.slang"
    )
    # Exclude shared / modules.
    list(FILTER SHADER_SLANG_OTHER_FILES EXCLUDE REGEX
      "^${SHARED_SHADER_DIRNAME}/"
    )
    # All non shared shader global path.
    list(TRANSFORM SHADER_SLANG_OTHER_FILES PREPEND "${GLOBAL_SLANG_DIR}/")

    # Slang-Modules (first).
    # foreach(module IN LISTS SHADER_SLANG_SHARED_FILES)
    #   slang2spirv("${module}" TARGET slang-module)
    # endforeach()

    # SPIR-Vs.
    foreach(shader IN LISTS SHADER_SLANG_OTHER_FILES)
      slang2spirv("${shader}" TARGET spirv)
    endforeach()

  endif()

  set(${binaries} "${spvBinaries}" PARENT_SCOPE)
  # set(${sources} "${local_sources}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------

## Compile all shaders (*.glsl / *.slang) from one directory to another.
## Used by the framework.
function(compile_shaders
  GLOBAL_SHADER_DIR
  GLOBAL_SPIRV_DIR
  binaries
  sources
  extra_dir
)
  set(local_binaries "")
  set(local_sources "")

  compile_glsl_shaders(${GLOBAL_SHADER_DIR} ${GLOBAL_SPIRV_DIR} glsl_binaries glsl_sources ${extra_dir})
  if(glsl_binaries)
    list(APPEND local_binaries ${glsl_binaries})
    list(APPEND local_sources ${glsl_sources})
  endif()

  compile_slang_shaders(${GLOBAL_SHADER_DIR} ${GLOBAL_SPIRV_DIR} slang_binaries slang_sources ${extra_dir})
  if(slang_binaries)
    list(APPEND local_binaries ${slang_binaries})
    list(APPEND local_sources ${slang_sources})
  endif()

  set(${binaries} "${local_binaries}" PARENT_SCOPE)
  set(${sources} "${local_sources}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------

## Compile all shaders (glsl and slang alike) from glsl/ and slang/ subdirectories.
## Used by the samples.
function(compile_sample_shaders
  GLOBAL_SHADER_DIR
  GLOBAL_SPIRV_DIR
  binaries
  sources
  extra_dir
)
  set(GLOBAL_GLSL_DIR   "${GLOBAL_SHADER_DIR}/glsl")
  set(GLOBAL_SLANG_DIR  "${GLOBAL_SHADER_DIR}/slang")

  set(local_binaries "")
  set(local_sources "")

  if (IS_DIRECTORY "${GLOBAL_GLSL_DIR}")
    compile_glsl_shaders(${GLOBAL_GLSL_DIR} ${GLOBAL_SPIRV_DIR} glsl_binaries glsl_sources ${extra_dir})
    if(glsl_binaries)
      list(APPEND local_binaries ${glsl_binaries})
      list(APPEND local_sources ${glsl_sources})
    endif()
  endif()

  if (IS_DIRECTORY "${GLOBAL_SLANG_DIR}")
    compile_slang_shaders(${GLOBAL_SLANG_DIR} ${GLOBAL_SPIRV_DIR} slang_binaries slang_sources ${extra_dir})
    if(slang_binaries)
      list(APPEND local_binaries ${slang_binaries})
      list(APPEND local_sources ${slang_sources})
    endif()
  endif()

  set(${binaries} "${local_binaries}" PARENT_SCOPE)
  set(${sources} "${local_sources}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------