#ifndef AER_APP_SETTINGS_H_
#define AER_APP_SETTINGS_H_

#include "aer/platform/wm_interface.h"  // for WMInterface::Settings
#include "aer/renderer/renderer.h"      // for RenderContext::Settings

/* -------------------------------------------------------------------------- */

/*
 * AppSettings
 *
 * Define default values to be used by the application.
 * Thoses can be changed per app by overriding Application::settings()
 *
 **/
struct AppSettings {
  WMInterface::Settings surface{
    .width  = 0u,                       //< When null, will use a default value
    .height = 0u,                       //< idem
  };

  RenderContext::Settings renderer{
    .color_format         = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
    .depth_stencil_format = VK_FORMAT_D24_UNORM_S8_UINT,
    .sample_count         = VK_SAMPLE_COUNT_1_BIT,
  };

  // Those will be overrided by the application.
  std::string app_name{"VkFramework::AppName"};
  bool use_xr{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_APP_SETTINGS_H_