# -----------------------------------------------------------------------------
# 
#  Android samples
# 
# -----------------------------------------------------------------------------

set(SAMPLES_PACKAGE_NAME  cc.polysfaer.vkframework.samples)

set(LOGGER_ANDROID_TAG    VkFramework) #!

# -----------------------------------------------------------------------------

if(NOT DEFINED ENV{ANDROID_HOME})
  message(STATUS "Environment variable ANDROID_HOME is not set.")
endif()

# -----------------------------------------------------------------------------

function(add_activity_target suffix)
  if(DEFINED ENV{ANDROID_HOME})
    set(ADB_CMD $ENV{ANDROID_HOME}/platform-tools/adb)

    set(PACKAGE_NAME ${SAMPLES_PACKAGE_NAME}.${suffix})
    set(ADB_RESOLVE_CMD
      "${ADB_CMD} shell cmd package resolve-activity --brief ${PACKAGE_NAME}"
    )
    set(ADB_LAUNCH_CMD
      "${ADB_CMD} shell am start -n \$ACTIVITY"
    )
    set(ADB_RESOLVE_AND_LAUNCH_CMD
      "ACTIVITY=\$(${ADB_RESOLVE_CMD} | awk 'NR==2 {print \$NF}'); ${ADB_LAUNCH_CMD}"
    )
    set(ADB_LOGCAT_CMD
      ${ADB_CMD} logcat -v color -v raw
    )

    list(APPEND BuildTypes "Debug")
    foreach(BuildType IN LISTS BuildTypes)
      set(targetPrefix build)#${BuildType})
      add_custom_target(${targetPrefix}_${suffix}
        COMMAND
          ./gradlew :${suffix}:assembleDebug#${targetPrefix}
        WORKING_DIRECTORY
          ${ANDROID_BUILD_DIR}
        COMMENT
          "Build Android DEBUG target \"${suffix}\"."
        VERBATIM
      )

      set(targetPrefix install)#${BuildType})
      add_custom_target(${targetPrefix}_${suffix}
        COMMAND
          ./gradlew :${suffix}:installDebug#${targetPrefix}
        WORKING_DIRECTORY
          ${ANDROID_BUILD_DIR}
        COMMENT
          "Install Android DEBUG target \"${suffix}\" on device."
        VERBATIM
      )

      add_custom_target(run_${suffix}#run${BuildType}_${suffix}
        COMMAND
          ${CMAKE_COMMAND} -E env bash -c "${ADB_RESOLVE_AND_LAUNCH_CMD}"
        DEPENDS
          ${targetPrefix}_${suffix}
        WORKING_DIRECTORY
          ${ANDROID_BUILD_DIR}
        COMMENT
          "Run Android DEBUG target \"${suffix}\" on device."
        VERBATIM
      )

      set(LOGCAT_OUTPUT_FILEPATH ${CMAKE_BINARY_DIR}/${suffix}_${BuildType}.log)

      add_custom_target(log_${suffix}#${BuildType}_${suffix}
        COMMAND
          ${ADB_CMD} logcat -c
        COMMAND
          # ${ADB_LOGCAT_CMD} | grep --line-buffered "${SAMPLES_PACKAGE_NAME}"
          # ${ADB_LOGCAT_CMD} | grep ${LOGGER_ANDROID_TAG}
          ${ADB_LOGCAT_CMD} *:F ${LOGGER_ANDROID_TAG}:D | tee ${LOGCAT_OUTPUT_FILEPATH}
        DEPENDS
          run_${suffix}#${BuildType}_${suffix}
        WORKING_DIRECTORY
          ${ANDROID_BUILD_DIR}
        COMMENT
          "Log Android target \"${suffix}\" on device.\n(copy at ${LOGCAT_OUTPUT_FILEPATH})"
        VERBATIM
      )
    endforeach()

  endif()
endfunction()

function(add_android_sample target)
  add_activity_target(${target})
  if(ANDROID)
    add_subdirectory(${target})
  endif()
endfunction()

# -----------------------------------------------------------------------------

add_android_sample(aloha)
add_android_sample(aloha_xr)

# -----------------------------------------------------------------------------
