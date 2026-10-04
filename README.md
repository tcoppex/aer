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
/:::/  \:::\   \:::\____\/:::/__\:::\   \:::\____\/:::/  \:::\   \:::\    \
\::/    \:::\  /:::/    /\:::\   \:::\   \::/    /\::/   |::::\  /:::|____|
 \/____/ \:::\/:::/    /  \:::\   \:::\   \/____/  \/____|:::::\/:::/    /
          \::::::/    /    \:::\   \:::\    \            |:::::::::/    /
           \::::/    /      \:::\   \:::\____\           |::|\::::/    /
           /:::/    /        \:::\   \::/    /           |::| \::/    /
          /:::/    /          \:::\   \/____/            |::|  \/____/
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

## aer

A cross-platform real-time rendering framework, inspired by WebGPU and _vk\_minimal\_latest_.

See [BUILD.md](BUILD.md) for detailed build instructions.

<details>
  <summary><strong>Quick start & run!</strong></summary>

```bash
# Clone the repository.
git clone https://github.com/tcoppex/aer
cd aer

# Build.
cmake . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run the first sample.
./bin/00_hello
```

</details>

### Samples

|   |   |
| --- | --- |
| **[00_hello](samples/desktop/00_hello/)** | Display a surface and clear its color. |
| **[01_triangle](samples/desktop/01_triangle/)** | Draw a simple triangle. |
| **[02_push_constant](samples/desktop/02_push_constant/)** | Update per-frame values via push constants and dynamic states. |
| **[03_descriptor_set](samples/desktop/03_descriptor_set/)** | Initialize & update a descriptor set on a single uniform buffer. |
| **[04_texturing](samples/desktop/04_texturing/)** | Render a textured cube with a linear sampler. |
| **[05_stencil_op](samples/desktop/05_stencil_op/)** | Stencil operations and instancing through a multi-passes portal effect. |
| **[06_blend_op](samples/desktop/06_blend_op/)** | Fast & simple billboarded GPU particles with additive blending. |
| **[07_compute](samples/desktop/07_compute/)** | Waves simulation with sorted alpha-blended particles. |
| **[08_hdr_envmap](samples/desktop/08_hdr_envmap/)** | Compute Image-Based Lighting from a HDR environment map. |
| **[09_post_process](samples/desktop/09_post_process/)** | Screen-space contour effect via a post-processing pipeline. |
| **[10_material](samples/desktop/10_material/)** | Showcase the internal PBR material system with scene graph ordering. |
| **[11_ray_tracing](samples/desktop/11_ray_tracing/)** | Simple path tracer on a Cornell box via hardware-accelerated ray tracing. |
| **[12_font](samples/desktop/12_font/)** | Dynamic 2D/3D text generation from a font file. |
| **[13_gaussian_splatting](samples/desktop/13_gaussian_splatting/)** | Implements *3D Gaussian Splatting for Real-Time Radiance Field Rendering*. |
| **[14_marching_cubes](samples/desktop/14_marching_cubes/)** | Dynamic marching cubes mesh generation via signed distant function. |
|   |   |

<!--
Samples are linear in progression: when a feature is introduced the
first version uses a somewhat verbose semantic before switching to simpler ones in subsequent examples.

### Acknowledgement

This project was inspired by the expressiveness of WebGPU and the work of **NVIDIA DesignWorks Samples**, in particular the _[vk_minimal_latest](https://github.com/nvpro-samples/vk_minimal_latest)_ project.

### License

This project is released under the _MIT License_.
-->

:leaves:
