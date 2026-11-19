#ifndef AER_RENDERER_FX_MATERIAL_MATERIAL_FX_REGISTRY_H_
#define AER_RENDERER_FX_MATERIAL_MATERIAL_FX_REGISTRY_H_

#include <set>
#include <span>
#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"  // for DescriptorSetWriteEntry
#include "aer/scene/material.h"         // for scene::MaterialRef, scene::MaterialProxy
#include "aer/renderer/fx/material/material_fx.h"

class RenderContext;

/* -------------------------------------------------------------------------- */

class MaterialFxRegistry {
 public:
  struct Entry {
    MaterialFx* fx{};
    uint32_t material_index{kInvalidIndexU32};
  };

 public:
  MaterialFxRegistry() = default;

  /* Create the initial material fx LUT. */
  void init(RenderContext const& context);

  /* Release all allocated resources. */
  void release();

  /* Register materials of a new asset. Returns one entry per ref, in order,
   * or an empty vector (nothing modified) on failure.
   * Fills MaterialRef::material_index. */
  [[nodiscard]]
  std::vector<Entry> add(
    std::span<scene::MaterialProxy const> proxies,
    std::span<scene::MaterialRef> refs
  );

  /* Push updated for all MaterialFx. */
  void uploadMaterialStorageBuffers() const;

  /* Getters */

  [[nodiscard]]
  MaterialFx* material_fx(scene::MaterialRef const& ref) const;

 private:
  using MaterialModel     = scene::MaterialModel; // std::type_index
  using MaterialFxMap     = std::unordered_map<MaterialModel, MaterialFx*>;
  // using MaterialStatesMap = std::unordered_map<MaterialModel, std::set<scene::MaterialStates>>;

 private:
  MaterialFxMap fx_map_{};
  // MaterialStatesMap states_map_{};
  std::vector<MaterialFx*> active_fx_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_MATERIAL_MATERIAL_FX_REGISTRY_H_
