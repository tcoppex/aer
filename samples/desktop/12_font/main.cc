/* -------------------------------------------------------------------------- */
//
//    12 - Font
//
// Where we play with font rendering, ALL KIND OF !
//

/* -------------------------------------------------------------------------- */

#include "utf8/cpp20.h"

#include "aer/application.h"
#include "aer/core/arcball_controller.h"
#include "aer/scene/font.h"
#include "aer/scene/font_mesh.h"

namespace shader_interop {
#include "shaders/interop.h"
}

// ----------------------------------------------------------------------------

class SampleApp final : public Application {
 public:

  static constexpr std::array<const char*, 2> kFontSelection{
    "angeme/Angeme-Regular.ttf",
    "angeme/Angeme-Bold.ttf",
    // "takezo/TakezoRegular.otf",
    // "takezo/TakezoTilt.otf",
    // "takezo/TakezoCondensed.otf",
    // "heroika/HeroikanamikusRegular.otf",
    // "freesans/FreeSans.ttf"
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.sample_count = VK_SAMPLE_COUNT_8_BIT;
    return S;
  }

  bool setup() final {
    wm_->set_title("12 - Fonte Farandolle");

    /* Setup the ArcBall camera. */
    {
      camera_.makePerspective(
        lina::radians(60.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.1f,
        750.0f
      );
      camera_.set_controller(&arcball_controller_);
      arcball_controller_.set_dolly(55.0f);
    }

    resetFont();

    /* Create Buffers). */
    {
      uniform_buffer_ = context_.createBuffer(
        sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Descriptor set. */
    {
      descriptor_set_layout_ = context_.createDescriptorSetLayout({
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                        | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                        | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        }
      });
      descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = { { uniform_buffer_.buffer } }
        },
      });
    }

    /* Setup the graphics pipeline. */
    {
      auto const& mesh = font_mesh_;

      VkPipelineLayout const pipeline_layout = context_.createPipelineLayout({
        .setLayouts = { descriptor_set_layout_ },
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(shader_interop::PushConstant),
          }
        },
      });

      auto shaders{context_.createShaderModules(COMPILED_SHADERS_DIR, {
        "simple.vert.glsl",
        "simple.frag.glsl",
      })};

      graphics_pipeline_ = context_.createGraphicsPipeline(pipeline_layout, {
        .dynamicStates = {
          VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
        },
        .vertex = {
          .module = shaders[0u].module,
          .buffers = mesh.pipeline_vertex_buffer_descriptors(),
        },
        .fragment = {
          .module = shaders[1u].module,
          .targets = {
            {
              .writeMask = VK_COLOR_COMPONENT_R_BIT
                         | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT
                         | VK_COLOR_COMPONENT_A_BIT
                         ,
            }
          },
        },
        .depthStencil = {
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_TRUE,
          .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        },
        .primitive = {
          .topology = mesh.vk_primitive_topology(),
          // .polygonMode = VK_POLYGON_MODE_LINE,
          // .cullMode = VK_CULL_MODE_NONE,
          .cullMode = VK_CULL_MODE_BACK_BIT,
        }
      });

      context_.releaseShaderModules(shaders);
    }

    return true;
  }

  bool resetFont() {
    std::string fontFilename = kFontSelection[ui_.fontArrayIndex];

    if (!font_.load(fontFilename)) {
      LOGW("Cannot load font \"{}\".", fontFilename);
      return false;
    }
    font_.generateGlyphs(scene::Font::kDefaultCorpus, ui_.fontCurveResolution);

    /* Build a shape mesh. */
    if (auto &mesh = font_mesh_; mesh.generate(font_, ui_.extrusionDepth)) {
      // Bind mesh attributes to shader location.
      mesh.initializeSubmeshDescriptors({
        {
          Geometry::AttributeType::Position,
          shader_interop::kAttribLocation_Position
        },
      });

      context_.deviceWaitIdle();

      context_.destroyResources(
        index_buffer_,
        vertex_buffer_
      );

      vertex_buffer_ = context_.transientCreateBuffer(
        mesh.vertices(),
        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
      );

      if (mesh.index_count() > 0) {
        index_buffer_ = context_.transientCreateBuffer(
          mesh.indices(),
          VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
        );
      }
    } else {
      LOGW("failed to build the font/shape mesh");
      return false;
    }

    std::u16string text16{};
    utf8::utf8to16(
      ui_.sampleText.begin(),
      ui_.sampleText.begin() +
      strlen(ui_.sampleText.data()),
      std::back_inserter(text16)
    );

    text_draw_info_ = font_mesh_.buildTextDrawInfo(text16, ui_.enableKerning);

    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      graphics_pipeline_.layout(),
      graphics_pipeline_,
      index_buffer_,
      vertex_buffer_,
      uniform_buffer_
    );
  }

  void update(float const dt) final {
    renderer_.set_clear_color(lina::to_vec4(ui_.clearColor, 1.0f));

    if (font_updated_) {
      resetFont();
    }

    auto const& C = camera_.transform();
    host_data_.camera = {
      .viewMatrix = C.view,
      .projectionMatrix = C.projection,
    };
    context_.writeBuffer(uniform_buffer_, host_data_);
  }

  void draw(CommandEncoder const& cmd) final {
    auto const scaleMatrix = linalg::scaling_matrix(
      vec3(font_.pixelScaleFromSize(2))
    );
    auto const centerMatrix = linalg::translation_matrix(
      vec3(text_draw_info_.cx, 0, 0)
    );
    auto const worldMatrix = linalg::mul(scaleMatrix, centerMatrix);

    auto pass = cmd.beginRendering();
    {
      pass.setViewportScissor(viewport_size_);

      pass.bindPipeline(graphics_pipeline_);
      {
        pass.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);

        for (size_t i=0; i < text_draw_info_.glyphs.size(); ++i) {
          auto const& glyph_draw_info = text_draw_info_.glyphs[i];
          auto const waveMatrix = ui_.enableAnimation ?
            linalg::translation_matrix(
              vec3(0, 12 * sin(i + 4.2*elapsed_time()), 85 * cos(i + 2.1 * elapsed_time()))
            ) : linalg::identity;
          push_constant_.model.worldMatrix = linalg::mul(
            worldMatrix,
            linalg::mul(glyph_draw_info.matrix, waveMatrix)
          );
          pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
          for (auto const& submesh : glyph_draw_info.submeshes) {
            pass.draw(submesh.draw_descriptor, vertex_buffer_, index_buffer_); //
          }
        }
      }
    }
    cmd.endRendering();

    drawUI(cmd);
  }

  void buildUI() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

      // Font UI.
      ImGui::Separator();

      if (ImGui::TreeNodeEx("Font", ImGuiTreeNodeFlags_DefaultOpen))
      {
        font_updated_ = false;

        font_updated_ |= ImGui::InputText(
          "text",
          ui_.sampleText.data(),
          ui_.sampleText.size()
        );

        /* We do not need to reset the whole font mesh when toggling kerning,
         * only the TextDrawInfo, but we do this here for simplicity. */
        font_updated_ |= ImGui::Checkbox("use kerning", &ui_.enableKerning);

        ImGui::Checkbox("animate", &ui_.enableAnimation);

        const char* combo_preview_value = kFontSelection[ui_.fontArrayIndex];
        if (ImGui::BeginCombo("font", combo_preview_value, 0))
        {
          for (int n = 0; n < (int)kFontSelection.size(); n++)
          {
            const bool is_selected = (ui_.fontArrayIndex == n);

            if (ImGui::Selectable(kFontSelection[n], is_selected)) {
              ui_.fontArrayIndex = n;
              font_updated_ = true;
            }

            if (is_selected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        font_updated_ |= ImGui::SliderFloat(
          "depth", &ui_.extrusionDepth, 0.0, 250.0, "%.1f", 0.1
        );
        font_updated_ |= ImGui::SliderInt(
          "resolution", &ui_.fontCurveResolution, 1, 8
        );


        ImGui::TreePop();
      }
      ImGui::Separator();
      ImGui::ColorEdit3("clear color", (float*)&ui_.clearColor);
    }
    ImGui::End();
  }

 private:
  ArcBallController arcball_controller_{};

  scene::Font font_{};
  scene::FontMesh font_mesh_{};
  scene::FontMesh::TextDrawInfo text_draw_info_{};

  struct {
    std::array<char, 128u> sampleText{
      "C’était à Mégara, faubourg de Carthage, dans les jardins d’Hamilcar"
    };
    int32_t fontArrayIndex{};
    int32_t fontCurveResolution{scene::Polyline::kDefaultCurveResolution};
    float extrusionDepth{};
    bool enableKerning{true};
    bool enableAnimation{true};

    vec3f clearColor{1.0, 0.78f, 0.29f};
  } ui_;

  bool font_updated_{};

  // ----
  shader_interop::UniformData host_data_{};

  backend::Buffer uniform_buffer_{};
  backend::Buffer vertex_buffer_{};
  backend::Buffer index_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};

  Pipeline graphics_pipeline_{};
  // ----
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
