# vk3dgrt

A Vulkan-based 3D Gaussian Ray Tracing (3DGRT) viewer.

![screenshot](docs/screenshot.png) <!-- TODO: Add screenshot -->

## Features

- Vulkan 1.3 Ray Tracing Pipeline (VK_KHR_ray_tracing_pipeline)
- 3D Gaussian Splatting rendering (Gaussian / Point / Splat modes)
- View-dependent color via Spherical Harmonics (SH degree 3)
- BLAS/TLAS acceleration structures
- Real-time parameter tuning with ImGui
- Interactive camera control

## Requirements

| Item | Requirement |
|------|------------|
| OS | Windows 10/11 |
| GPU | Vulkan Ray Tracing capable GPU (e.g. NVIDIA RTX series) |
| Vulkan SDK | [LunarG Vulkan SDK](https://vulkan.lunarg.com/) |
| Compiler | MSVC (Visual Studio 2022) |
| CMake | 3.16+ |
| Build System | Ninja |
| C++ Standard | C++23 |

## Dependencies

Managed via Git submodules.

| Library | Purpose |
|---------|---------|
| [GLFW](https://github.com/glfw/glfw) | Window and input handling |
| [ImGui](https://github.com/ocornut/imgui) | GUI |
| [GLM](https://github.com/g-truc/glm) | Math library |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Vulkan memory management |
| [tinyply](https://github.com/ddiakopoulos/tinyply) | PLY file parsing |

## Build

```bash
# 1. Clone
git clone --recursive https://github.com/jgnooo/vk3dgrt.git
cd vk3dgrt

# If submodules were not cloned
git submodule update --init --recursive

# 2. Build (requires VS 2022 Developer Environment)
# Run in PowerShell
cmake -B build -S . -G Ninja
cmake --build build --config Debug
```

## Run

```bash
# Specify a PLY file
./build/bin/vk3dgrt.exe path/to/scene.ply
```

## Project Structure

```
vk3dgrt/
├── src/
│   ├── main.cpp              # Entry point
│   ├── vulkan/               # Vulkan engine (context, pipeline, buffer, image, etc.)
│   ├── 3dgrt/                # 3DGRT implementation (scene, loader, renderer, accel struct)
│   └── gui/                  # ImGui integration and camera control
├── shaders/                  # GLSL RT shaders (rgen, rchit, rahit, rmiss)
├── data/                     # PLY scene data
└── dependencies/             # External libraries (git submodules)
```

## Tested Environment

| Item | Spec |
|------|------|
| OS | Windows 11 Home |
| GPU | NVIDIA GeForce RTX 5070 |
| Driver | 32.0.15.7688 |

## References

- [3D Gaussian Splatting for Real-Time Radiance Field Rendering](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)
- [nvpro-samples/vk_gaussian_splatting](https://github.com/nvpro-samples/vk_gaussian_splatting)
- <!-- TODO: Add additional references -->
