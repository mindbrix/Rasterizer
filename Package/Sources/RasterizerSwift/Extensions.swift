//
//  Extensions.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 13/12/2025.
//

import CoreGraphics
import RasterizerObjC


extension RAScene {
    func addRect(_ rect: CGRect, ctm: CGAffineTransform, width: Double, color: RAPaint) {
        let path = RAPath(rect: rect)
        path.close()
        addStroke(path, ctm: ctm, color: color, width: width, capStyle: .capButt, joinStyle: .joinMiter)
    }
}


extension CGAffineTransform {
    init(center: CGPoint, rotation: Double, scale: CGSize, translation: CGVector) {
        self = CGAffineTransform(translationX: -center.x, y: -center.y)
            .concatenating(CGAffineTransform(scaleX: scale.width, y: scale.height))
            .concatenating(CGAffineTransform(rotationAngle: rotation))
            .concatenating(CGAffineTransform(translationX: center.x + translation.dx, y: center.y + translation.dy))
    }
    
    func concatAroundCenter(t: CGAffineTransform, cx: Double, cy: Double) -> CGAffineTransform {
        CGAffineTransform(a, b, c, d, tx - cx, ty - cy).concatenating(CGAffineTransform(t.a, t.b, t.c, t.d, t.tx + cx, t.ty + cy))
    }
}


extension CGPoint {
    init(center: CGPoint, r: Double, theta: Double) {
        self = CGPoint(x: center.x + r * cos(theta), y: center.y + r * sin(theta))
    }
}

extension CGRect {
    static func boundsForIndex(_ bounds: CGRect, index: Int, count: Int) -> CGRect {
        let count = Double(count)
        let height = bounds.height / Double(count)
        return CGRect(x: bounds.origin.x,
                      y: bounds.origin.y + (count - 1 - Double(index)) * height,
                      width: bounds.width,
                      height: height)
    }
    static func drawGridIn(_ bounds: CGRect, count: Int, scene: RAScene) {
        for i in 0..<count {
            scene.addRect(boundsForIndex(bounds, index: i, count: count), ctm: .identity, width: 1, color: RAPaint())
        }
    }
}
 
