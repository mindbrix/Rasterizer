//
//  Extensions.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 19/01/2026.
//

import Foundation
import RasterizerObjC

extension CGPoint {
    init(center: CGPoint, r: Double, theta: Double) {
        self = CGPoint(x: center.x + r * cos(theta), y: center.y + r * sin(theta))
    }
}

public func CounterRotatingCircles(_ time: Double, width: Double, height: Double) -> RASceneList {
    let scene = RAScene()
    let count = 60
    let dim = min(width, height)
    let radius = 0.25 * dim
    let center = CGPoint(x: 0.5 * width, y: 0.5 * height)
    let path = RAPath()
    for i in 0 ..< count {
        let ti = Double(i) / Double(count)
        let ts = 2 * time / Double(count) + ti
        let t = ts - floor(ts)
        let origin = CGPoint(center: center, r: radius, theta: (i % 2 == 0 ? 1 : -1) * t * 2 * Double.pi)
        path.addEllipse(CGRect(x: origin.x - radius, y: origin.y - radius, width: 2 * radius, height: 2 * radius))
    }
    scene.addFill(path, ctm: .identity, color: RAPaint(), evenOdd: true)
    return RASceneList(scene: scene)
}
