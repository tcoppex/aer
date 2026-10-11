/* -------------------------------------------------------------------------- */
//
//    12 - Font
//
// Where we play with font rendering, ALL KINDS OF !
//
//  Two render modes:
//   - Mesh : triangulated + extruded glyph meshes (scene::FontMesh).
//   - MSDF : one quad per glyph sampling an MTSDF atlas (scene::FontAtlas).
//

/* -------------------------------------------------------------------------- */

#include "utf8/cpp20.h"

#include "aer/application.h"
#include "aer/core/arcball_controller.h"
#include "aer/scene/font.h"
#include "aer/scene/font_mesh.h"
#include "aer/scene/font_atlas.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  enum class RenderMode : int {
    Mesh = 0,
    Msdf = 1,
  };

  static constexpr std::array kRenderModeNames{
    "Mesh",
    "MSDF",
  };

  static constexpr std::array kFontSelection{
    "angeme/Angeme-Regular.ttf",
    "angeme/Angeme-Bold.ttf",
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.sample_count = VK_SAMPLE_COUNT_8_BIT;
    return S;
  }

  /* ---------------------------------------------------------------------- */
  /* Setup / Release                                                        */
  /* ---------------------------------------------------------------------- */

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

    /* The font mesh is needed to describe the mesh pipeline vertex inputs. */
    if (!loadFont() || !rebuildMeshGeometry()) {
      return false;
    }
    dirty_.mesh_geometry = false;
    dirty_.mesh_layout = true;

    /* Buffers. */
    uniform_buffer_ = context_.createBuffer(
      sizeof(host_data_),
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    setupDescriptorSet();
    setupMeshPipeline();
    setupMsdfPipeline();

    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      mesh_pipeline_.layout(),
      mesh_pipeline_,
      msdf_pipeline_.layout(),
      msdf_pipeline_,
      index_buffer_,
      vertex_buffer_,
      uniform_buffer_
    );
    if (atlas_image_.image != VK_NULL_HANDLE) {
      context_.destroyResources(atlas_image_);
    }
  }

  void setupDescriptorSet() {
    /* descriptor layout & set shared by both modes. */
    VkDescriptorBindingFlags const kBindingFlags{
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
      | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
    };

    descriptor_set_layout_ = context_.createDescriptorSetLayout({
      {
        .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = kBindingFlags,
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_Sampler,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = kBindingFlags,
      },
    });

    /* The atlas entry is written the first time the atlas is built. */
    descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
      {
        .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffers = { { uniform_buffer_.buffer } }
      },
    });
  }

  void setupMeshPipeline() {
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

    auto shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "mesh.slang");

    mesh_pipeline_ = context_.createGraphicsPipeline(pipeline_layout, {
      .dynamicStates = {
        VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
      },
      .vertex = {
        .module = shader.module,
        .entryPoint = "vertexMain",
        .buffers = mesh.pipeline_vertex_buffer_descriptors(),
      },
      .fragment = {
        .module = shader.module,
        .entryPoint = "fragmentMain",
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
        .cullMode = VK_CULL_MODE_BACK_BIT,
      }
    });

    context_.releaseShaderModule(shader);
  }

  void setupMsdfPipeline() {
    VkPipelineLayout const pipeline_layout = context_.createPipelineLayout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });

    auto shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "msdf.slang");

    // For antialiased MSDF we use alpha-blending
    // (might require sorting when not rendered on orthogonally to the screen)

    msdf_pipeline_ = context_.createGraphicsPipeline(pipeline_layout, {
      .vertex = {
        .module = shader.module,
        .entryPoint = "vertexMain",
      },
      .fragment = {
        .module = shader.module,
        .entryPoint = "fragmentMain",
        .targets = {
          {
            .writeMask = VK_COLOR_COMPONENT_R_BIT
                       | VK_COLOR_COMPONENT_G_BIT
                       | VK_COLOR_COMPONENT_B_BIT
                       | VK_COLOR_COMPONENT_A_BIT
                       ,
            .blend = {
              .enable = VK_TRUE,
              .color = {
                .operation = VK_BLEND_OP_ADD,
                .srcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              },
              .alpha = {
                .operation = VK_BLEND_OP_ADD,
                .srcFactor = VK_BLEND_FACTOR_ONE,
                .dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              },
            }
          }
        },
      },
      .depthStencil = {
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,  //
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      },
      .primitive = {
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .cullMode = VK_CULL_MODE_NONE,
      }
    });

    context_.releaseShaderModule(shader);
  }

  /* ---------------------------------------------------------------------- */
  /* Font data (shared)                                                     */
  /* ---------------------------------------------------------------------- */

  bool loadFont() {
    std::string const filename = kFontSelection[ui_.fontArrayIndex];
    if (!font_.load(filename)) {
      LOGW("Cannot load font \"{}\".", filename);
      return false;
    }
    font_.generateGlyphs(scene::Font::kDefaultCorpus, ui_.fontCurveResolution);
    return true;
  }

  [[nodiscard]]
  std::u16string currentText16() const {
    std::u16string text16{};
    utf8::utf8to16(
      ui_.sampleText.begin(),
      ui_.sampleText.begin() + strlen(ui_.sampleText.data()),
      std::back_inserter(text16)
    );
    return text16;
  }

  /* ---------------------------------------------------------------------- */
  /* Mesh mode data                                                         */
  /* ---------------------------------------------------------------------- */

  bool rebuildMeshGeometry() {
    auto &mesh = font_mesh_;

    if (!mesh.generate(font_, ui_.extrusionDepth)) {
      LOGW("failed to build the font/shape mesh");
      return false;
    }

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
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );

    if (mesh.index_count() > 0) {
      index_buffer_ = context_.transientCreateBuffer(
        mesh.indices(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT
      );
    }
    return true;
  }

  void rebuildMeshLayout() {
    text_draw_info_ = font_mesh_.buildTextDrawInfo(
      currentText16(), ui_.enableKerning
    );
  }

  /* ---------------------------------------------------------------------- */
  /* MSDF mode data                                                         */
  /* ---------------------------------------------------------------------- */

  [[nodiscard]]
  DescriptorSetWriteEntry atlasDescriptorEntry() const {
    return {
      .binding = shader_interop::kDescriptorSetBinding_Sampler,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = {
        {
          .sampler = context_.sampler_pool().anyso_clampedge_linear(),
          .imageView = atlas_image_.view,
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }
      }
    };
  }

  /* Generate the MTSDF atlas on CPU, upload it and rebind it. */
  bool rebuildAtlas() {
    scene::FontAtlas::Params params{};
    params.pixelsPerEm = ui_.atlasPixelsPerEm;
    params.pxRange = 4.0f;

    if (!atlas_.generate(font_, scene::Font::kDefaultCorpus, params)) {
      LOGW("MTSDF atlas generation failed");
      return false;
    }

    /* The old image may still be in use by in-flight frames. */
    context_.deviceWaitIdle();
    if (atlas_image_.image != VK_NULL_HANDLE) {
      context_.destroyResources(atlas_image_);
    }

    // Linear data (distances): UNORM, not SRGB. Single mip level.
    atlas_image_ = context_.createImage2D(
      atlas_.width(),
      atlas_.height(),
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      "MTSDF atlas"
    );
    context_.transientUploadImage(
      atlas_.pixels().data(),
      atlas_.pixels().size(),
      atlas_image_,
      VkExtent3D{atlas_.width(), atlas_.height(), 1u}
    );

    context_.updateDescriptorSet(descriptor_set_, { atlasDescriptorEntry() });
    return true;
  }

  void rebuildMsdfLayout() {
    msdf_layout_ = atlas_.layout(font_, currentText16(), ui_.enableKerning);
  }

  /* ---------------------------------------------------------------------- */
  /* Frame                                                                  */
  /* ---------------------------------------------------------------------- */

  void update(float const dt) final {
    renderer_.set_clear_color(lina::to_vec4(ui_.clearColor, 1.0f));

    updateResources();

    auto const& C = camera_.transform();
    host_data_.camera = {
      .viewMatrix = C.view,
      .projectionMatrix = C.projection,
    };
    host_data_.fillColor = vec4(ui_.fillColor, 1.0f);
    host_data_.outlineColor = vec4(ui_.outlineColor, 1.0f);
    host_data_.atlasSize = vec2(
      static_cast<float>(atlas_.width()),
      static_cast<float>(atlas_.height())
    );
    host_data_.pxRange = atlas_.pxRange();
    host_data_.outlineWidth = ui_.outlineWidth;
    context_.writeBuffer(uniform_buffer_, host_data_);
  }

  void updateResources() {
    if (dirty_.font_reload) {
      dirty_.font_reload = false;
      loadFont();
      dirty_.mesh_geometry = true;
      dirty_.mesh_layout = true;
      dirty_.msdf_layout = true;
    }

    if (ui_.mode == RenderMode::Mesh){
      if (dirty_.mesh_geometry) {
        dirty_.mesh_geometry = false;
        rebuildMeshGeometry();
        dirty_.mesh_layout = true;
      }
      if (dirty_.mesh_layout) {
        dirty_.mesh_layout = false;
        rebuildMeshLayout();
      }
    } else {
      if (dirty_.atlas) {
        dirty_.atlas = false;
        rebuildAtlas();
        dirty_.msdf_layout = true;
      }
      if (dirty_.msdf_layout) {
        dirty_.msdf_layout = false;
        rebuildMsdfLayout();
      }
    }
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      pass.setViewportScissor(viewport_size_);

      if (ui_.mode == RenderMode::Mesh) {
        drawMesh(pass);
      } else {
        drawMsdf(pass);
      }
    }
    cmd.endRendering();

    drawUI(cmd);
  }

  void drawMesh(RenderPassEncoder const& pass) {
    auto const scaleMatrix = lina::scaling_matrix(
      vec3(font_.pixelScaleFromSize(2))
    );
    auto const centerMatrix = lina::translation_matrix(
      vec3(text_draw_info_.cx, 0, 0)
    );
    auto const worldMatrix = lina::mul(scaleMatrix, centerMatrix);

    pass.bindPipeline(mesh_pipeline_);
    pass.bindDescriptorSet(
      descriptor_set_,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    );

    push_constant_.elapsedTime = elapsed_time();
    push_constant_.animate = ui_.enableAnimation;

    for (size_t i = 0; i < text_draw_info_.glyphs.size(); ++i) {
      auto const& glyph_draw_info = text_draw_info_.glyphs[i];
      push_constant_.instanceID = static_cast<uint32_t>(i);
      push_constant_.worldMatrix = lina::mul(
        worldMatrix,
        glyph_draw_info.matrix
      );
      pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
      for (auto const& submesh : glyph_draw_info.submeshes) {
        pass.bindAndDraw(submesh.draw_descriptor, vertex_buffer_, index_buffer_);
      }
    }
  }

  void drawMsdf(RenderPassEncoder const& pass) {
    /* Same on-screen size as the mesh mode: the mesh works in font units
     * scaled by pixelScaleFromSize(2), the atlas in em units. */
    float const em_to_world =
      font_.pixelScaleFromSize(2) / static_cast<float>(font_.emScale());

    auto const scaleMatrix = lina::scaling_matrix(vec3(em_to_world));
    auto const centerMatrix = lina::translation_matrix(
      vec3(-0.5f * msdf_layout_.width, 0.0f, 0.0f)
    );
    auto const worldMatrix = lina::mul(scaleMatrix, centerMatrix);

    pass.bindPipeline(msdf_pipeline_);
    pass.bindDescriptorSet(
      descriptor_set_,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    );

    push_constant_.worldMatrix = worldMatrix;
    push_constant_.elapsedTime = elapsed_time();
    push_constant_.animate = ui_.enableAnimation;

    // [might be improved by using instancing with a glyphes SSBO]
    for (size_t i = 0; i < msdf_layout_.quads.size(); ++i) {
      auto const& q = msdf_layout_.quads[i];
      push_constant_.instanceID = static_cast<uint32_t>(i);
      push_constant_.rect = vec4(
        q.position.x, q.position.y, q.size.x, q.size.y
      );
      push_constant_.uv = q.texcoord;
      pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
      pass.draw(4u);  // triangle strip quad
    }
  }

  /* ---------------------------------------------------------------------- */
  /* UI                                                                     */
  /* ---------------------------------------------------------------------- */

  void buildUI() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

      ImGui::Separator();

      if (ImGui::TreeNodeEx("Font", ImGuiTreeNodeFlags_DefaultOpen))
      {
        ImGui::TextUnformatted("mode");
        int mode = static_cast<int>(ui_.mode);
        for (int i = 0; i < (int)kRenderModeNames.size(); ++i) {
          ImGui::SameLine();
          ImGui::RadioButton(kRenderModeNames[i], &mode, i);
        }
        ui_.mode = static_cast<RenderMode>(mode);

        if (ImGui::InputText(
              "text", ui_.sampleText.data(), ui_.sampleText.size())) {
          dirty_.mesh_layout = true;
          dirty_.msdf_layout = true;
        }

        if (ImGui::Checkbox("use kerning", &ui_.enableKerning)) {
          dirty_.mesh_layout = true;
          dirty_.msdf_layout = true;
        }

        ImGui::Checkbox("animate", &ui_.enableAnimation);

        const char* combo_preview_value = kFontSelection[ui_.fontArrayIndex];
        if (ImGui::BeginCombo("font", combo_preview_value, 0))
        {
          for (int n = 0; n < (int)kFontSelection.size(); n++)
          {
            const bool is_selected = (ui_.fontArrayIndex == n);

            if (ImGui::Selectable(kFontSelection[n], is_selected)) {
              ui_.fontArrayIndex = n;
              dirty_.font_reload = true;
              dirty_.atlas = true;
            }

            if (is_selected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        ImGui::Separator();

        if (ui_.mode == RenderMode::Mesh)
        {
          if (ImGui::SliderFloat("depth", &ui_.extrusionDepth, 0.0, 250.0, "%.1f", 0.1)) {
            dirty_.mesh_geometry = true;
          }
          if (ImGui::SliderInt("resolution", &ui_.fontCurveResolution, 1, 8)) {
            dirty_.font_reload = true;
          }
        }
        else
        {
          if (ImGui::SliderFloat("atlas px/em", &ui_.atlasPixelsPerEm, 16.0f, 96.0f, "%.0f")) {
            dirty_.atlas = true;
          }
          ImGui::SliderFloat("outline", &ui_.outlineWidth, 0.0f, 0.4f);
          ImGui::ColorEdit3("fill", (float*)&ui_.fillColor);
          ImGui::ColorEdit3("outline color", (float*)&ui_.outlineColor);
        }

        ImGui::TreePop();
      }
      ImGui::Separator();
      ImGui::ColorEdit3("clear color", (float*)&ui_.clearColor);
    }
    ImGui::End();
  }

 private:
  ArcBallController arcball_controller_{};

  // ----

  /* Shared. */
  scene::Font font_{};

  /* Mesh mode. */
  scene::FontMesh font_mesh_{};
  scene::FontMesh::TextDrawInfo text_draw_info_{};

  /* MSDF mode. */
  scene::FontAtlas atlas_{};
  scene::FontAtlas::TextLayout msdf_layout_{};

  // ----

  struct {
    RenderMode mode{RenderMode::Mesh};

    // Shared.
    std::array<char, 128u> sampleText{
      "C'était à Mégara, faubourg de Carthage, dans les jardins d'Hamilcar."
    };
    int32_t fontArrayIndex{};
    bool enableKerning{true};
    bool enableAnimation{true};
    vec3f fillColor{0.76f, 0.26f, 0.36f};
    vec3f clearColor{1.0, 0.78f, 0.29f};

    // Mesh.
    int32_t fontCurveResolution{scene::Polyline::kDefaultCurveResolution};
    float extrusionDepth{};

    // MSDF.
    float atlasPixelsPerEm{48.0f};
    float outlineWidth{0.12f};
    vec3f outlineColor{1.0f, 1.0f, 1.0f};
  } ui_;

  struct {
    bool font_reload{};     // reload the font file + generate glyph paths
    bool mesh_geometry{};   // rebuild the FontMesh + its device buffers
    bool mesh_layout{true}; // rebuild the mesh TextDrawInfo
    bool atlas{true};       // regenerate the MTSDF atlas + image
    bool msdf_layout{true}; // rebuild the quad layout
  } dirty_;

  // ----

  shader_interop::UniformData host_data_{};

  backend::Buffer uniform_buffer_{};
  backend::Buffer vertex_buffer_{};
  backend::Buffer index_buffer_{};
  backend::Image atlas_image_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};

  shader_interop::PushConstant push_constant_{};

  Pipeline mesh_pipeline_{};
  Pipeline msdf_pipeline_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
