# FourierForge

A real-time SVG visualization tool using Discrete Fourier Transform and epicycles to draw complex paths with OpenGL rendering and video export capabilities.

## Features

- **SVG Path Parsing**: Load and sample SVG files with high-fidelity bezier curve extraction
- **Fourier Transform Visualization**: Real-time DFT computation displaying rotating circles (epicycles)
- **Interactive Camera**: Pan, zoom, and auto-follow modes
- **Video Export**: Cinematic auto-recording with FFmpeg integration
- **Visual Customization**: Rainbow ink, trail modes, adjustable stroke width
- **Performance Optimized**: Instanced rendering, async loading, multi-threaded computation

## Dependencies

- SDL2
- GLEW
- OpenGL 3.3+
- GLM
- Dear ImGui (auto-fetched)
- ImGuiFileDialog (auto-fetched)
- nanosvg (included)
- FFmpeg (for video export)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
./FourierForge
```

## Usage

1. Click "Load SVG" to select an SVG file
2. Adjust vector count to control approximation quality
3. Use playback controls to animate the drawing
4. Export videos using the "Cinematic Auto-Render" feature

## Controls

- **Mouse Wheel**: Zoom in/out
- **Left Click + Drag**: Pan camera
- **Reset Button**: Return to start with ghost outline visible

## Architecture

- `FourierCore.hpp`: DFT computation and epicycle evaluation
- `SVGParser.hpp`: SVG parsing with nanosvg, normalization, and arc-length resampling
- `Renderer.hpp`: Instanced circle/line batching, trail rendering
- `VideoExporter.hpp`: FFmpeg pipe for high-quality video capture
- `main.cpp`: Application loop, UI, camera system, animation state machine

## License

See individual component licenses in their respective directories.
