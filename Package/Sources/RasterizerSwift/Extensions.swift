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
    
    public func perimeter() -> CGFloat {
        2 * (width + height)
    }
    
    public func ellipsePerimeter() -> CGFloat {
        let a = 0.5 * width
        let b = 0.5 * height
        return CGFloat.pi * (3 * (a + b) - sqrt((3 * a + b) * (a + 3 * b)))
    }
}

// The circles of CounterRotatingCircles at time, in a width x height view, mapped by transform. Fill it even-odd
public func CounterRotatingCirclesPath(_ time: Double, width: Double, height: Double, transform: CGAffineTransform = .identity) -> RAPath {
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
        path.addEllipse(CGRect(x: origin.x - radius, y: origin.y - radius, width: 2 * radius, height: 2 * radius), transform: transform)
    }
    return path
}

public func CounterRotatingCircles(_ time: Double, width: Double, height: Double) -> RASceneList {
    let scene = RAScene()
    scene.addFill(CounterRotatingCirclesPath(time, width: width, height: height), ctm: .identity, color: RAPaint(), evenOdd: true)
    return RASceneList(scene: scene)
}

extension RAPath {
    // The 4 cubics of Ra::Geometry::addEllipse, with their control points mapped by transform, which maps them exactly
    public func addEllipse(_ rect: CGRect, transform: CGAffineTransform) {
        guard rect.width > 0, rect.height > 0 else {
            return
        }
        let t = 0.5 - 2.0 / 3.0 * (sqrt(2.0) - 1), s = 1 - t
        let lx = rect.minX, ly = rect.minY, ux = rect.maxX, uy = rect.maxY, mx = rect.midX, my = rect.midY
        func p(_ x: Double, _ y: Double) -> CGPoint {
            CGPoint(x: x, y: y).applying(transform)
        }
        let p0 = p(ux, my)
        move(to: p0.x, y: p0.y)
        for (c1, c2, e) in [(p(ux, t * ly + s * uy), p(t * lx + s * ux, uy), p(mx, uy)),
                            (p(s * lx + t * ux, uy), p(lx, t * ly + s * uy), p(lx, my)),
                            (p(lx, s * ly + t * uy), p(s * lx + t * ux, ly), p(mx, ly)),
                            (p(t * lx + s * ux, ly), p(ux, s * ly + t * uy), p(ux, my))] {
            cubic(to: c1.x, y1: c1.y, x2: c2.x, y2: c2.y, x3: e.x, y3: e.y)
        }
        close()
    }
}

extension RAScene {
    public func fillRect(_ rect: CGRect, paint: RAPaint) {
        addFill(RAPath(rect: rect), ctm: .identity, color: paint, evenOdd: false)
    }
    
    public func strokeRect(_ rect: CGRect, width: Double, paint: RAPaint) {
        addStroke(RAPath(rect: rect), ctm: .identity, color: paint, width: width, capStyle: .capButt, joinStyle: .joinMiter)
    }
}
