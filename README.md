Rasterizer
========

Rasterizer is a GPU-accelerated 2D vector graphics engine designed for simplicity, efficiency and portability. 

The current implementation targets macOS using C++ 11 and Metal, but it will work on any GPU that supports instancing and floating point render targets.


Building
--------

The Xcode demo app project builds out of the box as all dependencies are included. 


Demo App
-------

The demo app supports viewing SVG and PDF files, plus font grids.

![image](https://github.com/mindbrix/Rasterizer/blob/master/Screenshot.png)


Architecture
--------

Paths follow the Postscript model, with the same even-odd and non-zero fill rules. Stroking is also supported.

Paths are collected into Scenes, which are then collected into a SceneList.

The CPU stages make extensive use of highly-efficient and simple batch parallelism.

Filled paths are rasterized in 2 stages: first to a float mask buffer, and then to the color buffer. Small screen space fills (e.g. glyphs) have the optimal CPU path. Larger fills use a fat scanlines algorithm. Pixel area coverage is calculated using a novel windowed-inverse-lerp algorithm that can be trivially extended for zero-cost box blurs.

Stroked paths are rasterized straight to the color buffer using GPU triangulation.

Quadratic Beziér curves are first-class GPU primitives, so no expensive, scale-variant path flattening is necessary.


Credits
------

A huge thanks to the creators of the following libraries:

[XXHash](https://xxhash.com)

[NanoSVG](https://github.com/memononen/nanosvg)

[STB Truetype](https://github.com/nothings/stb)

[PDFium](https://pdfium.googlesource.com/pdfium/)


License
-------

This library is licensed under the [zlib license](LICENSE.txt)
