#ifndef AER_SCENE_PRIVATE_GLTF_LOADER_H_
#define AER_SCENE_PRIVATE_GLTF_LOADER_H_

#include <vector>
#include <memory>

#include "aer/core/common.h"
#include "aer/scene/asset.h"
#include "aer/scene/load_options.h"
#include "aer/scene/private/cgltf_wrapper.h"

/* -------------------------------------------------------------------------- */

namespace internal::gltf_loader {

void ExtractImages(
  cgltf_data const*,
  std::vector<scene::ImageData>&
);

void ExtractTextures(
  cgltf_data const*,
  std::vector<scene::Texture>&
);

// Appends the default material last
void ExtractMaterials(
  cgltf_data const*,
  std::vector<scene::ImageData> const&,
  std::vector<scene::Texture> const&,
  std::vector<scene::MaterialProxy>&,
  std::vector<scene::MaterialRef>&
);

// Returns mesh_remap
[[nodiscard]]
std::vector<uint32_t> ExtractMeshes(
  cgltf_data const*,
  uint32_t default_material,
  scene::LoadOptions const&,
  std::vector<scene::Mesh>&
);

[[nodiscard]]
std::shared_ptr<scene::Prefab> ExtractPrefab(
  cgltf_data const*,
  std::vector<uint32_t> const& mesh_remap
);

} // namespace "internal::gltf_loader"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_PRIVATE_GLTF_LOADER_H_