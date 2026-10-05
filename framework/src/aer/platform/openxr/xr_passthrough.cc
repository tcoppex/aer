#include "aer/platform/openxr/xr_passthrough.h"

#include <algorithm>

/* -------------------------------------------------------------------------- */

bool XRPassthrough::init(XrInstance instance, XrSession session) {
  if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) {
    return false;
  }

  instance_ = instance;
  session_ = session;

  if (!loadExtensionFunctions()) {
    LOGW("[OpenXR] XRPassthrough::loadExtensionFunctions fails.");
    return false;
  }

  XrPassthroughCreateInfoFB passthroughInfo{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
  if (XR_FAILED(pfnCreatePassthroughFB_(session_, &passthroughInfo, &passthrough_))) {
    LOGW("[OpenXR] xrCreatePassthroughFB fails.");
    return false;
  }

  active_ = false;
  return true;
}

// ----------------------------------------------------------------------------

void XRPassthrough::shutdown() {
  for (const auto& geo : geometries_) {
    if (geo.instance != XR_NULL_HANDLE) pfnDestroyGeometryInstanceFB_(geo.instance);
    if (geo.mesh != XR_NULL_HANDLE) pfnDestroyTriangleMeshFB_(geo.mesh);
  }
  geometries_.clear();

  for (XrPassthroughLayerFB layer : layers_) {
    if (layer != XR_NULL_HANDLE) {
      pfnPassthroughLayerPauseFB_(layer);
      pfnDestroyPassthroughLayerFB_(layer);
    }
  }
  layers_.clear();

  if (passthrough_ != XR_NULL_HANDLE) {
    if (active_) {
      pfnPassthroughPauseFB_(passthrough_);
    }
    pfnDestroyPassthroughFB_(passthrough_);
    passthrough_ = XR_NULL_HANDLE;
  }

  active_ = false;
}

// ----------------------------------------------------------------------------

void XRPassthrough::start() {
  if (!active_ && passthrough_ != XR_NULL_HANDLE) {
    pfnPassthroughStartFB_(passthrough_);
    active_ = true;
  }
}

// ----------------------------------------------------------------------------

void XRPassthrough::pause() {
  if (active_ && passthrough_ != XR_NULL_HANDLE) {
    pfnPassthroughPauseFB_(passthrough_);
    active_ = false;
  }
}

// ----------------------------------------------------------------------------

XrPassthroughLayerFB XRPassthrough::createReconstructionLayer() {
  XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
  layerInfo.passthrough = passthrough_;
  layerInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
  layerInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

  XrPassthroughLayerFB layer = XR_NULL_HANDLE;
  if (XR_SUCCEEDED(pfnCreatePassthroughLayerFB_(session_, &layerInfo, &layer))) {
    layers_.push_back(layer);
  }
  setLayerStyle(layer, 1.0f);
  return layer;
}

// ----------------------------------------------------------------------------

XrPassthroughLayerFB XRPassthrough::createProjectedLayer() {
  XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
  layerInfo.passthrough = passthrough_;
  layerInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_PROJECTED_FB;
  layerInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

  XrPassthroughLayerFB layer = XR_NULL_HANDLE;
  if (XR_SUCCEEDED(pfnCreatePassthroughLayerFB_(session_, &layerInfo, &layer))) {
    layers_.push_back(layer);
  }
  setLayerStyle(layer, 1.0f);
  return layer;
}

// ----------------------------------------------------------------------------

void XRPassthrough::destroyLayer(XrPassthroughLayerFB layer) {
  auto it = std::find(layers_.begin(), layers_.end(), layer);
  if (it != layers_.end()) {
    pfnDestroyPassthroughLayerFB_(*it);
    layers_.erase(it);
  }
}

// ----------------------------------------------------------------------------

void XRPassthrough::setLayerStyle(
  XrPassthroughLayerFB layer,
  float opacity,
  XrColor4f edgeColor
) {
  XrPassthroughStyleFB style{XR_TYPE_PASSTHROUGH_STYLE_FB};
  style.textureOpacityFactor = opacity;
  style.edgeColor = edgeColor;
  pfnPassthroughLayerSetStyleFB_(layer, &style);
}

// ----------------------------------------------------------------------------

XrGeometryInstanceFB XRPassthrough::createGeometryInstance(
  XrPassthroughLayerFB layer, XrSpace baseSpace,
  std::vector<XrVector3f> const& vertices,
  std::vector<uint32_t> const& indices
) {
    Geometry geometry;

    XrTriangleMeshCreateInfoFB meshInfo{XR_TYPE_TRIANGLE_MESH_CREATE_INFO_FB};
    meshInfo.vertexCount = static_cast<uint32_t>(vertices.size());
    meshInfo.vertexBuffer = vertices.data();
    meshInfo.triangleCount = static_cast<uint32_t>(indices.size() / 3);
    meshInfo.indexBuffer = indices.data();
    meshInfo.windingOrder = XR_WINDING_ORDER_UNKNOWN_FB;

    if (XR_FAILED(pfnCreateTriangleMeshFB_(session_, &meshInfo, &geometry.mesh))) {
      return XR_NULL_HANDLE;
    }

    geometry.transform.baseSpace = baseSpace;
    geometry.transform.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    geometry.transform.pose.position = {0.0f, 0.0f, 0.0f};
    geometry.transform.scale = {1.0f, 1.0f, 1.0f};

    XrGeometryInstanceCreateInfoFB geometryInfo{XR_TYPE_GEOMETRY_INSTANCE_CREATE_INFO_FB};
    geometryInfo.layer = layer;
    geometryInfo.mesh = geometry.mesh;
    geometryInfo.scale = geometry.transform.scale;
    geometryInfo.baseSpace = baseSpace;
    geometryInfo.pose = geometry.transform.pose;

    if (XR_FAILED(pfnCreateGeometryInstanceFB_(session_, &geometryInfo, &geometry.instance))) {
      pfnDestroyTriangleMeshFB_(geometry.mesh);
      return XR_NULL_HANDLE;
    }

    geometries_.push_back(geometry);
    return geometry.instance;
}

// ----------------------------------------------------------------------------

void XRPassthrough::destroyGeometryInstance(XrGeometryInstanceFB instance) {
  auto it = std::find_if(
    geometries_.begin(),
    geometries_.end(),
    [instance](Geometry const& geo) { return geo.instance == instance; }
  );

  if (it != geometries_.end()) {
    pfnDestroyTriangleMeshFB_(it->mesh);
    pfnDestroyGeometryInstanceFB_(it->instance);
    geometries_.erase(it);
  }
}

// ----------------------------------------------------------------------------

void XRPassthrough::setGeometryTransform(
  XrGeometryInstanceFB instance,
  XrTime time,
  XrPosef const& pose, XrVector3f const& scale
) {
  auto it = std::find_if(
    geometries_.begin(),
    geometries_.end(),
    [instance](Geometry const& geo) { return geo.instance == instance; }
  );

  if (it != geometries_.end()) {
    it->transform.time = time;
    it->transform.pose = pose;
    it->transform.scale = scale;
    pfnGeometryInstanceSetTransformFB_(instance, &it->transform);
  }
}

// ----------------------------------------------------------------------------

bool XRPassthrough::loadExtensionFunctions() {
  auto getProc = [this](char const* name, PFN_xrVoidFunction* out) {
    bool res = XR_SUCCEEDED(xrGetInstanceProcAddr(instance_, name, out));
    if (!res) {
      LOGW("{} not found.", name);
    }
    return res;
  };

  bool ok = true;

  ok &= getProc("xrCreatePassthroughFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreatePassthroughFB_));
  ok &= getProc("xrDestroyPassthroughFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyPassthroughFB_));
  ok &= getProc("xrPassthroughStartFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnPassthroughStartFB_));
  ok &= getProc("xrPassthroughPauseFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnPassthroughPauseFB_));
  ok &= getProc("xrCreatePassthroughLayerFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreatePassthroughLayerFB_));
  ok &= getProc("xrDestroyPassthroughLayerFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyPassthroughLayerFB_));
  ok &= getProc("xrPassthroughLayerResumeFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnPassthroughLayerResumeFB_));
  ok &= getProc("xrPassthroughLayerPauseFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnPassthroughLayerPauseFB_));
  ok &= getProc("xrPassthroughLayerSetStyleFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnPassthroughLayerSetStyleFB_));

  ok &= getProc("xrCreateGeometryInstanceFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateGeometryInstanceFB_));
  ok &= getProc("xrDestroyGeometryInstanceFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyGeometryInstanceFB_));
  ok &= getProc("xrGeometryInstanceSetTransformFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnGeometryInstanceSetTransformFB_));
  ok &= getProc("xrCreateTriangleMeshFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateTriangleMeshFB_));
  ok &= getProc("xrDestroyTriangleMeshFB", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyTriangleMeshFB_));

  return ok;
}

/* -------------------------------------------------------------------------- */