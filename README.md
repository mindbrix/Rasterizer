Rasterizer
========

Rasterizer is a GPU-accelerated 2D vector graphics engine designed for simplicity, efficiency and portability. 

The current implementation targets macOS using C++ 11 and Metal, but it will work on any GPU that supports instancing and floating point render targets.


Building
--------

The Xcode demo app project builds out of the box as all dependencies are included. For best performance, select the Run Rasterizer scheme.


Demo App
-------

The demo app can open SVG and PDF files. Use to -/+ keys to select PDF pages. Other key mappings are shown on the HUD top left.

Use the trackpad and/or mouse to drag and zoom/rotate the canvas around its center. Hold the `Shift` key to zoom/rotate around the pointer.

A single font is used, settable via the fonts panel (`<Cmd>T`).

![image](https://github.com/mindbrix/Rasterizer/blob/master/Screenshot.png)


Architecture
--------

`Path` objects follow the Postscript model, with the same even-odd and non-zero fill rules. Stroking is also supported.

`Scene` objects group `Path` objects together with their draw parameters: color, affine transform, stroke width (0 for a fill, +ve for user space, -ve for device space), flags (fill rule, end caps etc.) and an optional clip bounds.

For rendering, `SceneList` objects group `Scene` objects together with their draw parameters: affine transform and an optional clip bounds.

Filled paths are rasterized in 2 stages: first to a float mask buffer, then to the color buffer. 

Small screen space fills (e.g. glyphs) are optimal, as raw `Path` geometry can be `memcpy`ed into GPU memory. 

Larger fills use a fat scanlines algorithm on clipped, device-space `Path` geometry. 

Pixel area coverage is calculated using a novel windowed-inverse-lerp algorithm.

Stroked paths are rasterized straight to the color buffer using GPU triangulation.

Quadratic Beziér curves are solved on the GPU, enabling coarse curve geometry.

The CPU stages use simple batch parallelism for efficiency.

The GPU holds no state. Frames are written afresh to double-buffered shared memory. In practice, `memcpy` is cheaper than managing GPU-resident `Path` objects in a parallel context.


Credits
------

A huge thanks to the creators of the following libraries:

[XXHash](https://xxhash.com)

[NanoSVG](https://github.com/memononen/nanosvg)

[STB Truetype](https://github.com/nothings/stb)

[PDFium](https://pdfium.googlesource.com/pdfium/)


License
-------

This library is licensed under a [personal use zlib license](LICENSE.txt)
