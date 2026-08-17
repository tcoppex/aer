# 
# CMake helpers commands to find and build GLSL file directly to spirv,
# with include handling and shader stage detection (based on prefix / suffix).
# 
# this need the Vulkan SDK installed.
# 
# -----------------------------------------------------------------------------

## When set to TRUE, the SPIR-V binaries will remove the last extension of
## previous source files (eg. simple.frag.glsl -> simple.frag.spv)
set(REMOVE_ORIGINAL_SHADER_EXTENSION TRUE)

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

## Custom function to generate binary shaders from GLSL, with include handling.
function(glsl2spirv input_glsl output_spirv shader_dir deps extra_args)
  # Retrieve the input file name
  get_filename_component(fn ${input_glsl} NAME)
  
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

  get_filename_component(output_dir ${output_spirv} DIRECTORY)

  # ----------------------------

  # Destroy the output directory on clean if it was not created by the user
  # beforehand.

  string(MAKE_C_IDENTIFIER "${output_dir}" output_dir_id)
  set(var_name "${output_dir_id}_CREATED_BY_CMAKE")

  if(NOT DEFINED ${var_name})
    set(${var_name} OFF CACHE INTERNAL "Did CMake create ${output_dir}?")
  endif()

  if(NOT IS_DIRECTORY "${output_dir}")
    file(MAKE_DIRECTORY "${output_dir}")
    set(${var_name} ON CACHE INTERNAL "Did CMake create ${output_dir}?")
  endif()

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
# -----------------------------------------------------------------------------

function(compile_glsl_shaders
  GLOBAL_GLSL_DIR
  GLOBAL_SPIRV_DIR
  binaries
  sources
  extra_dir
)
  # Retrieve all SOURCE glsl shaders
  file(GLOB_RECURSE g_ShadersGLSL ${GLOBAL_GLSL_DIR}/*.*)

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
    ${GLOBAL_GLSL_DIR}/../interop.h ##
    ${GLOBAL_GLSL_DIR}/../*.glsl
  )

  file(GLOB_RECURSE ShadersDependencies_bis
    ${extra_dir}/*.h
    ${extra_dir}/*.glsl
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
    set(source ${GLOBAL_GLSL_DIR}/${glslshader})

    # output binary final filename
    set(binary ${GLOBAL_SPIRV_DIR}/${glslshader})
    if (REMOVE_ORIGINAL_SHADER_EXTENSION)
      cmake_path(REMOVE_EXTENSION binary LAST_ONLY)
    endif()
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
# (Work In Progress)
# -----------------------------------------------------------------------------

function(compile_slang_shaders
  GLOBAL_SLANG_DIR
  GLOBAL_SPIRV_DIR
  binaries
  sources
  extra_dir
)
  file(GLOB_RECURSE SHADER_SLANG_FILES FILES ${GLOBAL_SLANG_DIR}/*.slang)

  # Convert each Slang shaders into a SpirV binary
  foreach(shader IN LISTS SHADER_SLANG_FILES)

    file(RELATIVE_PATH shader_rel "${GLOBAL_SLANG_DIR}" "${shader}")
    get_filename_component(output_dir "${GLOBAL_SPIRV_DIR}/${shader_rel}" DIRECTORY)

    compile_slang(
      "${shader}"
      "${output_dir}"
      SPVS_VAR
        binary
      # HEADERS_VAR
      #   local_sources
      # CAPABILITIES
      #   spvDescriptorHeapEXT
      EXTRA_FLAGS
        "-I${extra_dir} -I${GLOBAL_SLANG_DIR}"
    )

    if (REMOVE_ORIGINAL_SHADER_EXTENSION)
      cmake_path(REMOVE_EXTENSION binary LAST_ONLY)
      cmake_path(REMOVE_EXTENSION binary LAST_ONLY)
      set(binary ${binary}.spv)
    endif()

    list(APPEND spvBinaries ${binary})
  endforeach()

  set(${binaries} "${spvBinaries}" PARENT_SCOPE)
  # set(${sources} "${local_sources}" PARENT_SCOPE)
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

## Compile all shaders (*.glsl / *.slang) from one directory to another.
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