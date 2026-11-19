#ifndef AER_SCENE_ASSET_HANDLE_H_
#define AER_SCENE_ASSET_HANDLE_H_

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

struct AssetHandle {
  uint32_t index{kInvalidIndexU32};
  uint32_t generation{0};

  [[nodiscard]]
  bool valid() const noexcept { return index != kInvalidIndexU32; }

  bool operator==(AssetHandle const&) const = default;
};

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ASSET_HANDLE_H_