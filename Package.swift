// swift-tools-version: 6.0
// The swift-tools-version declares the minimum version of Swift required to build this package.
//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

import PackageDescription

let package = Package(
    name: "RasterizerSwift",
    platforms: [
        .macOS(.v14),
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
            path: "Package/Sources/RasterizerCpp",
            cxxSettings: [
                .headerSearchPath("include"),
            ]
        ),
        .target(
            name: "RasterizerObjC",
            dependencies: ["RasterizerCpp"],
            path: "Package/Sources/RasterizerObjC",
            resources: [
                .process("../../Sources/RasterizerCpp/include/Shaders.metal")
            ],
            cxxSettings: [
                .headerSearchPath("private"),
            ],
            linkerSettings: []
        ),
        .target(
            name: "RasterizerSwift",
            dependencies: ["RasterizerObjC"],
            path: "Package/Sources/RasterizerSwift"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
