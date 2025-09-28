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
            path: "Sources/RasterizerCpp",
            exclude: [
                "exclude"
            ]
        ),
        .target(
            name: "RasterizerObjC",
            dependencies: ["RasterizerCpp"],
            path: "Sources/RasterizerObjC",
            cxxSettings: [
                .headerSearchPath("private"),
            ],
            linkerSettings: [
                // Frameworks
                .linkedFramework("Accelerate"),
                // Libraries
            ]
        ),
        .testTarget(
            name: "RasterizerSwiftTests",
            dependencies: ["RasterizerObjC"]
        ),
    ],
    cxxLanguageStandard: .cxx11
)
