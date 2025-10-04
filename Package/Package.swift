// swift-tools-version: 6.0
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "RasterizerSwift",
    platforms: [
        .macOS(.v14)
        ],
    products: [
        // Products define the executables and libraries a package produces, making them visible to other packages.
        .library(
            name: "RasterizerCpp",
            targets: ["RasterizerCpp"]),
        .library(
            name: "RasterizerObjC",
            targets: ["RasterizerObjC"]),
    ],
    targets: [
        // Targets are the basic building blocks of a package, defining a module or a test suite.
        // Targets can depend on other targets in this package and products from dependencies.
        .target(
            name: "RasterizerCpp",
            dependencies: ["freetype", "pdfium"],
            path: "Sources/RasterizerCpp",
            exclude: [
                "exclude"
            ],
            cxxSettings: [
                .headerSearchPath("include"),
            ]
        ),
        .target(
            name: "RasterizerObjC",
            dependencies: ["RasterizerCpp"],
            path: "Sources/RasterizerObjC",
            resources: [
                .copy("../../Sources/RasterizerCpp/include/Shaders.metal")
            ],
            cxxSettings: [
                .headerSearchPath("private"),
            ],
            linkerSettings: [
                // Frameworks
                .linkedFramework("Accelerate"),
                // Libraries
            ]
        ),
        .binaryTarget(
            name: "freetype",
            path: "Sources/RasterizerCpp/exclude/freetype.xcframework"
        ),
        .binaryTarget(
            name: "pdfium",
            path: "Sources/RasterizerCpp/exclude/pdfium.xcframework"
        ),
        .testTarget(
            name: "RasterizerSwiftTests",
            dependencies: ["RasterizerObjC"]
        ),
    ],
    cxxLanguageStandard: .cxx11
)
