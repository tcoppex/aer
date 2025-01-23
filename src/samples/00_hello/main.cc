/* -------------------------------------------------------------------------- */
//
//    00 - Hello VK
//
//    Simple vulkan window creation with clear screen.
//
/* -------------------------------------------------------------------------- */

#include "framework/application.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    glfwSetWindowTitle(window_, "00 - アカシ コンピュータ システム");
    return true;
  }

  void frame() final {
    auto cmd = renderer_.begin_frame();

#if 1 /* Direct method */

    /* Change the default Render Target clear color value. */
    renderer_.set_color_clear_value({.float32 = {0.9f, 0.75f, 0.5f, 1.0f}});

    /**
     * Dynamic rendering directly to the swapchain.
     *
     * With no argument specified to 'begin_rendering' (aceppting both a RTInterface or
     * a RenderPassDescriptor_t), the command will draw directly to the swapchain.
     *
     * This is similar than specifying 'renderer_' as the RTInterface parameter.
     *
     * When a RTInterface is used, there is no need to manually transition the
     * image layouts before and after rendering. It will automatically be
     * ready to be drawn after 'begin_rendering' and ready to be presented after
     * 'end_rendering'.
     **/
    auto pass = cmd.begin_rendering(/*renderer_*/);
    {
      /* Do something. */
    }
    cmd.end_rendering();

#else /* Alternative with more controls */

    auto const& current_swapchain_image{ renderer_.get_color_attachment().image };

    /**
     * When a RenderPassDescriptor_t is passed to 'begin_rendering' we need
     * to transition the images layout manually to the correct attachment.
     **/
    cmd.transition_images_layout(
      {{.image = current_swapchain_image}},
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    );

    cmd.begin_rendering({
      .colorAttachments = {
        {
          .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
          .imageView   = renderer_.get_color_attachment().view,
          .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
          .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
          .clearValue  = {{{0.25f, 0.75f, 0.5f, 1.0f}}},
        }
      },
      .depthStencilAttachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = renderer_.get_depth_stencil_attachment().view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {{{1.0f, 0u}}},
      },
      .renderArea = {{0, 0}, viewport_size_},
    });
    {
      /* Do something. */
    }
    cmd.end_rendering();

    /* The image layout must be changed manually before being submitted to
     * the Present queue by 'end_frame'.
     */
    cmd.transition_images_layout(
      {{.image = current_swapchain_image}},
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

#endif

    renderer_.end_frame();
  }
};

// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  return SampleApp().run();
}

/* -------------------------------------------------------------------------- */
