#include "aer/scene/asset.h"

#include <iostream>
#include "aer/scene/private/gltf_loader.h"

/* -------------------------------------------------------------------------- */

namespace scene {

std::optional<Asset> LoadGLTF(std::string_view filename, LoadOptions load_options) {
  auto const basename = utils::ExtractBasename(filename);
  auto const ext      = utils::ExtractExtension(filename);

  cgltf_options options{};
  cgltf_result result{};
  cgltf_data* data{};

  utils::FileReader file{};
  if (!file.read(filename)) {
    LOGE("GLTF: failed to read the file.");
    return {};
  }

  result = cgltf_parse(&options, file.buffer.data(), file.buffer.size(), &data);
  if (cgltf_result_success != result) {
    LOGE("GLTF: failed to parse file \"{}\" {}.\n", basename, (int)result);
    return {};
  }

  result = cgltf_load_buffers(&options, data, filename.data());
  if (cgltf_result_success != result) {
    LOGE("GLTF: failed to load buffers in \"{}\" {}.\n", basename, (int)result);
    cgltf_free(data);
    return {};
  }

  Asset A{};

  /* Extract data */
  {
    using namespace internal::gltf_loader;

    // All scenes of the file are described in Asset::prefab (one root list each).
    // Resources are shared by every scene and extracted once.

    // Reserve data.
    A.images.reserve(data->images_count);
    A.textures.reserve(data->textures_count);
    A.materials.reserve(data->materials_count);
    A.material_refs.reserve(data->materials_count);
    A.meshes.reserve(data->meshes_count);
    // A.samplers.reserve(data->samplers_count);

    ExtractImages(data, A.images);
    ExtractTextures(data, A.textures);
    ExtractMaterials(data, A.images, A.textures, A.materials, A.material_refs);

    auto const mesh_remap = ExtractMeshes(
      data,
      static_cast<uint32_t>(A.materials.size() - 1u), // last is default material
      load_options,
      A.meshes
    );

    // Skeletons and the clips driving them live in the prefab, as they are
    // host data shared by every instance.
    std::vector<scene::Skeleton> skeletons{};
    ExtractSkeletons(data, skeletons);
    if (load_options.load_animations) {
      ExtractAnimations(data, load_options, skeletons);
    }

    A.prefab = ExtractPrefab(data, mesh_remap, std::move(skeletons));
  }

  /* Recalculate the scene global matrices buffer. */
  // updateSceneTreeTransforms();

  /* Wait for the host images to finish loading before using them. */
  for (auto & image : A.images) {
    image.getAsyncResult();
  }
  cgltf_free(data);

#if !defined(NDEBUG)
  size_t clip_count = 0u;
  for (auto const& skeleton : A.prefab->skeletons) {
    clip_count += skeleton.clips.size();
  }

  LOGI("> \"{}.{}\" has been loaded successfully.", basename, ext);
  std::cout << "┌────────────┬───── " << std::endl;
  std::cout << "│ Images     │ " << A.images.size()     << std::endl;
  std::cout << "│ Textures   │ " << A.textures.size()   << std::endl;
  std::cout << "│ Materials  │ " << (A.materials.size()-1) << " + default" << std::endl;
  std::cout << "│ Meshes     │ " << A.meshes.size()     << std::endl;
  std::cout << "│ Skeletons  │ " << A.prefab->skeletons.size() << std::endl;
  std::cout << "│ Clips      │ " << clip_count << std::endl;
  std::cout << "│ Nodes      │ " << A.prefab->nodes.size()  << std::endl;
  std::cout << "│ Scenes     │ " << A.prefab->scenes.size() << std::endl;
  std::cerr << "└────────────┴─────" << std::endl;
#endif

  return A;
}

Asset CreateMeshAsset(Mesh mesh, std::string name) {
  Asset asset{};
  asset.meshes.push_back(std::move(mesh));

  auto prefab = std::make_shared<Prefab>();
  prefab->nodes.push_back({
    .parent_index = kInvalidIndexU32,
    .transform    = {},
    .mesh_index   = 0u,
    .name         = std::move(name),
  });
  prefab->scenes.push_back({ 0u });
  prefab->default_scene = 0u;

  asset.prefab = std::move(prefab);
  return asset;
}

} // namespace "scene"

/* -------------------------------------------------------------------------- */

#if !defined(NDEBUG)

/* Checks to assert the data structures can be moved / assigned cheaply. */

STATIC_ASSERT_TRIVIALITY(scene::Sampler);
STATIC_ASSERT_TRIVIALITY(scene::Texture);
STATIC_ASSERT_TRIVIALITY(scene::MaterialProxy);
STATIC_ASSERT_TRIVIALITY(scene::MaterialRef);

STATIC_ASSERT_MOVABLE_ONLY(scene::ImageData);

static_assert(std::is_nothrow_move_constructible_v<scene::Mesh>);

// [TODO]
// STATIC_ASSERT_MOVABLE_ONLY(scene::Mesh);
// STATIC_ASSERT_MOVABLE_ONLY(scene::Skeleton);

#endif // NDEBUG

/* -------------------------------------------------------------------------- */
