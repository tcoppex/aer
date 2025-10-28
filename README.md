<!--
<pre>
          _____                    _____                    _____
         /\    \                  /\    \                  /\    \
        /::\    \                /::\    \                /::\    \
       /::::\    \              /::::\    \              /::::\    \
      /::::::\    \            /::::::\    \            /::::::\    \
     /:::/\:::\    \          /:::/\:::\    \          /:::/\:::\    \
    /:::/__\:::\    \        /:::/__\:::\    \        /:::/__\:::\    \
   /::::\   \:::\    \      /::::\   \:::\    \      /::::\   \:::\    \
  /::::::\   \:::\    \    /::::::\   \:::\    \    /::::::\   \:::\    \
 /:::/\:::\   \:::\    \  /:::/\:::\   \:::\    \  /:::/\:::\   \:::\____\
/:::/  \:::\   \:::\____\/:::/__\:::\   \:::\____\/:::/  \:::\   \:::|    |
\::/    \:::\  /:::/    /\:::\   \:::\   \::/    /\::/   |::::\  /:::|____|
 \/____/ \:::\/:::/    /  \:::\   \:::\   \/____/  \/____|:::::\/:::/    /
          \::::::/    /    \:::\   \:::\    \            |:::::::::/    /
           \::::/    /      \:::\   \:::\____\           |::|\::::/    /
           /:::/    /        \:::\   \::/    /           |::| \::/____/
          /:::/    /          \:::\   \/____/            |::|  ~|
         /:::/    /            \:::\    \                |::|   |
        /:::/    /              \:::\____\               \::|   |
        \::/    /                \::/    /                \:|   |
         \/____/                  \/____/                  \|___|

</pre>
-->

![language: c++20](https://img.shields.io/badge/c++-20-blue.svg)
![api: vulkan1.3](https://img.shields.io/badge/Vulkan-1.3-red.svg)
![api: openxr1.1](https://img.shields.io/badge/OpenXR-1.1-purple.svg)
![api: android32](https://img.shields.io/badge/Android_API-32-green.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

# aer

A real-time 3d rendering framework, inspired by WebGPU and _vk\_minimal\_latest_.

Runs on GNU/Linux, Windows 11 and Android 12L (_Meta Quest 3_). See [BUILD.md](BUILD.md) for detailed build instructions.

<details>
  <summary><strong>Quick start & run!</strong></summary>

```bash
# [Optional] Retrieve system build dependencies with Synaptic.
# sudo apt install git git-lfs build-essential cmake vulkan-sdk

# [Optionnal] Specify the ANDROID_SDK path to create Android targets.
# export ANDROID_SDK=~/Android/Sdk

# Clone the repository.
git clone https://github.com/tcoppex/aer
cd aer

# Build.
cmake . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run the first sample.
./bin/00_hello

# [Optionnal] Build & Run an Android sample on a connected device.
# cmake --build build --target run_aloha
```

</details>


### Samples

* **[00_hello](samples/desktop/00_hello/main.cc)**: Display a surface and clear its color (_Device, Swapchain, dynamic rendering_).
* **[01_triangle](samples/desktop/01_triangle/main.cc)**: Display a simple triangle (_Shader, Graphics Pipeline, Vertex Buffer, Commands_).
* **[02_push_constant](samples/desktop/02_push_constant/main.cc)**: Update per-frame values via push constants and dynamic states (_Push Constant_).
* **[03_descriptor_set](samples/desktop/03_descriptor_set/main.cc)**: Initialize & update a descriptor set on a single uniform buffer (_Descriptor Set_).
* **[04_texturing](samples/desktop/04_texturing/main.cc)**: Display a textured cube with a linear sampler (_Image, Sampler_).
* **[05_stencil_op](samples/desktop/05_stencil_op/main.cc)**: Stencil operations and instancing through a multi-passes portal effect (_Stencil, instancing_).
* **[06_blend_op](samples/desktop/06_blend_op/main.cc)**: Fast & simple billboarded GPU particles with additive blending (_Blending_).
* **[07_compute](samples/desktop/07_compute/main.cc)**: Waves simulation with sorted alpha-blended particles (_Compute Pipeline, Buffer Barriers_).
* **[08_hdr_envmap](samples/desktop/08_hdr_envmap/main.cc)**: Compute Image-Based Lighting from a HDR environment map (_Texture Barriers_).
* **[09_post_process](samples/desktop/09_post_process/main.cc)**: Screen-space contour effect via a post-processing pipeline (_Render Target_, _Blit_).
* **[10_material](samples/desktop/10_material/main.cc)**: Showcase the internal PBR material system with scene graph ordering (_Pipeline Cache_, _Specialization Constants_).
* **[11_raytracing](samples/desktop/11_raytracing/main.cc)**: Simple path tracer on a Cornell box via hardware-accelerated ray tracing (_Acceleration Structure_, _Ray Tracing Pipeline_, _Buffer Device Address_).
* **[12_font](samples/desktop/12_font/main.cc)**: Dynamic 2D/3D text generation from a font file.

Samples are linear in progression: when a feature is introduced the
first version uses a somewhat verbose semantic before switching to simpler ones in subsequent examples.

### Acknowledgement

This project was inspired by the expressiveness of WebGPU and the work of **NVIDIA DesignWorks Samples**, in particular the _[vk_minimal_latest](https://github.com/nvpro-samples/vk_minimal_latest)_ project.

### License

This project is released under the _MIT License_.

:leaves:

