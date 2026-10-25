#include <algorithm>

#include "aer/renderer/fx/material/material_fx_registry.h"

#include "aer/renderer/render_context.h"
#include "aer/renderer/fx/material/impl/pbr_metallic_roughness.h" //
#include "aer/renderer/fx/material/impl/unlit.h" //

/* -------------------------------------------------------------------------- */

void MaterialFxRegistry::init(RenderContext const& context) {
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
    fx->init(context);
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

std::vector<MaterialFxRegistry::Entry> MaterialFxRegistry::add(
  std::span<scene::MaterialProxy const> proxies,
  std::span<scene::MaterialRef> refs
) {
  using States = std::set<scene::MaterialStates>;

  /* 1. Validate, without touching anything. */
  std::unordered_map<MaterialModel, uint32_t> counts{};
  std::unordered_map<MaterialModel, States> new_states{};

  for (auto const& ref : refs) {
    auto it = fx_map_.find(ref.model);
    if (it == fx_map_.end()) {
      LOGE("MaterialFxRegistry::add: unknown material model.");
      return {};
    }
    if (ref.proxy_index >= proxies.size()) {
      LOGE("MaterialFxRegistry::add: proxy index out of range.");
      return {};
    }
    ++counts[ref.model];
    if (!it->second->has_pipeline(ref.states)) {
      new_states[ref.model].insert(ref.states);
    }
  }
  for (auto const& [model, count] : counts) {
    if (!fx_map_.at(model)->has_room(count)) {
      LOGE("MaterialFxRegistry::add: material buffer is full.");
      return {};
    }
  }

  /* 2. Setup fx on first use, create only the missing pipelines. */
  for (auto const& [model, _] : counts) {
    MaterialFx* fx = fx_map_.at(model);
    if (!fx->valid()) {
      fx->setup();
    }
    if (std::ranges::find(active_fx_, fx) == active_fx_.end()) {
      active_fx_.push_back(fx);
    }
    if (auto it = new_states.find(model); it != new_states.end()) {
      fx->createPipelines({it->second.cbegin(), it->second.cend()});
    }
  }

  /* 3. Create the materials. */
  std::vector<Entry> entries{};
  entries.reserve(refs.size());
  for (auto& ref : refs) {
    MaterialFx* fx = fx_map_.at(ref.model);
    ref.material_index = fx->createMaterial(proxies[ref.proxy_index]);
    entries.push_back({ fx, ref.material_index });
  }

  /* 4. Upload each touched buffer once. */
  for (auto const& [model, _] : counts) {
    fx_map_.at(model)->uploadMaterialStorageBuffer();
  }

  return entries;
}

// ----------------------------------------------------------------------------

void MaterialFxRegistry::uploadMaterialStorageBuffers() const {
  for (auto fx : active_fx_) {
    fx->uploadMaterialStorageBuffer();
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

