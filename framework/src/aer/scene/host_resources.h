#ifndef AER_SCENE_HOST_RESOURCES_H_
#define AER_SCENE_HOST_RESOURCES_H_

#include "aer/core/common.h"

#include "aer/scene/animation.h"
#include "aer/scene/texture.h"
#include "aer/scene/image_data.h"
#include "aer/scene/material.h"
#include "aer/scene/mesh.h"

/* -------------------------------------------------------------------------- */

namespace scene {

// (to remove to only use std::vector instead)
template<typename T>
using ResourceBuffer = std::vector<std::unique_ptr<T>>;

template<typename T>
using ResourceMap = std::unordered_map<std::string, std::unique_ptr<T>>;

// ----------------------------------------------------------------------------

struct HostResources {
 public:
  // Use threads to extract internal GLTF assets & load images asynchronously.
  static bool constexpr kUseAsyncLoad{true};

  // Force all loaded meshes to match VertexInternal_t structure.
  static bool constexpr kRestructureAttribs{true};

  // For consistency and simplicity across shaders, even if 16bit is common.
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

  /* --- Host Data --- */

  std::vector<Sampler> samplers{};
  std::vector<ImageData> host_images{}; // (not trivially moveable)
  std::vector<Texture> textures{};

  std::vector<MaterialProxy> material_proxies{};
  ResourceBuffer<MaterialRef> material_refs{}; //

  ResourceBuffer<Mesh> meshes{}; //
  std::vector<mat4f> transforms{};

  ResourceBuffer<Skeleton> skeletons{}; //
  ResourceMap<AnimationClip> animations_map{};

  uint32_t vertex_buffer_size{0u};
  uint32_t index_buffer_size{0u};
  uint32_t total_image_size{0u};

 protected:
  void resetInternalDescriptors();

  MaterialProxy::TextureBinding default_texture_binding_{};
};

} // namespace scene

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_HOST_RESOURCES_H_
