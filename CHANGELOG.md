# Changelog

All notable changes to Rasterizer are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org). Until 1.0.0, minor versions may make breaking changes to the API.

## [Unreleased]

## [0.1.0] - 2026-10-10

The first versioned release, installable from the repository URL with Swift Package Manager.

### Added

- Swift package with three libraries: `RasterizerCpp`, the C++17 core; `RasterizerObjC`, the Swift and Objective-C API with `RasterizerView`; and `RasterizerSwift`, Swift helpers.
- Fills with even-odd and non-zero fill rules, and strokes with butt, square or round caps and miter or round joins.
- Dashed copies of paths.
- Linear and radial gradients, and images.
- Clipping to rectangles, and to anti-aliased clip paths with either fill rule.
- All of Core Graphics' separable and non-separable blend modes.
- Text laid out by Core Text.
- SVG import via NanoSVG.
- PDF import, with a parallel content-stream parser.
- Editable scenes: `-[RAScene updateDrawsInRange:usingBlock:]` changes draws in place.
- Up to 120Hz on ProMotion displays.
- iOS demo app: browse bundled and imported SVG and PDF files, pan and zoom, a PDF page stepper, a grid animation, and opening files sent from the Files app.

### Changed

- Licensed under the MIT license, replacing the personal-use license.
- `Package.swift` moved to the repository root, so the package can be added by URL.

### Removed

- The PDFium dependency. PDFs are now parsed by Rasterizer itself.

[Unreleased]: https://github.com/mindbrix/Rasterizer/compare/0.1.0...HEAD
[0.1.0]: https://github.com/mindbrix/Rasterizer/releases/tag/0.1.0
