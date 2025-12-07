#ifndef AER_SCENE_PRIVATE_GLTF_LOADER_H_
#define AER_SCENE_PRIVATE_GLTF_LOADER_H_

#include "aer/core/common.h"

#include "aer/scene/host_resources.h"
#include "aer/scene/private/cgltf_wrapper.h"

/* -------------------------------------------------------------------------- */

namespace internal::gltf_loader {

template<typename T>
using PointerMap_t = std::unordered_map<void const*, T>;

using PointerToIndexMap_t = PointerMap_t<uint32_t>;
using PointerToSamplerMap_t = PointerMap_t<scene::Sampler>;

/* -------------------------------------------------------------------------- */

PointerToSamplerMap_t ExtractSamplers(
  cgltf_data const* data,
  std::vector<scene::Sampler>& samplers
);

PointerToIndexMap_t ExtractImages(
  cgltf_data const* data,
  std::vector<scene::ImageData>& images
);

PointerToIndexMap_t ExtractTextures(
  cgltf_data const* data,
  PointerToIndexMap_t const& image_indices,
  PointerToSamplerMap_t const& samplers_lut, //
  std::vector<scene::Texture>& textures
);

PointerToIndexMap_t ExtractMaterials(
  cgltf_data const* data,
  PointerToIndexMap_t const& textures_indices,
  std::vector<scene::MaterialProxy>& material_proxies,
  scene::ResourceBuffer<scene::MaterialRef>& material_refs,
  scene::MaterialProxy::TextureBinding const& bindings
);

PointerToIndexMap_t ExtractSkeletons(
  cgltf_data const* data,
  scene::ResourceBuffer<scene::Skeleton>& skeletons
);

void ExtractMeshes(
  cgltf_data const* data,
  PointerToIndexMap_t const& materials_indices,
  scene::ResourceBuffer<scene::MaterialRef>const& material_refs,
  PointerToIndexMap_t const& skeleton_indices,
  scene::ResourceBuffer<scene::Skeleton>const& skeletons,
  scene::ResourceBuffer<scene::Mesh>& meshes,
  scene::IndexMap &mesh_indices_map,
  std::vector<mat4f>& transforms,
  bool const bRestructureAttribs,
  bool const bForce32bitsIndex
);

void ExtractAnimations(
  cgltf_data const* data,
  std::string const& basename,
  scene::ResourceMap<scene::Skeleton>& skeletons_map,
  scene::ResourceMap<scene::AnimationClip>& animations_map
);

} // namespace internal::gltf_loader

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_PRIVATE_GLTF_LOADER_H_