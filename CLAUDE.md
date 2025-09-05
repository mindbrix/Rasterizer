# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Rasterizer is a GPU-accelerated 2D vector graphics engine for macOS, written in C++11 and Metal. It's designed to be up to 60x faster than CPU rendering, ideal for vector animated UI. The engine supports SVG and PDF rendering with reference-quality anti-aliasing.

## Build Commands

The project uses Xcode for building:

```bash
# Build and run the demo app (preferred for performance)
xcodebuild -project Rasterizer.xcodeproj -scheme "Run Rasterizer" -configuration Release build

# Build debug version
xcodebuild -project Rasterizer.xcodeproj -scheme "Debug Rasterizer" build

# Open in Xcode
open Rasterizer.xcodeproj
```

The Xcode project builds out of the box with all dependencies included. For best performance, use the "Run Rasterizer" scheme.

## Architecture

### Core Components

- **Rasterizer Engine (`src/`)**: Core C++ rendering engine with Metal GPU acceleration
  - `Rasterizer.hpp`: Main API interface with Path, Scene, and SceneList classes
  - `Rasterizer.h`: Configuration constants and definitions
  - `Shaders.metal`: GPU shaders for rendering pipeline

- **Platform Layer (`Apple/`)**: macOS-specific integration
  - `RasterizerLayer.*`: CAMetalLayer subclass for Metal rendering
  - `RasterizerView.*`: NSView wrapper for user interaction
  - `RasterizerCG.hpp`: Core Graphics integration utilities
  - `RasterizerRenderer.hpp`: Platform-specific rendering logic

- **Demo Application (`Demo/`)**: Reference implementation and testing
  - `DemoView.*`: Main demo interface with file loading and interaction
  - `Document.*`: Document-based app architecture
  - `RasterizerDemo.hpp`: Demo-specific rendering logic
  - `Concentrichron.hpp`: Performance animation demo

- **Format Support (`src/`)**: File format parsers
  - `RasterizerSVG.hpp`: SVG path parsing using NanoSVG
  - `RasterizerPDF.hpp`: PDF rendering using PDFium
  - `RasterizerFont.hpp`: Font rendering using STB TrueType
  - `RasterizerWinding.hpp`: Path winding rule algorithms

### Rendering Pipeline

The engine follows a two-stage rendering approach:
1. **Filled paths**: Rasterized to float mask buffer, then to color buffer
2. **Stroked paths**: Rasterized directly to color buffer with GPU triangulation

Small fills use direct geometry copying, while large fills use a "fat scanlines" algorithm on device-space geometry. The GPU holds no persistent state - frames are written to double-buffered shared memory.

### External Dependencies

All dependencies are included in `lib/`:
- **PDFium**: PDF parsing and rendering (`lib/pdfium/`)
- **NanoSVG**: SVG parsing (`lib/nanosvg.*`)
- **STB TrueType**: Font rendering (`lib/stb_truetype.*`)
- **XXHash**: Fast hashing (`lib/xxhash.*`)

## Demo App Controls

- **File Operations**: Open SVG/PDF files via menu or drag-and-drop
- **PDF Navigation**: `-`/`+` keys to change pages
- **Canvas Control**: Trackpad/mouse drag to pan, pinch/scroll to zoom/rotate
- **Precision Control**: Hold `Shift` to zoom/rotate around pointer instead of center
- **Typography**: `Cmd+T` to open font panel
- **Performance Demo**: `T` key to toggle vector animation demo

## Development Notes

- The codebase uses C++11 features and Objective-C++ for macOS integration
- Metal shaders require Xcode's shader compiler
- Performance is optimized for macOS with batch parallelism on CPU stages
- The rendering algorithm uses novel "windowed-inverse-lerp" for pixel coverage calculation
- License is personal-use only (commercial license required separately)