//
//  Extensions.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 13/12/2025.
//

import CoreGraphics
import RasterizerObjC


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
    func withGutter() -> CGRect {
        let gutter = height
        return CGRect(
            x: minX, y: minY - gutter,
            width: width, height: height + gutter)
    }
}

extension NSMutableAttributedString {
    func appendString(_ string: String) -> NSRange {
        appendString(NSAttributedString(string: string))
    }
    
    func appendString(_ string: NSAttributedString) -> NSRange {
        let range = NSRange(location: length, length: string.length)
        append(string)
        return range
    }
}

extension RAScene {
    func strokeRect(_ rect: CGRect, width: Double, paint: RAPaint) {
        let path = RAPath(rect: rect)
        path.close()
        addStroke(path, ctm: .identity, color: paint, width: width, capStyle: .capButt, joinStyle: .joinMiter)
    }
}
