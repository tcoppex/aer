# Build

This project was tested on the following platforms and compilers:

| Platform                | Compiler                |
|-------------------------|-------------------------|
| GNU/Linux               | GCC 11.4                |
| Windows 11              | MSVC 19.38              |
| Android 12L (API 32)    | Clang 21                |

For desktop build you only need a compatible CMake version and a build toolchains
to fetch the project dependencies automatically.

You can then easily build the project using those commands:

```bash
# [Optional] Retrieve system build dependencies with Synaptic.
# sudo apt install git git-lfs build-essential cmake vulkan-sdk

# Clone the repository.
git clone https://github.com/tcoppex/aer
cd aer

# Build.
cmake . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# [Optionnal] Build & Run an Android sample on a connected device.
# cmake --build build --target run_aloha
```

### Dependencies

##### Third parties

 * CMake 3.22.1
 * CPM 0.40.3
 * Vulkan 1.4.321.0 headers
 * Volk 1.4.321.0
 * VulkanMemoryAllocator 3.2.0
 * Slang 2026.5.2
 * GLFW 3.4
 * libfmt 12.0.0
 * ImGUI v1.92.3-docking
 * cgltf 1.15
 * miniply 1.10
 * MikkTSpace
 * linalg v2.2
 * earcut v2.2.4
 * utfcpp v4.0.8
 * stb\_truetype.h
 * stb\_image.h

<!--
* KTX (Khronos Texture) Library 4.4.2
-->

By default CPM downloads and caches third-party dependencies in the `$CPM_SOURCE_CACHE` directory (_either fetched from env or cmake variable_). When none exist it will default to `./third_party/.cpmlocalcache/`.

##### Vulkan device extensions

This project use Vulkan 1.3 alongside the following device extensions, some being non mandatory.

* [VK_EXT_extended_dynamic_state3](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_extended_dynamic_state3.html)
* [VK_EXT_vertex_input_dynamic_state](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_vertex_input_dynamic_state.html)
* [VK_EXT_image_view_min_lod](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_image_view_min_lod.html)
* [VK_KHR_swapchain](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_swapchain.html)
* [VK_KHR_index_type_uint8](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_index_type_uint8.html)
* [VK_KHR_maintenance5](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_maintenance5.html)
* [VK_KHR_maintenance6](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_maintenance6.html)
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

# Setup ANDROID_HOME.
export ANDROID_HOME=$HOME/Android
mkdir $ANDROID_HOME && cd $ANDROID_HOME

# Download & install Android SDK Command-line Tools 12.0.
wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip commandlinetools-linux-11076708_latest.zip -d cmdline-tools
mv cmdline-tools/cmdline-tools cmdline-tools/latest
export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH

# Install dependencies.
sdkmanager "platforms;android-36" "platform-tools" "build-tools;36.0.0" "ndk;29.0.14033849"
```
</details>

<details>
  <summary><strong>Windows 11</strong></summary>

```bash
# Install JDK manually on Windows (eg. Temurin 17), and setup the JAVA_HOME environment variable.
#export JAVA_HOME="C:\Program Files (x86)\Eclipse Adoptium\jdk-17.0.16.8-hotspot"

# Setup ANDROID_HOME as needed
export ANDROID_HOME="$HOME/Android/Sdk"
mkdir -p $ANDROID_HOME && cd $ANDROID_HOME

# Download & install Android SDK Command-line Tools 12.0.
curl -O https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip
unzip commandlinetools-win-11076708_latest.zip -d cmdline-tools
mv cmdline-tools/cmdline-tools cmdline-tools/latest
export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH

# Install dependencies.
sdkmanager.bat --sdk_root=$ANDROID_HOME "platforms;android-36" "platform-tools" "build-tools;36.0.0" "ndk;29.0.14033849"
```
</details>

##### Build and Run

Each Android sample provides a set of CMake debug targets in the form `{prefix}{sample_name}`
(_eg. `log_aloha`_) to simplify development without the need to launch Android Studio. All of this commands target debug builds.

| Target Prefix | Action                                                  |
|---------------|---------------------------------------------------------|
| **build_**    | Build the sample.                                       |
| **install_**  | Build and install the sample on a connected device.     |
| **run_**      | Build, install, and run the sample.                     |
| **log_**      | Build, install, run, and stream the sample’s logcat.    |

_Device-dependent targets (**install**, **run**, **log**) require a compatible connected Android device._

_**Note:** the Gradle's cmake cache directory (`.cxx` in projects subdirectory) might require to be cleaned up manually when local configurations change (eg. Vulkan SDK directory)._

### VR / XR support

Virtual and extended reality are supported on Android-based devices, specifically the _Meta Quest 3_.

### Assets

A few assets are served via `git-lfs` but most will be downloaded automatically on CMake Cache generation time.

