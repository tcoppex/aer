/* -------------------------------------------------------------------------- */
//
//    Audio Vizualizer
//
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

#include <jni.h>
#include <mutex>
#include <algorithm>

namespace shader_interop {
#include "../shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

static std::mutex g_fft_mutex;
static std::vector<float> g_spectrum;
static int32_t g_spectrum_rate_hz = 48000;

// ----------------------------------------------------------------------------

extern "C" {

JNIEXPORT void JNICALL
Java_cc_polysfaer_vkframework_samples_audio_1visualizer_MainActivity_nativeUpdateAudioSpectrum(
  JNIEnv* env,
  jobject thiz,
  jfloatArray magnitudes,
  jint sample_rate_hz
) {
  if (magnitudes == nullptr) {
    return;
  }

  jsize const length = env->GetArrayLength(magnitudes);
  std::vector<float> copy(static_cast<size_t>(length));

  env->GetFloatArrayRegion(magnitudes, 0, length, copy.data());

  std::lock_guard<std::mutex> lock(g_fft_mutex);
  g_spectrum.swap(copy);

  if (sample_rate_hz > 0) {
    g_spectrum_rate_hz = sample_rate_hz;
  }
}

}

// ----------------------------------------------------------------------------

class AudioVisualizerSample final : public Application {
 private:
  // Per-band level in dB, mapped to [0..1]
  struct BandLevel {
    float level = 0.0f;
    float peak_db = -60.0f;
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    return S;
  }

  bool setup() final {
    renderer_.set_clear_color(vec4(0.02f, 0.01f, 0.06f, 1.0f));
    renderer_.set_foveated_rendering_enabled(false);

    /* Create buffers. */
    {
      uniform_buffer_ = context_.createBuffer(
        2u * sizeof(shader_interop::UniformCameraData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Create the descriptor set with its binding. */
    {
      uint32_t const kDescSetUniformBinding = 0u;

      descriptor_set_layout_ = context_.createDescriptorSetLayout(
        {
          {
            .binding = kDescSetUniformBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .bindingFlags =
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
              | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
              | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
          }
        },
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
      );

      descriptor_set_ = context_.createDescriptorSet(
        descriptor_set_layout_,
        {{
          .binding = kDescSetUniformBinding,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = {{ uniform_buffer_.buffer }}
        }}
      );
    }

    auto const shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "main.slang");

    pipeline_layout_ = context_.createPipelineLayout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_FRAGMENT_BIT
                      ,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });

    graphics_pipeline_ = context_.createGraphicsPipeline(
      pipeline_layout_,
      {
        .vertex = {
          .module = shader.handle,
          .entryPoint = "vertexMain",
        },
        .fragment = {
          .module = shader.handle,
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
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        }
      }
    );

    context_.releaseShaderModule(shader);

    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      pipeline_layout_,
      graphics_pipeline_,
      uniform_buffer_
    );
  }

  void update(float dt) final {
    auto const& frame = xr_->frame_data();

    // Update uniform buffer for both eyes.
    std::vector<shader_interop::UniformCameraData> camera_data{
      {
        .projectionMatrix = frame.projMatrices[0],
        .viewMatrix = frame.viewMatrices[0]
      },
      {
        .projectionMatrix = frame.projMatrices[1],
        .viewMatrix = frame.viewMatrices[1]
      },
    };

    context_.writeBuffer(uniform_buffer_, camera_data);

    updateAudioBands(dt);

    push_constant_.time = frame_time();
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      pass.bindPipeline(graphics_pipeline_);
      pass.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
      pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
      pass.draw(3);
    }
    cmd.endRendering();
  }

 private:
  void updateAudioBands(float const dt) {
    // Retrieve concurrent data fetch by the kotlin thread.
    std::vector<float> spectrum;
    int32_t rate_hz = 0;
    {
      std::lock_guard<std::mutex> lock(g_fft_mutex);
      spectrum = g_spectrum;
      rate_hz = g_spectrum_rate_hz;
    }
    size_t const bin_count = spectrum.size();

    if (bin_count > 8u) {
      // frequency width (in Hz)
      float const bin_hz = static_cast<float>(rate_hz) / (2.0f * bin_count);

      // Convert target frequency thresholds (Hz) into array indices
      size_t const bass_hi  = hzToBinIndex(250.0f, bin_hz, bin_count);
      size_t const mids_hi  = hzToBinIndex(4000.0f, bin_hz, bin_count);
      size_t const highs_hi = hzToBinIndex(16000.0f, bin_hz, bin_count);

      // Calculate the mean amplitude for each frequency band.
      float const raw_bass  = calculateSpectrumBandMean(spectrum, 1u, bass_hi);
      float const raw_mids  = calculateSpectrumBandMean(spectrum, bass_hi + 1u, mids_hi);
      float const raw_highs = calculateSpectrumBandMean(spectrum, mids_hi + 1u, highs_hi);

      // Convert for audio reactivity
      bass_  = updateBandEnvelope(bass_db_, raw_bass, dt);
      mids_  = updateBandEnvelope(mids_db_, raw_mids, dt);
      highs_ = updateBandEnvelope(highs_db_, raw_highs, dt);

      push_constant_.audioAmplitude = bass_;
      push_constant_.audioMids = mids_;
      push_constant_.audioHighs = highs_;
    }
  }

  [[nodiscard]]
  float calculateSpectrumBandMean(
    std::vector<float> const& mags,
    size_t first_bin,
    size_t last_bin
  ) {
    first_bin = std::max(size_t(1u), first_bin);
    last_bin = std::min(last_bin, mags.size() - 1u);

    if (first_bin > last_bin) {
      return 0.0f;
    }

    float sum = 0.0f;
    for (size_t bin = first_bin; bin <= last_bin; ++bin) {
      sum += mags[bin];
    }

    return sum / static_cast<float>(last_bin - first_bin + 1u);
  }

  [[nodiscard]]
  float updateBandEnvelope(BandLevel& s, float mag, float dt) {
    constexpr float kRangeDb      = 24.0f;      // Dynamic range shown below the peak.
    constexpr float kPeakDecayDb  = 3.0f;       // Peak falls by this many dB/second.
    constexpr float kMinPeakDb    = -60.0f;     // Do not amplify near-silence.

    float const db = 20.0f * std::log10(mag + 1e-9f);

    s.peak_db = std::max(db, s.peak_db - kPeakDecayDb * dt);
    s.peak_db = std::max(s.peak_db, kMinPeakDb);

    float const target = std::clamp((db - (s.peak_db - kRangeDb)) / kRangeDb, 0.0f, 1.0f);
    float const rate = target > s.level ? 30.0f : 6.0f;
    s.level += (target - s.level) * (1.0f - std::exp(-dt * rate));

    return s.level;
  }

  [[nodiscard]]
  size_t hzToBinIndex(float hz, float bin_hz, size_t bin_count) {
    auto const bin = static_cast<size_t>(std::lround(hz / bin_hz));
    return std::clamp(bin, size_t(1u), bin_count - 1u);
  }

 private:
  backend::Buffer uniform_buffer_{};
  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};

  VkPipelineLayout pipeline_layout_{};
  Pipeline graphics_pipeline_{};
  shader_interop::PushConstant push_constant_{};

  // ----------

  BandLevel bass_db_{};
  BandLevel mids_db_{};
  BandLevel highs_db_{};

  float bass_ = 0.0f;
  float mids_ = 0.0f;
  float highs_ = 0.0f;
};

// ----------------------------------------------------------------------------

ENTRY_POINT(AudioVisualizerSample)

/* -------------------------------------------------------------------------- */
