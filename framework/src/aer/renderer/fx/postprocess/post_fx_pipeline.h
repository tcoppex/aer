#ifndef AER_RENDERER_FX_POSTPROCESS_POST_FX_PIPELINE_H_
#define AER_RENDERER_FX_POSTPROCESS_POST_FX_PIPELINE_H_

#include "aer/renderer/fx/postprocess/post_fx_interface.h"
#include "aer/platform/vulkan/context.h"

/* -------------------------------------------------------------------------- */

///
/// Handle post processing pipeline.
///
/// notes:
///   - Might want to switch shared_ptr to unique_ptr with raw ptr sharing.
///
class PostFxPipeline : public PostFxInterface {
 public:
  template<typename T>
  requires DerivedFrom<T, PostFxInterface>
  struct FxDep {
    std::shared_ptr<T> fx{};
    uint32_t index{};
  };

  struct PostFxDependencies {
    std::vector<FxDep<PostFxInterface>> images{};
    std::vector<FxDep<PostFxInterface>> buffers{};
  };

 public:
  virtual void reset() {
    effects_.clear();
    dependencies_.clear();
  }

  template<typename T>
  requires DerivedFrom<T, PostFxInterface>
  std::shared_ptr<T> add(PostFxDependencies const& dependencies) {
    auto fx = std::make_shared<T>();
    effects_.push_back(fx);
    dependencies_.push_back(dependencies);
    return fx;
  }

  template<typename T>
  requires DerivedFrom<T, PostFxInterface>
  std::shared_ptr<T> add() {
    return add<T>({});
  }

  template<typename T>
  requires DerivedFrom<T, PostFxInterface>
  std::shared_ptr<T> get(uint32_t index) {
    return std::static_pointer_cast<T>(effects_.at(index));
  }

  virtual void setupDependencies();

 public:
  void init(RenderContext const& context) override;

  void setup(VkExtent2D const dimension) override {
    for (auto fx : effects_) {
      fx->setup(dimension);
    }
    setupDependencies();
  }

  bool resize(VkExtent2D const dimension) override {
    bool has_resized = false;
    for (auto fx : effects_) {
      has_resized = fx->resize(dimension);
    }
    return has_resized;
  }

  void release() override {
    for (auto it = effects_.rbegin(); it != effects_.rend(); ++it) {
     (*it)->release();
    }
    reset();
  }

  void execute(CommandEncoder const& cmd) const override {
    for (auto fx : effects_) {
      fx->execute(cmd);
    }
  }

  void set_image_inputs(std::vector<backend::Image> const& inputs) override {
    LOG_CHECK(!effects_.empty());
    effects_.front()->set_image_inputs(inputs);
  }

  void set_buffer_inputs(std::vector<backend::Buffer> const& inputs) override {
    LOG_CHECK(!effects_.empty());
    effects_.front()->set_buffer_inputs(inputs);
  }

  backend::Image image_output(uint32_t index = 0u) const override {
    LOG_CHECK(!effects_.empty());
    return effects_.back()->image_output(index);
  }

  std::vector<backend::Image> image_outputs() const override {
    LOG_CHECK(!effects_.empty());
    return effects_.back()->image_outputs();
  }

  backend::Buffer buffer_output(uint32_t index) const override {
    return effects_.back()->buffer_output(index);
  }

  std::vector<backend::Buffer> buffer_outputs() const override {
    return effects_.back()->buffer_outputs();
  }

  void setupUI() override {
    for (auto fx : effects_) {
      fx->setupUI();
    }
  }

 protected:
  static
  PostFxDependencies GetOutputDependencies(std::shared_ptr<PostFxPipeline> fx) {
    auto dep = fx->default_output_dependencies();
    for (auto& img : dep.images) {
      img.fx = fx;
    }
    for (auto& buf : dep.buffers) {
      buf.fx = fx;
    }
    return dep;
  }

  // Format of the pipeline output, usually just an image.
  virtual PostFxDependencies default_output_dependencies() const {
    return { .images = { {.index = 0u} } };
  }

 protected:
  Context const* context_ptr_{};
  std::vector<std::shared_ptr<PostFxInterface>> effects_{};
  std::vector<PostFxDependencies> dependencies_{};
};

// ----------------------------------------------------------------------------

/**
 * A Templated fx pipeline where the provided parameter is the entry point
 * effect, automatically added to the pipeline.
 */
template<typename E>
class TPostFxPipeline : public PostFxPipeline {
 public:
  TPostFxPipeline()
    : PostFxPipeline()
  {
    reset();
  }

  void reset() override {
    PostFxPipeline::reset();
    add<E>();
    set_entry_dependencies(entry_dependencies_);
  }

  virtual std::shared_ptr<E> entry_fx() {
    return get<E>(0u);
  }

  void set_entry_dependencies(PostFxDependencies const& dependencies) {
    dependencies_[0u] = dependencies;
    entry_dependencies_ = dependencies;
  }

  template<typename T>
  requires DerivedFrom<T, PostFxInterface>
  std::shared_ptr<T> add(PostFxDependencies const& dependencies = {}) {
    auto fx = PostFxPipeline::add<T>(dependencies);
    if constexpr (DerivedFrom<T, PostFxPipeline>) {
      fx->set_entry_dependencies(dependencies);
    }
    return fx;
  }

  template<typename T>
  requires DerivedFrom<T, PostFxInterface>
  std::shared_ptr<T> add(std::shared_ptr<PostFxPipeline> pipeline) {
    return add<T>(GetOutputDependencies(pipeline));
  }

 public:
  PostFxDependencies entry_dependencies_{};
};

// ----------------------------------------------------------------------------

// Blank Fx used to pass data to a specialized pipeline
class PassDataNoFx final : public PostFxInterface {
 public:
  void init(RenderContext const& context) final {}

  void setup(VkExtent2D const dimension) final {}

  bool resize(VkExtent2D const dimension) final { return false; }

  void execute(CommandEncoder const& cmd) const final {}

  void setupUI() final {}

  void release() final {
    images_.clear();
    buffers_.clear();
  }

  void set_image_inputs(std::vector<backend::Image> const& inputs) final {
    images_ = inputs;
  }

  void set_buffer_inputs(std::vector<backend::Buffer> const& inputs) final {
    buffers_ = inputs;
  }

  backend::Image image_output(uint32_t index = 0u) const override {
    LOG_CHECK(index < images_.size());
    return images_[index];
  }

  std::vector<backend::Image> /*const&*/ image_outputs() const override {
    return images_;
  }

  backend::Buffer buffer_output(uint32_t index = 0u) const override {
    LOG_CHECK(index < buffers_.size());
    return buffers_[index];
  }

  std::vector<backend::Buffer> /*const&*/ buffer_outputs() const final {
    return buffers_;
  }

 private:
  std::vector<backend::Image> images_{};
  std::vector<backend::Buffer> buffers_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_POST_FX_PIPELINE_H_