#ifndef SHADERS_SCENE_PUSH_CONSTANT_GENERIC_H_
#define SHADERS_SCENE_PUSH_CONSTANT_GENERIC_H_

// ----------------------------------------------------------------------------

/// Notes
///
/// * We separate PushConstant_Generic from interop.h to avoid inclusion
///   issue with cpp.
///
/// * Currently we could simply use PushConstant_Generic everywhere instead of
///   redefining PushConstant struct per material type, but we let the possibility
///   to customize them hence the need to separate the generic definition.
///

// [32 bytes < 128 bytes]
struct PushConstant_Generic {
  uint64_t transform_buffer_address;
  uint64_t material_buffer_address;
  uint transform_index;
  uint material_index;
  uint instance_index;
  uint _pad0[1]; // uint dynamic_states;
};

// ----------------------------------------------------------------------------

#ifndef __cplusplus
#define GetTransform(TransformBuffer)   TransformBuffer[pushConstant.generic.transform_index]
#define GetMaterial(MaterialBuffer)     MaterialBuffer[pushConstant.generic.material_index]
#endif

// ----------------------------------------------------------------------------


#endif //