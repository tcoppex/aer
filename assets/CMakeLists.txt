# -----------------------------------------------------------------------------
#
# External Assets Dependencies (CC0 or public domains).
#
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# Helper functions.
# -----------------------------------------------------------------------------

function(download_model url)
  download_file(${PROJECT_ASSETS_DIR}/models/ ${url})
endfunction()

function(download_texture url)
  download_file(${PROJECT_ASSETS_DIR}/textures/ ${url})
endfunction()

# -----------------------------------------------------------------------------
# Download Models.
# -----------------------------------------------------------------------------

list(APPEND glbKhronosAssets
  DamagedHelmet                   # (already on git-lfs)
  AlphaBlendModeTest
)

foreach(asset IN LISTS glbKhronosAssets)
  set(GLB_KHRONOS_ASSETS_URL
    "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models"
  )
  download_model(
    "${GLB_KHRONOS_ASSETS_URL}/${asset}/glTF-Binary/${asset}.glb"
  )
endforeach()

download_model(
  "https://raw.githubusercontent.com/SaschaWillems/VulkanPathTracer/refs/heads/master/data/models/CornellBox-Original.gltf"
)

# -----------------------------------------------------------------------------
# Download Textures.
# -----------------------------------------------------------------------------

list(APPEND hdriPolyhavenAssets
  qwantani_dusk_2_2k              # (already on git-lfs)
  rogland_clear_night_2k
)

foreach(asset IN LISTS hdriPolyhavenAssets)
  set(HDRI_POLYHAVEN_ASSETS_URL
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k"
  )
  download_texture(
    "${HDRI_POLYHAVEN_ASSETS_URL}/${asset}.hdr"
  )
endforeach()

# -----------------------------------------------------------------------------
