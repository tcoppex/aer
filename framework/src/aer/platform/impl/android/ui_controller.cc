/* -------------------------------------------------------------------------- */

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuseless-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#else
#pragma warning(push)
#endif

#define IMGUI_WRAPPER_IMPL
#include "aer/platform/ui_controller.h" //

#include <backends/imgui_impl_android.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui_internal.h>

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma warning(pop)
#endif

#include "aer/renderer/renderer.h"
#include "aer/platform/backend/context.h"

/* -------------------------------------------------------------------------- */

bool UIController::init(Renderer const& renderer, WMInterface const& wm) {
  // IMGUI_CHECKVERSION();

  // wm_ptr_ = &wm;

  // ImGui::CreateContext();
  // ImGui::StyleColorsDark();

  // if (!ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(wm.handle()), true)) {
  //   return false;
  // }

  // auto const& context = renderer.context();

  // VkDescriptorPoolSize const pool_sizes[]{
  //   { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512u },
  // };
  // VkDescriptorPoolCreateInfo const desc_pool_info{
  //   .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
  //   .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
  //          | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
  //          ,
  //   .maxSets = 1024u, //
  //   .poolSizeCount = 1u,
  //   .pPoolSizes = pool_sizes,
  // };
  // CHECK_VK(vkCreateDescriptorPool(context.device(), &desc_pool_info, nullptr, &imgui_descriptor_pool_));

  // VkFormat const formats[]{
  //   renderer.color_attachment().format
  // };
  // VkFormat const depth_stencil_format{
  //   renderer.depth_stencil_attachment().format
  // };
  // auto const main_queue{ context.queue() };
  // ImGui_ImplVulkan_InitInfo info{
  //   .Instance = context.instance(),
  //   .PhysicalDevice = context.physical_device(),
  //   .Device = context.device(),
  //   .QueueFamily = main_queue.family_index,
  //   .Queue = main_queue.queue,
  //   .DescriptorPool = imgui_descriptor_pool_,
  //   .MinImageCount = 2,
  //   .ImageCount = renderer.swapchain().image_count(),
  //   .UseDynamicRendering = true,
  //   .PipelineRenderingCreateInfo = {
  //     .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
  //     .colorAttachmentCount = 1u,
  //     .pColorAttachmentFormats = formats,
  //     .depthAttachmentFormat = depth_stencil_format,
  //     .stencilAttachmentFormat = depth_stencil_format,
  //   },
  // };
  // if (!ImGui_ImplVulkan_Init(&info)) {
  //   return false;
  // }

  // ImGui::GetIO().ConfigFlags = ImGuiConfigFlags_DockingEnable;

  return true;
}

// ----------------------------------------------------------------------------

void UIController::release(Context const& context) {
  // ImGui_ImplVulkan_Shutdown();
  // ImGui_ImplGlfw_Shutdown();
  // ImGui::DestroyContext();
  // vkDestroyDescriptorPool(context.device(), imgui_descriptor_pool_, nullptr);
}

// ----------------------------------------------------------------------------

void UIController::beginFrame() {
  // ImGui_ImplVulkan_NewFrame();
  // ImGui_ImplGlfw_NewFrame();
  // ImGui::NewFrame();
  // setupStyles();

  // // [todo] OnViewportSizeChange
  // {
  //   float xscale, yscale;
  //   glfwGetWindowContentScale(reinterpret_cast<GLFWwindow*>(wm_ptr_->handle()), &xscale, &yscale);
  //   ImGui::GetIO().FontGlobalScale = xscale;
  // }
}

// ----------------------------------------------------------------------------

void UIController::endFrame() {
  // ImGui::Render();

  // if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
  //   ImGui::UpdatePlatformWindows();
  //   ImGui::RenderPlatformWindowsDefault();
  // }
  // ImGui::EndFrame();
}

// ----------------------------------------------------------------------------

void UIController::draw(
  CommandEncoder const& cmd,
  VkImageView image_view,
  VkExtent2D surface_size
) {
  // TODO
}

// ----------------------------------------------------------------------------

void UIController::setupStyles() {
  // typedef ImVec4 (*srgbFunction)(float, float, float, float);
  // srgbFunction   passthrough = [](float r, float g, float b, float a) -> ImVec4 { return ImVec4(r, g, b, a); };
  // srgbFunction   toLinear    = [](float r, float g, float b, float a) -> ImVec4 {
  //   auto toLinearScalar = [](float u) -> float {
  //     return u <= 0.04045 ? 25 * u / 323.f : powf((200 * u + 11) / 211.f, 2.4f);
  //   };
  //   return ImVec4(toLinearScalar(r), toLinearScalar(g), toLinearScalar(b), a);
  // };
  // srgbFunction srgb = true ? toLinear : passthrough;

  // ImGui::StyleColorsDark();

  // ImGuiStyle& style                  = ImGui::GetStyle();
  // style.WindowRounding               = 0.0f;
  // style.WindowBorderSize             = 0.0f;
  // style.ColorButtonPosition          = ImGuiDir_Right;
  // style.FrameRounding                = 2.0f;
  // style.FrameBorderSize              = 1.0f;
  // style.GrabRounding                 = 4.0f;
  // style.IndentSpacing                = 12.0f;
  // style.Colors[ImGuiCol_WindowBg]    = srgb(0.2f, 0.2f, 0.2f, 0.33f);
  // style.Colors[ImGuiCol_MenuBarBg]   = srgb(0.2f, 0.2f, 0.2f, 0.75f);
  // style.Colors[ImGuiCol_ScrollbarBg] = srgb(0.2f, 0.2f, 0.2f, 1.0f);
  // style.Colors[ImGuiCol_PopupBg]     = srgb(0.14f, 0.14f, 0.14f, 1.0f);
  // style.Colors[ImGuiCol_Border]      = srgb(0.4f, 0.4f, 0.4f, 0.5f);
  // style.Colors[ImGuiCol_FrameBg]     = srgb(0.05f, 0.05f, 0.05f, 0.5f);

  // // Normal
  // ImVec4 normal_color = srgb(0.47, 0.47, 0.53f, 1.0f);
  // std::vector<ImGuiCol> to_change_nrm;
  // to_change_nrm.push_back(ImGuiCol_Header);
  // to_change_nrm.push_back(ImGuiCol_SliderGrab);
  // to_change_nrm.push_back(ImGuiCol_Button);
  // to_change_nrm.push_back(ImGuiCol_CheckMark);
  // to_change_nrm.push_back(ImGuiCol_ResizeGrip);
  // to_change_nrm.push_back(ImGuiCol_TextSelectedBg);
  // to_change_nrm.push_back(ImGuiCol_Separator);
  // to_change_nrm.push_back(ImGuiCol_FrameBgActive);
  // for(auto c : to_change_nrm) {
  //   style.Colors[c] = normal_color;
  // }

  // // Active
  // ImVec4 active_color = srgb(0.365f, 0.365f, 0.425f, 1.0f);
  // std::vector<ImGuiCol> to_change_act;
  // to_change_act.push_back(ImGuiCol_HeaderActive);
  // to_change_act.push_back(ImGuiCol_SliderGrabActive);
  // to_change_act.push_back(ImGuiCol_ButtonActive);
  // to_change_act.push_back(ImGuiCol_ResizeGripActive);
  // to_change_act.push_back(ImGuiCol_SeparatorActive);
  // for(auto c : to_change_act) {
  //   style.Colors[c] = active_color;
  // }

  // // Hovered
  // ImVec4 hovered_color = srgb(0.565f, 0.565f, 0.625f, 1.0f);
  // std::vector<ImGuiCol> to_change_hover;
  // to_change_hover.push_back(ImGuiCol_HeaderHovered);
  // to_change_hover.push_back(ImGuiCol_ButtonHovered);
  // to_change_hover.push_back(ImGuiCol_FrameBgHovered);
  // to_change_hover.push_back(ImGuiCol_ResizeGripHovered);
  // to_change_hover.push_back(ImGuiCol_SeparatorHovered);
  // for(auto c : to_change_hover) {
  //   style.Colors[c] = hovered_color;
  // }

  // style.Colors[ImGuiCol_TitleBgActive]    = srgb(0.465f, 0.465f, 0.465f, 1.0f);
  // style.Colors[ImGuiCol_TitleBg]          = srgb(0.125f, 0.125f, 0.125f, 1.0f);
  // style.Colors[ImGuiCol_Tab]              = srgb(0.05f, 0.05f, 0.05f, 0.5f);
  // style.Colors[ImGuiCol_TabHovered]       = srgb(0.465f, 0.495f, 0.525f, 1.0f);
  // style.Colors[ImGuiCol_TabActive]        = srgb(0.282f, 0.290f, 0.302f, 1.0f);
  // style.Colors[ImGuiCol_ModalWindowDimBg] = srgb(0.465f, 0.465f, 0.465f, 0.350f);

  // ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

  // // --------------

  // const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode
  //                                    | ImGuiDockNodeFlags_NoDockingInCentralNode
  //                                    ;
  // ImGuiID dockID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

  // if(!ImGui::DockBuilderGetNode(dockID)->IsSplitNode() && !ImGui::FindWindowByName("Viewport")) {
  //   ImGui::DockBuilderDockWindow("Viewport", dockID);
  //   ImGui::DockBuilderGetCentralNode(dockID)->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
  //   ImGuiID leftID = ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Right, 0.2f, nullptr, &dockID);
  //   ImGui::DockBuilderDockWindow("Settings", leftID);
  // }

  // // if(ImGui::BeginMainMenuBar()) {
  // //   if(ImGui::BeginMenu("File")) {
  // //     // if(ImGui::MenuItem("vSync", "", &m_vSync))
  // //     //   m_swapchain.needToRebuild();  // Recreate the swapchain with the new vSync setting

  // //     ImGui::Separator();

  // //     // if(ImGui::MenuItem("Exit"))
  // //     //   glfwSetWindowShouldClose(m_window, true);

  // //     ImGui::EndMenu();
  // //   }
  // //   ImGui::EndMainMenuBar();
  // // }

  // // // We define "viewport" with no padding an retrieve the rendering area
  // // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  // // ImGui::Begin("Viewport");
  // // ImGui::End();
  // // ImGui::PopStyleVar();

  // // --------------

  // // [Docker code originally from NvPro samples]

  // // ImGuiID Panel::dockspaceID{0};

  // // Keeping the unique ID of the dock space
  // ImGuiID dockspaceID = ImGui::GetID("DockSpace");

  // // The dock need a dummy window covering the entire viewport.
  // ImGuiViewport* viewport = ImGui::GetMainViewport();
  // ImGui::SetNextWindowPos(viewport->WorkPos);
  // ImGui::SetNextWindowSize(viewport->WorkSize);
  // ImGui::SetNextWindowViewport(viewport->ID);

  // // All flags to dummy window
  // ImGuiWindowFlags host_window_flags{};
  // host_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
  // host_window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
  // host_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  // host_window_flags |= ImGuiWindowFlags_NoBackground;

  // // Starting dummy window
  // char label[32];
  // ImFormatString(label, IM_ARRAYSIZE(label), "DockSpaceViewport_%08X", viewport->ID);
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  // ImGui::Begin(label, nullptr, host_window_flags);
  // ImGui::PopStyleVar(3);

  // // The central node is transparent, so that when UI is draw after, the image is visible
  // // Auto Hide Bar, no title of the panel
  // // Center is not dockable, that is for the scene
  // ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode
  //                                   | ImGuiDockNodeFlags_AutoHideTabBar
  //                                   | ImGuiDockNodeFlags_NoDockingOverCentralNode
  //                                   ;

  // // Building the splitting of the dock space is done only once
  // if (!ImGui::DockBuilderGetNode(dockspaceID))
  // {
  //   ImGui::DockBuilderRemoveNode(dockspaceID);
  //   ImGui::DockBuilderAddNode(dockspaceID, dockspaceFlags | ImGuiDockNodeFlags_DockSpace);
  //   ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->Size);

  //   ImGuiID dock_main_id = dockspaceID;

  //   // Slitting all 4 directions, targetting (320 pixel * DPI) panel width, (180 pixel * DPI) panel height.
  //   const float xRatio = 0.5f; // clamp<float>(320.0f * getDPIScale() / viewport->WorkSize[0], 0.01f, 0.499f);
  //   const float yRatio = 0.5f; // clamp<float>(180.0f * getDPIScale() / viewport->WorkSize[1], 0.01f, 0.499f);

  //   // Note, for right, down panels, we use the n / (1 - n) formula to correctly split the space remaining from the left, up panels.
  //   ImGuiID id_left, id_right, id_up, id_down;
  //   id_left  = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, xRatio, nullptr, &dock_main_id);
  //   id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, xRatio / (1 - xRatio), nullptr, &dock_main_id);
  //   id_up    = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, yRatio, nullptr, &dock_main_id);
  //   id_down  = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, yRatio / (1 - yRatio), nullptr, &dock_main_id);

  //   ImGui::DockBuilderDockWindow("Dock_left", id_left);
  //   ImGui::DockBuilderDockWindow("Dock_right", id_right);
  //   ImGui::DockBuilderDockWindow("Dock_up", id_up);
  //   ImGui::DockBuilderDockWindow("Dock_down", id_down);
  //   ImGui::DockBuilderDockWindow("Scene", dock_main_id);

  //   ImGui::DockBuilderFinish(dock_main_id);
  // }

  // // Setting the panel to blend with alpha
  // float const alpha = 0.33;
  // ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  // ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(col.x, col.y, col.z, alpha));

  // ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), dockspaceFlags);

  // ImGui::PopStyleColor();
  // ImGui::End();

  // // The panel
  // if (alpha < 1.0) {
  //   ImGui::SetNextWindowBgAlpha(alpha);
  // }
}

/* -------------------------------------------------------------------------- */
