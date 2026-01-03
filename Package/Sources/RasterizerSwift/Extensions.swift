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
    func snappedTo(lineHeight: CGFloat, in b: CGRect) -> CGRect {
        let ly = b.maxY - ceil((b.maxY - minY) / lineHeight) * lineHeight
        let uy = b.maxY - floor((b.maxY - maxY) / lineHeight) * lineHeight
        return CGRect(x: minX, y: ly, width: width, height: uy - ly)
    }
    func withGutter() -> CGRect {
        let gutter = height
        return CGRect(
            x: minX, y: minY - gutter,
            width: width, height: height + gutter)
    }
}

extension NSMutableAttributedString {
    func placeholderFor(_ value: Any) -> String? {
        if let _ = value as? Bool {
            "flag"
        } else if let _ = value as? Double {
            "slydeer"
        } else if let _ = value as? NSRange {
           "Rang"
        } else {
            nil
        }
    }
    
    func stringFor(_ value: Any) -> String? {
        if let slider = value as? Double {
            String(format: "%.2f", slider)
        } else if let range = value as? NSRange {
            String(range.location)
        } else if let convertible = value as? CustomStringConvertible {
            String(describing: convertible)
        } else {
            nil
        }
    }
    
    func styleFor(_ indent: Int, fontSize: Double) -> NSParagraphStyle {
        let style = NSMutableParagraphStyle()
        style.alignment = .left
        style.lineBreakMode = .byTruncatingTail
        style.tabStops = [
            NSTextTab(type: .leftTabStopType, location: CGFloat(indent + 0) * fontSize),
            NSTextTab(type: .rightTabStopType, location: CGFloat(indent + 7) * fontSize)
        ]
        return style
    }
    func appendString(_ string: String) -> NSRange {
        appendString(NSAttributedString(string: string))
    }
    
    func appendString(_ string: NSAttributedString) -> NSRange {
        let range = NSRange(location: length, length: string.length)
        append(string)
        return range
    }
    
    func appendKey(_ key: String, value: Any, taps: inout [(String, NSRange)]?, keyColor: CGColor, valueColor: CGColor, fontSize: Double, indent: Int = 0) -> NSRange {
        let mutable = taps != nil
        let style = styleFor(indent, fontSize: fontSize)
        let begin = length
        _ = appendString(String(repeating: "\t", count: min(1, indent)))
        addAttribute(.foregroundColor,
            value: keyColor,
            range: appendString("\(key):\t"))
        if let dict = value as? Store.DictType {
            _ = appendString("\n")
            addAttribute(.paragraphStyle, value: style, range: NSRange(location: begin, length: length - begin))
            
            for (subKey, value) in dict {
                _ = appendKey(subKey, value: value, taps: &taps, keyColor: keyColor, valueColor: valueColor, fontSize: fontSize, indent: indent + 1)
            }
        } else if let string = mutable ? placeholderFor(value) : stringFor(value) {
            if let _ = value as? NSRange {
                _ = appendString((stringFor(value) ?? "") + " ")
            }
            let range = appendString(string)
            addAttribute(.foregroundColor, value: valueColor, range: range)
            
            if mutable {
                taps?.append((key, range))
            }
            _ = appendString("\n")
            addAttribute(.paragraphStyle, value: style, range: NSRange(location: begin, length: length - begin))
        } else {
            _ = appendString("\n")
            addAttribute(.paragraphStyle, value: style, range: NSRange(location: begin, length: length - begin))

            if indent < 10 {
                let mirror = Mirror(reflecting: value)
                for child in mirror.children {
                    _ = appendKey(child.label ?? "", value: child.value, taps: &taps, keyColor: keyColor, valueColor: valueColor, fontSize: fontSize, indent: indent + 1)
                }
            }
        }
        return NSRange(location: begin, length: length - begin)
    }
}

extension Control: Hashable {
    static func == (lhs: Control, rhs: Control) -> Bool {
        lhs.hashValue == rhs.hashValue
    }
    func hash(into hasher: inout Hasher) {
        hasher.combine(key)
        hasher.combine(mode)
    }
}

extension CGRect: @retroactive Hashable {
    public func hash(into hasher: inout Hasher) {
        hasher.combine(minX)
        hasher.combine(minY)
        hasher.combine(width)
        hasher.combine(height)
    }
}

extension Hasher {
    mutating func combineMirror(_ mirror: Mirror, level: Int = 0) {
        for child in mirror.children {
            combine(child.label ?? "")
            combineValue(child.value, level: level)
        }
    }
    mutating func combineValue(_ value: Any, level: Int = 0) {
        if let dict = value as? Store.DictType {
            for (subKey, value) in dict {
                combine(subKey)
                combineValue(value, level: level + 1)
            }
        } else if let hashable = value as? any Hashable {
            combine(hashable)
        } else if let convertible = value as? CustomStringConvertible {
            combine(String(describing: convertible))
        } else {
            if level < 10 {
                combineMirror(Mirror(reflecting: value), level: level + 1)
            }
        }
    }
}

extension RAScene {
    func addControl(_ value: Any, in b: CGRect, paint: RAPaint, fontSize: Double) {
        if let flag = value as? Bool {
            addFlag(flag, in: b, paint: paint, fontSize: fontSize)
        } else if let slider = value as? Double {
            addSlider(slider, in: b, paint: paint, fontSize: fontSize)
        } else if let _ = value as? NSRange {
            addStepper(in: b, paint: paint, fontSize: fontSize)
        } else {
            fillRect(b, paint: paint)
        }
    }
    func addFlag(_ flag: Bool, in rect: CGRect, paint: RAPaint, fontSize: Double) {
        let white = RAPaint(gray: 1, alpha: 1)
        let gray = RAPaint(gray: 0.66, alpha: 1)
        let corner = 0.5 * min(rect.width, rect.height)
        let inset = fontSize / 12
        let innerRect = rect.insetBy(dx:inset, dy: inset)
        let radius = 0.5 * innerRect.height
        let cx = flag ? innerRect.maxX - radius : innerRect.minX + radius
        let b = CGRect(x: cx - radius, y: innerRect.midY - radius, width: 2 * radius, height: 2 * radius)
        let roundedRect = RAPath(roundedRect: rect, cornerWidth: corner, cornerHeight: corner)
        let ellipse = RAPath(ellipse: b)
        addFill(roundedRect, ctm: .identity, color: flag ? paint : gray, evenOdd: false)
        addFill(ellipse, ctm: .identity, color: white, evenOdd: false)
    }
    
    func addSlider(_ slider: Double, in rect: CGRect, paint: RAPaint, fontSize: Double) {
        let gray = RAPaint(gray: 0.66, alpha: 1)
        let b = rect.insetBy(dx: fontSize / 4, dy: 0)
        addSliderTrack(in: b, paint: gray, fontSize: fontSize)
        addSliderThumb(slider, in: b, paint: paint, fontSize: fontSize)
    }
    func addSliderThumb(_ slider: Double, in rect: CGRect, paint: RAPaint, fontSize: Double) {
        let height = fontSize / 2
        let radius = 0.5 * height
        let b = CGRect(x: rect.minX - radius + slider * rect.width, y: rect.midY - radius, width: height, height: height)
        addFill(RAPath(ellipse: b), ctm: .identity, color: paint, evenOdd: false)
    }
    func addSliderTrack(in rect: CGRect, paint: RAPaint, fontSize: Double) {
        let height = fontSize / 12
        let corner = 0.5 * height
        let b = CGRect(x: rect.minX, y: rect.midY - corner, width: rect.width, height: height)
        let rounded = RAPath(roundedRect: b, cornerWidth: corner, cornerHeight: corner)
        addFill(rounded, ctm: .identity, color: paint, evenOdd: false)
    }
    
    func addStepper(in rect: CGRect, paint: RAPaint, fontSize: Double) {
        let gray = RAPaint(gray: 0.66, alpha: 1)
        let corner = fontSize / 6
        let width = fontSize / 16
        let inset = 0.5 * width
        let dimension = min(0.5 * rect.width, rect.height)
        let radius =  max(0.0, 0.5 * dimension - fontSize / 4)
        let x0 = 0.5 * (rect.minX + rect.midX)
        let x1 = 0.5 * (rect.midX + rect.maxX)
        
        let rounded = RAPath(roundedRect: rect.insetBy(dx: inset, dy: inset), cornerWidth: corner, cornerHeight: corner)
        rounded.close()
        rounded.move(to: rect.midX, y: rect.minY + corner)
        rounded.line(to: rect.midX, y: rect.maxY - corner)
        
        let glyphs = RAPath()
        glyphs.move(to: x0 - radius, y: rect.midY)
        glyphs.line(to: x0 + radius, y: rect.midY)
        glyphs.move(to: x1 - radius, y: rect.midY)
        glyphs.line(to: x1 + radius, y: rect.midY)
        glyphs.move(to: x1, y: rect.midY - radius)
        glyphs.line(to: x1, y: rect.midY + radius)
        
        addStroke(rounded, ctm: .identity, color: gray, width: width, capStyle: .capButt, joinStyle: .joinMiter)
        addStroke(glyphs, ctm: .identity, color: paint, width: width, capStyle: .capButt, joinStyle: .joinMiter)
    }
    
    func fillRect(_ rect: CGRect, paint: RAPaint) {
        addFill(RAPath(rect: rect), ctm: .identity, color: paint, evenOdd: false)
    }
    
    func strokeRect(_ rect: CGRect, width: Double, paint: RAPaint) {
        let path = RAPath(rect: rect)
        path.close()
        addStroke(path, ctm: .identity, color: paint, width: width, capStyle: .capButt, joinStyle: .joinMiter)
    }
}
