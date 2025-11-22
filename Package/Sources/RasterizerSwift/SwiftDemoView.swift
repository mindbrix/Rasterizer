//
//  SwiftDemoView.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation
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

public class SwiftDemoView: RasterizerView {
    let demo = SwiftDemo()
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        listDelegate = demo
    }
    
    override public var acceptsFirstResponder: Bool {
        true
    }
    override public func becomeFirstResponder() -> Bool {
        true
    }
    override public func keyDown(with event: NSEvent) {
        guard let characters = event.characters?.first?.lowercased(), let character = characters.first else {
            return
        }
        demo.bounds = self.bounds
        if !demo.handleEvent(.keyDown(character: character, flags: event.modifierFlags)) {
            if character == "0" {
                useCG.toggle()
            } else {
                super.keyDown(with: event)
            }
        }
    }
    override public func magnify(with event: NSEvent) {
        _ = demo.handleEvent(.magnify(scale: 1.0 + event.magnification))
    }
    override public func mouseDragged(with event: NSEvent) {
        _ = demo.handleEvent(.translate(tx: event.deltaX, ty: -event.deltaY))
    }
    override public func rotate(with event: NSEvent) {
        _ = demo.handleEvent(.rotate(angle: 0.1 * event.rotation))
    }
    override public func scrollWheel(with event: NSEvent) {
        let inversion = event.isDirectionInvertedFromDevice ? -1.0 : 1.0
        _ = demo.handleEvent(.translate(tx: event.deltaX, ty: inversion * event.deltaY))
    }
}

protocol RADrawable {
    func getSceneAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RAScene
}

class TestDasher: RADrawable {
    func ellipsePerimeter(a: Double, b: Double) -> Double {
        .pi * (3 * (a + b) - sqrt((3 * a + b) * (a + 3 * b)))
    }
    func getSceneAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RAScene {
        let ts = time
        let tick = ts - floor(ts)
        let width = 10.0
        let b = bounds.insetBy(dx: 0.5 * width, dy: 0.5 * width)
        if b.width == 0 || b.height == 0 {
            return RAScene()
        }
        let path = RAPath()
        if state.useRect {
            path.add(b)
        } else {
            path.addEllipse(b)
        }
        let perimeter = state.useRect ? 2 * (bounds.width + bounds.height) : ellipsePerimeter(a: 0.5 * b.width, b: 0.5 * b.height)
        
        let capStyle: RACapStyle = state.flag ? .capRound : .capButt
        
        let length = perimeter / 60
        let capLen = capStyle == .capRound ? width : 1
        let l0 = max(0, 0.666 * length - capLen)
        let lengths = [l0 as NSNumber, length - l0 as NSNumber]
        let dashed = path.dashedCopy(withPhase: tick * length - 0.5 * capLen, lengths: lengths)
//        let cgDashed = path.dashedCGCopy(withPhase: tick * length, lengths: lengths)
//        let dashed = state.flag ? cgDashed : path.dashedCopy(withPhase: tick * length, lengths: lengths)
        
        let scene = RAScene()
        scene.addStroke(dashed, ctm: .identity, color: RAColor(), width: width, capStyle: capStyle, joinStyle: .joinRound, clip: .zero)
        return scene
    }
}
class TestGradients: RADrawable {
    static func gradientForBounds(_ bounds: CGRect, isRadial: Bool) -> RAColor {
        let colors: [RAColor] = [
            RAColor(red: 1, green: 0, blue: 0, alpha: 1),
            RAColor(red: 0, green: 1, blue: 0, alpha: 1),
            RAColor(red: 0, green: 0, blue: 1, alpha: 1)
        ]
        let locations: [NSNumber] = [ 0, 0.5, 1 ]
        if isRadial {
            let center = CGPoint(x: bounds.midX, y: bounds.midY)
            let radius = 0.5 * min(bounds.width, bounds.height)
            return RAColor(radialWith: colors, locations: locations, center: center, radius: radius)
        } else {
            let start = CGPoint(x: bounds.minX, y: bounds.minY)
            let end = CGPoint(x: bounds.maxX, y: bounds.minY)
            return RAColor(linearWith: colors, locations: locations, start: start, end: end)
        }
    }
    
    func getSceneAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RAScene {
        let gradient = Self.gradientForBounds(bounds, isRadial: !state.useRect)
        
        let path = RAPath()
        path.add(bounds);
        
        let scene = RAScene()
        scene.addFill(path, ctm: .identity, color: gradient, evenOdd: false, clip: .zero)
        return scene
    }
}

class TestQuadratics: RADrawable {
    func getSceneAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RAScene {
        let width = bounds.width
        let height = bounds.height
        let dim = min(width, height)
        let stroke = (state.flag ? 1e-2 : 1e-1) * dim
        let ts = 1 * time
        let t = ts - floor(ts)
        let sine = sin(t * 2 * Double.pi)
        let color = RAColor(gray: 0, alpha: 1)
        
        let path = RAPath()
        path.move(to: 0, y: 0)
        path.quad(to: 0.5 * dim + sine * dim, y1: dim, x2: dim, y2: 0)
        
        let scene = RAScene()
        
        scene.addStroke(path, ctm: .identity, color: color, width: stroke, capStyle: .capButt, joinStyle: .joinMiter, clip: .zero)
        return scene
    }
}

class TestCubics: RADrawable {
    func getSceneAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RAScene {
        let count = state.flag ? 72 : 36
        let dim = min(bounds.width, bounds.height)
        let radius = 0.25 * dim
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        let path = RAPath()
        for i in 0 ..< count {
            let ti = Double(i) / Double(count)
            let ts = 0.01 * time + ti
            let t = ts - floor(ts)
            let origin = CGPoint(center: center, r: radius, theta: (i % 2 == 0 ? 1 : -1) * t * 2 * Double.pi)
            if state.useRect {
                path.add(CGRect(x: origin.x - radius, y: origin.y - radius, width: 2 * radius, height: 2 * radius))
            } else {
                path.addEllipse(CGRect(x: origin.x - radius, y: origin.y - radius, width: 2 * radius, height: 2 * radius))
            }
        }
        let scene = RAScene()
        scene.addFill(path, ctm: .identity, color: RAColor(gray: 0, alpha: 1), evenOdd: true, clip: .zero)
        return scene
    }
}

class Test0: RADrawable {
    func getSceneAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RAScene {
        let ts = 0.1 * time
        let t = ts - floor(ts)
        let dim = min(bounds.width, bounds.height)
        let unitRect = CGRect(x: 0, y: 0, width: 1, height: 1)
        let unitCenter = CGPoint(x: unitRect.midX, y: unitRect.midY)
        let unitWidth = 0.0// 0.1
        let path = RAPath()
        if (state.useRect) {
            path.add(unitRect)
        } else {
            path.addEllipse(unitRect)
        }
        path.close()
        
        let scene = RAScene()
        let count = state.flag ? 2000 : 20
        let r1 = 0.0625 * dim
        let r0 = 0.5 * dim - r1 - unitWidth * r1
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        for i in 0 ..< count {
            let tl = ts + Double(i) / Double(count)
            let ti = tl - floor(tl)
            let colors: [RAColor] = [
                RAColor(hue: ti, saturation: 1, value: 1, alpha: 1),
                RAColor(gray: 0, alpha: 1)
            ]
            let locations: [NSNumber] = [ 0, 1 ]
            let gradient = RAColor(radialWith: colors, locations: locations, center: unitCenter, radius: 0.5)
            let radial = CGPoint(center: center, r: r0, theta: ti * 2 * Double.pi)
            
            let ctm = CGAffineTransform(
                center: unitCenter,
                rotation: -t * 2 * Double.pi,
                scale: CGSize(width: 2 * r1, height: 2 * r1),
                translation: CGVector(dx: radial.x - unitCenter.x, dy: radial.y - unitCenter.y)
            )
            scene.addFill(path, ctm: ctm, color: gradient, evenOdd: false, clip: .zero)
        }
        return scene
    }
}

class SwiftDemo: NSObject, RASceneListDelegate {
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case magnify(scale: Double)
        case rotate(angle: Float)
        case translate(tx: Double, ty: Double)
    }
    
    let drawables: [RADrawable] = [
        Test0(),
        TestQuadratics(),
        TestCubics(),
        TestGradients(),
        TestDasher()
    ]
    
    var index = 0
    var flag = false
    var paused = false
    var useCurves = true
    var showOpaques = true
    var showOutlines = false
    var useRect = false
    var t = 0.0
    var ctm = CGAffineTransform.identity
    var bounds = CGRect.zero
    var pastedScene: RAScene?
    
    func handleEvent(_ event: Event) -> Bool {
        switch event {
        case .keyDown(let character, let flags):
            switch character {
            case "1"..."9":
                guard drawables.count > 0 else {
                    break
                }
                let i = Int(character.asciiValue ?? 0) - Int(Character("1").asciiValue ?? 0)
                index = min(drawables.count - 1, i)
            case "a":
                if (flags.contains(.shift)) {
                    flag.toggle()
                }
            case "c":
                useCurves.toggle()
            case "f":
                ctm = .identity
            case "i":
                showOpaques.toggle()
            case "o":
                showOutlines.toggle()
            case "p":
                paused.toggle()
            case "r":
                useRect.toggle()
            case "v":
                if (flags.contains(.command)) {
                    let objects = NSPasteboard.general.readObjects(forClasses: [NSAttributedString.self])
                    if let attrString = objects?.first as? NSAttributedString {
                        let scene = RAScene()
                        scene.addText(attrString, in: bounds, ctm: .identity, clip: .zero)
                        pastedScene = scene
                    }
                }
            default:
                return false
            }
            break
        case .magnify(let scale):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(scaleX: scale, y: scale), cx: bounds.midX, cy: bounds.midY)
        case .rotate(let angle):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(rotationAngle: CGFloat(angle)), cx: bounds.midX, cy: bounds.midY)
        case .translate(let tx, let ty):
            ctm.tx += tx
            ctm.ty += ty
        }
        return true
    }
    
    func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList! {
        bounds = CGRect(x: 0, y: 0, width: width, height: height)
        t = paused ? t : time
        let list = RASceneList()
        let scene = drawables[index].getSceneAtTime(t, bounds: bounds, state: self)
        list.add(scene, ctm: .identity, clip: .zero)
        if let pasted = pastedScene {
            list.add(pasted,
                ctm: .identity,
                clip: .zero
            )
        }
        list.ctm = ctm
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
}
