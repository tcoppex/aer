#include "aer/platform/impl/android/jni_context.h"

extern "C" {
#include <android/asset_manager_jni.h>
}

/* -------------------------------------------------------------------------- */

JNIContext::JNIContext(struct android_app* app) {
  app_            = app;
  jvm_            = app->activity->vm;
  asset_manager_  = app->activity->assetManager;

  JNIEnv* env{nullptr};
  jvm_->AttachCurrentThread(&env, nullptr);
}

// ----------------------------------------------------------------------------

JNIContext::~JNIContext() {
  if (jvm_) {
    jvm_->DetachCurrentThread();
    jvm_ = nullptr;
  }
}

// ----------------------------------------------------------------------------

bool JNIContext::readFile(std::string_view filename, std::vector<uint8_t>& buffer) {
  if (nullptr == asset_manager_) {
    LOGW("JNIContext has not been initialized.");
    return false;
  }

  AAsset* asset = AAssetManager_open(asset_manager_, filename.data(), AASSET_MODE_STREAMING);
  if (!asset) {
    LOGE("Failed to open asset: {}", filename);
    return false;
  }

  size_t const assetLength = AAsset_getLength(asset);
  if (assetLength <= 0) {
    LOGE("Asset length is invalid: {}", filename);
    AAsset_close(asset);
    return false;
  }
  buffer.resize(assetLength);

  if (int bytesRead = AAsset_read(asset, buffer.data(), assetLength); bytesRead <= 0) {
    LOGE("Failed to read asset: {}", filename);
    AAsset_close(asset);
    return false;
  }
  AAsset_close(asset);

  LOGD("Successfully loaded asset: {}", filename);
  return true;
}

// ----------------------------------------------------------------------------

bool JNIContext::readFile(std::string_view filename) {
  return readFile(filename, buffer_);
}

/* -------------------------------------------------------------------------- */
