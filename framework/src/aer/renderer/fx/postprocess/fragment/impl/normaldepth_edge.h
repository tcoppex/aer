#ifndef POST_NORMAL_DEPTH_EDGE_H_
#define POST_NORMAL_DEPTH_EDGE_H_

#include "aer/renderer/fx/postprocess/fragment/render_target_fx.h"

/* -------------------------------------------------------------------------- */

namespace fx::frag {

class NormalDepthEdge final : public RenderTargetFx {
 public:
  // [tmp]
  void setInputs(backend::Image const& data_image, backend::Buffer const& depth_minmax_buffer) {
    set_image_input(data_image);
    set_buffer_input(depth_minmax_buffer);
  }

  void setNormalThreshold(float value) {
    push_constant_.normal_threshold = value;
  }

  void setDepthThreshold(float value) {
    push_constant_.depth_threshold = value;
  }

 public:
  void setupUI() final {
    if (!ImGui::CollapsingHeader("Normal Edge")) {
      return;
    }
    ImGui::SliderFloat("normal threshold", &push_constant_.normal_threshold, 0.2f, 3.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    // ImGui::SliderFloat("depth threshold", &push_constant_.depth_threshold, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
  }

 private:
  std::vector<VkPushConstantRange> push_constant_ranges() const final {
    return {
      {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(push_constant_),
      }
    };
  }

  void pushConstant(GenericCommandEncoder const &cmd) const final {
    cmd.pushConstant(push_constant_, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT);
  }

  std::string shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "postprocess/normaldepth_edge.frag.glsl";
  }

 private:
  struct {
    float normal_threshold{1.5f};
    float depth_threshold{1.0f};
  } push_constant_;
};

}

/* -------------------------------------------------------------------------- */

#endif // POST_NORMAL_DEPTH_EDGE_H_
