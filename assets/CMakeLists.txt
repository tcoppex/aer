# -----------------------------------------------------------------------------
#
# External Assets Dependencies (CC0 or public domains).
#
# -----------------------------------------------------------------------------

function(download_model url)
  download_file(${PROJECT_ASSETS_DIR}/models/ ${url})
endfunction()

function(download_texture url)
  download_file(${PROJECT_ASSETS_DIR}/textures/ ${url})
endfunction()

# -----------------------------------------------------------------------------
# Textures
# -----------------------------------------------------------------------------

list(APPEND hdriPolyhavenAssets
  qwantani_dusk_2_2k
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

list(APPEND imageURLS
  https://raw.githubusercontent.com/brachior/scalajs-cordova-threejs-examples/refs/heads/master/www/img/whynot.png
)

foreach(imageURL IN LISTS imageURLS)
  download_texture("${imageURL}")
endforeach()

# -----------------------------------------------------------------------------
# Meshes
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

list(APPEND modelURLs
  https://raw.githubusercontent.com/SaschaWillems/VulkanPathTracer/refs/heads/master/data/models/CornellBox-Original.gltf
)

foreach(modelURL IN LISTS modelURLs)
  download_model("${modelURL}")
endforeach()

# -----------------------------------------------------------------------------
# Pointclouds
# -----------------------------------------------------------------------------

download_files(
  FILENAMES
    flowers_1.zip
  URLS
    "http://developer.download.nvidia.com/ProGraphics/nvpro-samples/flowers_1.zip"
  TARGET_DIR
    "${PROJECT_ASSETS_DIR}/pointclouds"
  EXTRACT
)

# -----------------------------------------------------------------------------
