# Build

This project target the following platforms with their correlated compiler :

| Platform                | Compiler                |
|-------------------------|-------------------------|
| GNU/Linux               | GCC 11.4                |
| Windows 11              | MSVC 19.38              |
| Android 12L (API 32)    | Clang 21                |

For desktop build you only need a compatible CMake version and a build toolchains
to fetch the project dependencies automatically.

You can then easily fetch and build the project using those commands :

```bash
git clone https://github.com/tcoppex/aer
cd aer
cmake . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Dependencies

##### Third parties

 * CMake 3.22.1
 * CPM 0.40.3 (_downloaded automatically_)
 * Vulkan SDK 1.1 (_1.4.321.0 headers downloaded via CPM_)
 * Volk 1.4.321.0 (_via CPM_)
 * VulkanMemoryAllocator 3.2.0 (_via CPM_)
 * GLFW 3.4 (_via CPM_)
 * libfmt 12.0.0 (_via CPM_)
 * ImGUI v1.92.3-docking (_via CPM_)
 * MikkTSpace (_via CPM_)
 * linalg v2.2 (_via CPM_)
 * earcut v2.2.4 (_via CPM_)
 * utfcpp v4.0.8 (_via CPM_)
 * stb\_truetype.h (_fetched_)
 * stb\_image.h (_included_)

By default, CPM downloads and caches third-party dependencies in the `$CPM_SOURCE_CACHE` directory (either fetch from env or cmake variable). When none exist it will default to `./third_party/.cpmlocalcache/`.

##### Vulkan device extensions

Here is a non exhaustive list of main Vulkan extensions use, some are non mandatory and only target specific samples.

* [VK_EXT_descriptor_indexing](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_descriptor_indexing.html)
* [VK_EXT_extended_dynamic_state](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_extended_dynamic_state.html)
* [VK_EXT_extended_dynamic_state2](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_extended_dynamic_state2.html)
* [VK_EXT_extended_dynamic_state3](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_extended_dynamic_state3.html)
* [VK_EXT_image_view_min_lod](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_image_view_min_lod.html)
* [VK_EXT_vertex_input_dynamic_state](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_vertex_input_dynamic_state.html)
* [VK_KHR_buffer_device_address]( https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_buffer_device_address.html)
* [VK_KHR_create_renderpass2](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_create_renderpass2.html)
* [VK_KHR_depth_stencil_resolve](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_depth_stencil_resolve.html)
* [VK_KHR_dynamic_rendering](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_dynamic_rendering.html)
* [VK_KHR_index_type_uint8](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_index_type_uint8.html)
* [VK_KHR_maintenance4](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_maintenance4.html)
* [VK_KHR_maintenance5](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_maintenance5.html)
* [VK_KHR_maintenance6](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_maintenance6.html)
* [VK_KHR_swapchain](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_swapchain.html)
* [VK_KHR_synchronization2](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_synchronization2.html)
* [VK_KHR_timeline_semaphore](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_timeline_semaphore.html)
* [VK_KHR_acceleration_structure](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_acceleration_structure.html)
* [VK_KHR_ray_tracing_pipeline](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_ray_tracing_pipeline.html)

### Android build

The Android build root project is defined in the `android/` subfolder and use the following dependencies :

 * Android SDK 36
 * Android NDK 29.0.14033849
 * Gradle 8.14.3

You can either install them via Android Studio (_eg. Narwhal 3 Feature Drop | 2025.1.3_) or depending on your platform using one of those script :

<details>
  <summary><strong>GNU/Linux</strong></summary>

```bash
# Install tools and the JDK.
sudo apt-get install -y unzip wget openjdk-17-jdk

# Setup ANDROID_SDK.
export ANDROID_SDK=$HOME/Android
mkdir $ANDROID_SDK && cd $ANDROID_SDK

# Download & install Android SDK Command-line Tools 12.0.
wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip commandlinetools-linux-11076708_latest.zip -d cmdline-tools
mv cmdline-tools/cmdline-tools cmdline-tools/latest
export PATH=$ANDROID_SDK/cmdline-tools/latest/bin:$ANDROID_SDK/platform-tools:$PATH

# Install dependencies.
sdkmanager "platforms;android-36" "platform-tools" "build-tools;36.0.0" "ndk;29.0.14033849"
```
</details>

<details>
  <summary><strong>Windows 11</strong></summary>

```bash
# Install JDK manually on Windows (eg. Temurin 17), and setup the JAVA_HOME environment variable.
#export JAVA_HOME="C:\Program Files (x86)\Eclipse Adoptium\jdk-17.0.16.8-hotspot"

# Setup ANDROID_SDK (also refered as $ANDROID_HOME)
export ANDROID_SDK="$HOME/Android"
mkdir -p $ANDROID_SDK && cd $ANDROID_SDK

# Download & install Android SDK Command-line Tools 12.0.
curl -O https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip
unzip commandlinetools-win-11076708_latest.zip -d cmdline-tools
mv cmdline-tools/cmdline-tools cmdline-tools/latest
export PATH=$ANDROID_SDK/cmdline-tools/latest/bin:$ANDROID_SDK/platform-tools:$PATH

# Install dependencies.
sdkmanager.bat --sdk_root=$ANDROID_SDK "platforms;android-36" "platform-tools" "build-tools;36.0.0" "ndk;29.0.14033849"
```
</details>

###### Build and Run

Each Android sample provides a set of CMake debug targets in the form `{prefix}{sample_name}`
(_eg. `log_aloha`_) to simplify development without the need to launch Android Studio. All of this commands target debug builds.

| Target Prefix | Action                                                  |
|---------------|---------------------------------------------------------|
| **build_**    | Build the sample.                                       |
| **install_**  | Build and install the sample on a connected device.     |
| **run_**      | Build, install, and run the sample.                     |
| **log_**      | Build, install, run, and stream the sample’s logcat.    |

_Device-dependent targets (**install**, **run**, **log**) require a compatible connected Android device._

#### Assets

A few assets are served via `git-lfs` but most will be downloaded automatically on CMake Cache generation time.

