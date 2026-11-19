#ifndef AER_SCENE_ASSET_H_
#define AER_SCENE_ASSET_H_

#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

#include "aer/scene/ecs/components.h" // for component::Transform
#include "aer/scene/load_options.h"
#include "aer/scene/image_data.h"
#include "aer/scene/texture.h"
#include "aer/scene/material.h"
#include "aer/scene/mesh.h"
// #include "aer/scene/sampler.h"
// #include "aer/scene/animation.h"

/* -------------------------------------------------------------------------- */

namespace scene {

struct PrefabNode {
  uint32_t parent_index{kInvalidIndexU32};
  component::Transform transform{};
  uint32_t mesh_index{kInvalidIndexU32};
  std::string name{};
};

// ----------------------------------------------------------------------------

struct Prefab {
  std::vector<PrefabNode> nodes{};
  std::vector<std::vector<uint32_t>> scenes{};      // root node ids per glTF scene
  uint32_t default_scene{0u};
};

// ----------------------------------------------------------------------------

struct Asset {
  std::vector<ImageData>      images{};             // (not trivially moveable)
  std::vector<Texture>        textures{};
  std::vector<MaterialProxy>  materials{};
  std::vector<MaterialRef>    material_refs{};
  std::vector<Mesh>           meshes{};
  // std::vector<Sampler>        samplers{};
  // std::vector<Skeleton>       skeletons{};
  // std::vector<AnimationClip>  animations{};

  std::shared_ptr<const Prefab> prefab{};           // outlives geometry bytes
};

// ----------------------------------------------------------------------------

// thread-safe: writes only its own Asset
std::optional<Asset> LoadGLTF(std::string_view filename, LoadOptions = {});

} // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ASSET_H_
