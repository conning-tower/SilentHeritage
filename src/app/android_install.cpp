#include "app/android_install.h"

#include <SDL3/SDL_system.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sh {

namespace {

constexpr const char* kIndexAsset = "bundle_index.txt";
constexpr std::string_view kBundleDir = "bundle/";

struct AssetCloser {
  void operator()(AAsset* asset) const { AAsset_close(asset); }
};

// Returns the whole content of asset `path`, nothing if it cannot be read.
// The asset manager reads assets directly (SDL_IOFromFile would first look
// for the same relative path in the app's internal storage).
std::optional<std::vector<std::uint8_t>> read_asset(AAssetManager* manager, const std::string& path) {
  const std::unique_ptr<AAsset, AssetCloser> asset(AAssetManager_open(manager, path.c_str(), AASSET_MODE_BUFFER));
  if (!asset) {
    return std::nullopt;
  }
  const off64_t length = AAsset_getLength64(asset.get());
  if (length < 0) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
  std::size_t done = 0;
  while (done < data.size()) {
    const int n = AAsset_read(asset.get(), data.data() + done, data.size() - done);
    if (n <= 0) {
      return std::nullopt;
    }
    done += static_cast<std::size_t>(n);
  }
  return data;
}

// Returns the native asset manager of SDL's activity.
AAssetManager* asset_manager() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    throw std::runtime_error(std::format("no Android activity: {}", SDL_GetError()));
  }
  const jclass activity_class = env->GetObjectClass(activity);
  const jmethodID get_assets = env->GetMethodID(activity_class, "getAssets", "()Landroid/content/res/AssetManager;");
  const jobject assets = env->CallObjectMethod(activity, get_assets);
  AAssetManager* manager = nullptr;
  if (assets != nullptr) {
    // The global reference keeps the Java asset manager, which owns the
    // native one, alive for the life of the process.
    manager = AAssetManager_fromJava(env, env->NewGlobalRef(assets));
    env->DeleteLocalRef(assets);
  }
  env->DeleteLocalRef(activity_class);
  env->DeleteLocalRef(activity);
  if (manager == nullptr) {
    throw std::runtime_error("no Android asset manager");
  }
  return manager;
}

}  // namespace

std::optional<Install> apk_install() {
  AAssetManager* manager = asset_manager();
  const std::optional<std::vector<std::uint8_t>> index = read_asset(manager, kIndexAsset);
  if (!index) {
    return std::nullopt;
  }
  return Install::packaged(std::string_view(reinterpret_cast<const char*>(index->data()), index->size()),
                           [manager](const std::string& path) {
                             return read_asset(manager, std::string(kBundleDir) + path);
                           },
                           "the APK");
}

}  // namespace sh
