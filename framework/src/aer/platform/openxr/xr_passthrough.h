#pragma once

// check
// https://developers.meta.com/vr/documentation/native/android/mobile-passthrough/
// https://github.com/meta-quest/Meta-OpenXR-SDK/blob/main/Samples/XrSamples/XrDynamicObjects/Src/XrPassthroughHelper.cpp

/* -------------------------------------------------------------------------- */

#include "aer/platform/openxr/xr_common.h"

#include <vector>
#include <algorithm>

/* -------------------------------------------------------------------------- */

class XRPassthrough {
 public:
  struct Geometry {
    XrTriangleMeshFB mesh{XR_NULL_HANDLE};
    XrGeometryInstanceFB instance{XR_NULL_HANDLE};
    XrGeometryInstanceTransformFB transform{XR_TYPE_GEOMETRY_INSTANCE_TRANSFORM_FB};
  };

  XRPassthrough() = default;
  ~XRPassthrough() { shutdown(); }

  XRPassthrough(XRPassthrough const&) = delete;
  XRPassthrough& operator=(XRPassthrough const&) = delete;

  [[nodiscard]]
  bool init(XrInstance instance, XrSession session);

  void shutdown();

  void start();

  void pause();

  [[nodiscard]]
  XrPassthroughLayerFB createReconstructionLayer();

  [[nodiscard]]
  XrPassthroughLayerFB createProjectedLayer();

  void destroyLayer(XrPassthroughLayerFB layer);

  void setLayerStyle(XrPassthroughLayerFB layer, float opacity, XrColor4f edgeColor = {0.0f, 0.0f, 0.0f, 0.0f});

  [[nodiscard]]
  XrGeometryInstanceFB createGeometryInstance(
    XrPassthroughLayerFB layer,
    XrSpace baseSpace,
    std::vector<XrVector3f> const& vertices,
    std::vector<uint32_t> const& indices
  );

  void destroyGeometryInstance(XrGeometryInstanceFB instance);

  void setGeometryTransform(
    XrGeometryInstanceFB instance,
    XrTime time,
    XrPosef const& pose,
    XrVector3f const& scale
  );

  [[nodiscard]]
  bool isActive() const noexcept {
    return active_;
  }

  [[nodiscard]]
  XrPassthroughLayerFB layer(uint32_t index = 0u) const {
    return layers_.at(index);
  }

 private:
  bool loadExtensionFunctions();

 private:
  XrInstance instance_{XR_NULL_HANDLE};
  XrSession session_{XR_NULL_HANDLE};

  XrPassthroughFB passthrough_{XR_NULL_HANDLE};

  std::vector<XrPassthroughLayerFB> layers_{};
  std::vector<Geometry> geometries_{};

  bool active_ = false;

  PFN_xrCreatePassthroughFB pfnCreatePassthroughFB_{nullptr};
  PFN_xrDestroyPassthroughFB pfnDestroyPassthroughFB_{nullptr};
  PFN_xrPassthroughStartFB pfnPassthroughStartFB_{nullptr};
  PFN_xrPassthroughPauseFB pfnPassthroughPauseFB_{nullptr};

  PFN_xrCreatePassthroughLayerFB pfnCreatePassthroughLayerFB_{nullptr};
  PFN_xrDestroyPassthroughLayerFB pfnDestroyPassthroughLayerFB_{nullptr};
  PFN_xrPassthroughLayerResumeFB pfnPassthroughLayerResumeFB_{nullptr};
  PFN_xrPassthroughLayerPauseFB pfnPassthroughLayerPauseFB_{nullptr};
  PFN_xrPassthroughLayerSetStyleFB pfnPassthroughLayerSetStyleFB_{nullptr};

  PFN_xrCreateGeometryInstanceFB pfnCreateGeometryInstanceFB_{nullptr};
  PFN_xrDestroyGeometryInstanceFB pfnDestroyGeometryInstanceFB_{nullptr};
  PFN_xrGeometryInstanceSetTransformFB pfnGeometryInstanceSetTransformFB_{nullptr};

  PFN_xrCreateTriangleMeshFB pfnCreateTriangleMeshFB_{nullptr};
  PFN_xrDestroyTriangleMeshFB pfnDestroyTriangleMeshFB_{nullptr};
};

/* -------------------------------------------------------------------------- */