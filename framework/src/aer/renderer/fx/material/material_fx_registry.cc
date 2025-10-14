#include "aer/renderer/fx/material/material_fx_registry.h"

#include "aer/platform/vulkan/context.h"
#include "aer/renderer/renderer.h"

#include "aer/renderer/fx/material/impl/pbr_metallic_roughness.h" //
#include "aer/renderer/fx/material/impl/unlit.h" //

/* -------------------------------------------------------------------------- */

void MaterialFxRegistry::init(Renderer const& renderer) {
  fx_map_ = {
    {
      scene::MaterialModel::PBRMetallicRoughness,
      new fx::material::PBRMetallicRoughnessFx()
    },
    {
      scene::MaterialModel::Unlit,
      new fx::material::UnlitMaterialFx()
    },
  };

  for (auto [_, fx] : fx_map_) {
    fx->init(renderer);
  }
}

// ----------------------------------------------------------------------------

void MaterialFxRegistry::release() {
  for (auto [_, fx] : fx_map_) {
    fx->release();
    delete fx;
  }
  *this = {};
}

// ----------------------------------------------------------------------------

void MaterialFxRegistry::setup(
  std::vector<scene::MaterialProxy> const& material_proxies,
  std::vector<std::unique_ptr<scene::MaterialRef>>& material_refs
) {
  LOG_CHECK(material_proxies.size() == material_refs.size());

  // Register the needed MaterialFx+MaterialStates for every material proxies.
  for (auto const& material_ref : material_refs) {
    states_map_[material_ref->model].insert(material_ref->states);
  }

  // ----------------------
  // Create associated pipelines and internal buffers for each active MaterialFx.
  active_fx_.clear();
  for (auto const& [model, states_set] : states_map_) {
    MaterialFx *fx = fx_map_.at(model);
    if (!fx->valid()) {
      fx->setup();
    }
    fx->createPipelines({states_set.cbegin(), states_set.cend()});
    active_fx_.emplace_back(fx);
  }
  // Clear the states for possible future setup.
  states_map_.clear();
  // ----------------------

  // Create internal material for each MaterialFx.
  for (size_t i = 0; i < material_proxies.size(); ++i) {
    auto &matref = *material_refs[i];
    MaterialFx* fx = fx_map_.at(matref.model);
    matref.material_index = fx->createMaterial(material_proxies[i]);
  }
}

// ----------------------------------------------------------------------------

void MaterialFxRegistry::push_material_storage_buffers() const {
  for (auto fx : active_fx_) {
    fx->pushMaterialStorageBuffer();
  }
}

// ----------------------------------------------------------------------------

MaterialFx* MaterialFxRegistry::material_fx(scene::MaterialRef const& ref) const {
  if (auto it = fx_map_.find(ref.model); it != fx_map_.end()) {
    return it->second;
  }
  return nullptr;
}

/* -------------------------------------------------------------------------- */

