// swift-tools-version: 6.0
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "RasterizerSwift",
    platforms: [
        .macOS(.v12),
        .iOS(.v13)
    ],
    products: [
        // Products define the executables and libraries a package produces, making them visible to other packages.
        .library(
            name: "RasterizerCpp",
            targets: ["RasterizerCpp"]),
        .library(
            name: "RasterizerObjC",
            targets: ["RasterizerObjC"]),
        .library(
            name: "RasterizerSwift",
            targets: ["RasterizerSwift"]),
    ],
    dependencies: [],
    targets: [
        // Targets are the basic building blocks of a package, defining a module or a test suite.
        // Targets can depend on other targets in this package and products from dependencies.
        .target(
            name: "RasterizerCpp",
            path: "Sources/RasterizerCpp",
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
            linkerSettings: []
        ),
        .target(
            name: "RasterizerSwift",
            dependencies: ["RasterizerObjC"],
            path: "Sources/RasterizerSwift"
        ),
        .testTarget(
            name: "RasterizerSwiftTests",
            dependencies: ["RasterizerObjC"]
        ),
    ],
    cxxLanguageStandard: .cxx11
)
