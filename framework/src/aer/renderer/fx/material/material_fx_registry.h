#ifndef AER_RENDERER_FX_MATERIAL_MATERIAL_FX_REGISTRY_H_
#define AER_RENDERER_FX_MATERIAL_MATERIAL_FX_REGISTRY_H_

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"  // for DescriptorSetWriteEntry
#include "aer/scene/material.h" // for scene::MaterialRef, scene::MaterialProxy

#include <set>

class Context;
class Renderer;
class MaterialFx;

/* -------------------------------------------------------------------------- */

class MaterialFxRegistry {
 public:
  MaterialFxRegistry() = default;

  /* Create the initial material fx LUT. */
  void init(Renderer const& renderer);

  /* Release all allocated resources. */
  void release();

  /* Create internal resources for all used MaterialFx. */
  void setup(
    std::vector<scene::MaterialProxy> const& material_proxies,
    std::vector<std::unique_ptr<scene::MaterialRef>>& material_refs
  );

  /* Push updated for all MaterialFx. */
  void push_material_storage_buffers() const;

  /* Getters */

  MaterialFx* material_fx(scene::MaterialRef const& ref) const;

 private:
  using MaterialModel = scene::MaterialModel; // std::type_index

  using MaterialFxMap     = std::unordered_map<MaterialModel, MaterialFx*>;
  using MaterialStatesMap = std::unordered_map<MaterialModel, std::set<scene::MaterialStates>>;

 private:
  MaterialFxMap fx_map_{};
  MaterialStatesMap states_map_{};
  std::vector<MaterialFx*> active_fx_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_MATERIAL_MATERIAL_FX_REGISTRY_H_
