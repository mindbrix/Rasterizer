//
//  Extensions.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 19/01/2026.
//

import Foundation
import RasterizerObjC

extension CGAffineTransform {
    public init(center: CGPoint, rotation: Double, scale: CGSize, translation: CGVector) {
        self = CGAffineTransform(translationX: -center.x, y: -center.y)
            .concatenating(CGAffineTransform(scaleX: scale.width, y: scale.height))
            .concatenating(CGAffineTransform(rotationAngle: rotation))
            .concatenating(CGAffineTransform(translationX: center.x + translation.dx, y: center.y + translation.dy))
    }
    
    public func concatAroundCenter(t: CGAffineTransform, cx: Double, cy: Double) -> CGAffineTransform {
        CGAffineTransform(a, b, c, d, tx - cx, ty - cy).concatenating(CGAffineTransform(t.a, t.b, t.c, t.d, t.tx + cx, t.ty + cy))
    }
}

extension CGPoint {
    public init(center: CGPoint, r: Double, theta: Double) {
        self = CGPoint(x: center.x + r * cos(theta), y: center.y + r * sin(theta))
    }
}

extension CGRect {
    public func fitTransform(b: CGRect) -> CGAffineTransform {
        guard !isNull && !isEmpty && !isInfinite else {
            return .identity
        }
        let w = width, h = height, bw = b.width, bh = b.height, s = min(w / bw, h / bh)
        return CGAffineTransform(
            a: s, b: 0,
            c: 0, d: s,
            tx: minX + 0.5 * (w - s * bw) - s * b.minX,
            ty: minY + 0.5 * (h - s * bh) - s * b.minY)
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
