# -----------------------------------------------------------------------------
#
# Samples
#
# -----------------------------------------------------------------------------

## On Android build we skip desktop as we don't need to build them,
## on Desktop build we still look into the android subdir to capture Gradle
## special targets for CMake.

if(NOT ANDROID)
add_subdirectory(${PROJECT_SAMPLES_DIR}/desktop)
endif()

add_subdirectory(${PROJECT_SAMPLES_DIR}/android)

# -----------------------------------------------------------------------------
