Rasterizer
========

Rasterizer is a GPU-accelerated 2D vector graphics engine designed for simplicity and portability. 

The current implementation targets macOS using C++ 11 and Metal, but it should work on any GPU that supports instancing and floating point render targets.


Architecture
--------

Path fills are rasterized in 2 stages: first to a float mask buffer, and then to the color buffer. Pixel area coverage is calculated using a novel windowed inverse-lerp algorithm that can be trivially extended for zero-cost box blurs.

Strokes are rasterized straight to the color buffer using GPU triangulation.

Quadratic Beziér curves are first-class GPU primitives, so no expensive, scale-variant path flattening is necessary.

The CPU stages make extensive use of highly-efficient and simple batch parallelism.


Building
--------

The Xcode demo app project builds out of the box as all dependencies are included. 


Demo App
-------

The demo app supports viewing SVG and PDF files, plus font grids.

![image](https://github.com/mindbrix/Rasterizer/blob/master/Screenshot.png)

Using
------
Path -> Scene -> SceneList


Credits
------

XXHash
NanoSVG
STB Truetype
PDFium


License
-------


