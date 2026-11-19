/* -------------------------------------------------------------------------- */
//
//    00 - Hello VK
//
//    Simple vulkan window creation with clear screen.
//    Where we learn all the way to render a screen.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
  static constexpr bool kUseDynamicRenderingPass = true;

  bool setup() final {
    wm_->set_title("00 - アカシ コンピュータ システム");
    return true;
  }

  void update(float dt) {
    clear_color_ = lina::lerp(
      vec4{0.26f, 0.20f, 0.24f, 1.0f},
      vec4{0.9f, 0.75f, 0.5f, 1.0f},
      cosf(0.75f * elapsed_time()) * 0.5f + 0.5f
    );

    /* Change the default Render Target clear color value. */
    renderer_.set_clear_color(clear_color_);
  }

#if 1

  void drawPass(RenderPassEncoder const& pass) final {
    /**
     * 1. Direct internal renderer method.
     *
     * The renderer will automatically render into its swapchain.
     *
     * We can specify additionnal pass command here if needed but we don't
     * need to declare this method if there is none.
     *
     **/
  }

#else

  /* Alternative with more controls */

  // Redefining this method will bypass the internal renderer.
  void draw(CommandEncoder const& cmd) final {

    if constexpr (kUseDynamicRenderingPass)
    {
      /**
       * 2. Dynamic rendering directly to the swapchain.
       *
       * With no argument specified to 'beginRendering' (accepting both a RTInterface or
       * a RenderPassDescriptor), the command will use the default renderer
       * internal RTInterface.
       *
       * When a RTInterface is used, there is no need to manually transition the
       * image layouts before and after rendering. It will automatically be
       * ready to be drawn after 'beginRendering' and ready to be presented after
       * 'endRendering'.
       **/
      auto pass = cmd.beginRendering();
      {
        // Do anything, here we're calling the internal renderer.
        renderer_.render(pass);
      }
      cmd.endRendering();
    }
    else
    {
      /**
       * 3. Legacy RenderPass method.
       *
       * When a RenderPassDescriptor is passed to 'beginRendering' we need
       * to transition the images layout manually to the correct attachment.
       *
       **/

      // Disable the default renderer internal postprocess to be able to
      // blit directly to the swapchain.
      renderer_.set_swapchain_blit_enabled(false);

      auto const& current_swapchain_image{ renderer_.swapchain_image() };
      cmd.transitionColorImages(
        { current_swapchain_image },
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
      );

      auto pass = cmd.beginRendering({
        .colorAttachments = {
          {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView   = current_swapchain_image.view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {{ lina::as<VkClearColorValue>(clear_color_) }},
          }
        },
        .renderArea = {{0, 0}, renderer_.surface_size()},
      });
      {
        /* Do something. */
        renderer_.render(pass);
      }
      cmd.endRendering();

      /* The image layout must be changed manually before being submitted to
       * the Present queue by 'end_frame'.
       */
      cmd.transitionColorImages(
        { current_swapchain_image },
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
      );
    }
  }
#endif

 private:
  vec4 clear_color_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
