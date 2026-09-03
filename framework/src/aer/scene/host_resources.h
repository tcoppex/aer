#ifndef AER_SCENE_HOST_RESOURCES_H_
#define AER_SCENE_HOST_RESOURCES_H_

#include "aer/core/common.h"

#include "aer/scene/animation.h"
#include "aer/scene/texture.h"
#include "aer/scene/image_data.h"
#include "aer/scene/material.h"
#include "aer/scene/mesh.h"

#include "aer/scene/ecs/hierarchy.h" //

/* -------------------------------------------------------------------------- */

namespace scene {

// (to remove to only use std::vector instead)
template<typename T>
using ResourceBuffer = std::vector<std::unique_ptr<T>>;

template<typename T>
using ResourceMap = std::unordered_map<std::string, std::unique_ptr<T>>;

using IndexMap = std::unordered_map<std::string, uint32_t>;

// ----------------------------------------------------------------------------

struct HostResources {
 public:
  // Use threads to extract internal GLTF assets & load images asynchronously.
  static bool constexpr kUseAsyncLoad{true};

  // Force all loaded meshes to match VertexInternal_t structure.
  static bool constexpr kRestructureAttribs{true};

  // For consistency and simplicity across shaders, even if 16bit is common.
  // Required for RayTracing.
  static bool constexpr kForce32BitsIndexing{true};

 public:
  HostResources() = default;
  ~HostResources() = default;

  void setup();

  [[nodiscard]]
  bool loadFile(std::string_view filename);

  [[nodiscard]]
  MaterialProxy const& material_proxy(MaterialRef const& ref) const {
    return material_proxies[ref.proxy_index];
  }

  [[nodiscard]]
  mat4 const& root_matrix() const;

 protected:
  [[nodiscard]]
  bool loadGLTF(std::string_view filename);

  void resetInternalDescriptors();

  void updateSceneTreeTransforms();

 public:
  scene::Hierarchy scene_tree{};   //

  /* --- Host Data --- */

  std::vector<Sampler> samplers{};
  std::vector<ImageData> host_images{}; // (not trivially moveable)
  std::vector<Texture> textures{};

  std::vector<MaterialProxy> material_proxies{};
  ResourceBuffer<MaterialRef> material_refs{}; //

  ResourceBuffer<Mesh> meshes{};    // [todo: don't use unique_ptr for Meshes]
  IndexMap mesh_indices_map{};      // [deprecated]

  // -------
  // Used to store the buffer of global transforms, caculated by the hierarchy.
  // Should not be changed directly.
  std::vector<mat4f> transforms{};  // [move to scene_tree ?]
  // -------

  ResourceBuffer<Skeleton> skeletons{}; //
  ResourceMap<AnimationClip> animations_map{};

  uint32_t vertex_buffer_size{0u};
  uint32_t index_buffer_size{0u};
  uint32_t total_image_size{0u};

 protected:
  MaterialProxy::TextureBinding default_texture_binding_{};
};

} // namespace scene

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_HOST_RESOURCES_H_
