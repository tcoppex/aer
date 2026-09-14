## 13 - Gaussian Splatting

A 3D Gaussian Splatting rasterization pipeline implemented in Slang, utilizing a Onesweep radix sort for key-value sorting.

### Datasets

This sample use the [3DGS flowers model](http://developer.download.nvidia.com/ProGraphics/nvpro-samples/flowers_1.zip).

More assets can be found on the _vk\_gaussian\_splatting_ [datasets page](https://nvpro-samples.github.io/vk_gaussian_splatting/datasets/).

### References

* ["3D Gaussian Splatting for Real-Time Radiance Field Rendering"](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/) – Bernhard Kerbl, Georgios Kopanas, Thomas Leimkühler, George Drettakis. *ACM Transactions on Graphics (TOG)*, Vol. 42, No. 4, July 2023.
* ["Onesweep: A Faster Least Significant Digit Radix Sort for GPUs"](https://arxiv.org/abs/2206.01784) – Andy Adinets and Duane Merrill. arXiv:2206.01784, 2022.
* ["Single-pass Parallel Prefix Scan with Decoupled Look-back"](https://research.nvidia.com/publication/2016-03_single-pass-parallel-prefix-scan-decoupled-look-back) – Duane Merrill and Michael Garland. NVIDIA Technical Report NVR-2016-002, March 2016.
