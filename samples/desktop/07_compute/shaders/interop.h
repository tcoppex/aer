#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

const uint kAttribLocation_Position = 0;

const uint kDescriptorSetBinding_UniformBuffer = 0;
const uint kDescriptorSetBinding_StorageBuffer_Position = 1;
const uint kDescriptorSetBinding_StorageBuffer_Index = 2;
const uint kDescriptorSetBinding_StorageBuffer_DotProduct = 3;

const uint kCompute_Simulation_kernelSize_x = 256;
const uint kCompute_FillIndex_kernelSize_x = 256;
const uint kCompute_DotProduct_kernelSize_x = 256;
const uint kCompute_SortIndex_kernelSize_x = 256;

const float kTwoPi = 6.28318530718f;

// ---------------------------------------------------------------------------

struct Camera {
  mat4 viewMatrix;
  mat4 projectionMatrix;
};

struct Model {
  mat4 worldMatrix;
};

// ---------------------------------------------------------------------------

struct Scene {
  Camera camera;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Scene scene;
};

struct PushConstant_Graphics {
  Model model;
};

struct PushConstant_Compute {
  Model model;
  float time;
  uint numElems;
  uint padding_[2];
  uint readOffset;
  uint writeOffset;
  uint blockWidth;
  uint maxBlockWidth;
};

// ---------------------------------------------------------------------------

struct PushConstant {
  PushConstant_Graphics graphics;
  PushConstant_Compute compute;
};

// ---------------------------------------------------------------------------

#endif