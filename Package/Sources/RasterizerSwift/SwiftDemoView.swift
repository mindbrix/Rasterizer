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
    override public func rotate(with event: NSEvent) {
        _ = demo.handleEvent(.rotate(angle: 0.1 * event.rotation))
    }
    override public func scrollWheel(with event: NSEvent) {
        let inversion = event.isDirectionInvertedFromDevice ? -1.0 : 1.0
        _ = demo.handleEvent(.translate(tx: event.deltaX, ty: inversion * event.deltaY))
    }
}


class SwiftDemo: NSObject, RASceneListDelegate {
    typealias ListClosure = (_ time: Double, _ width: Double, _ height: Double) -> RASceneList
    
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case magnify(scale: Double)
        case rotate(angle: Float)
        case translate(tx: Double, ty: Double)
    }
    
    lazy var closures: [ListClosure] = [{ [weak self] time, width, height in
        guard let self else {
            return RASceneList()
        }
        return self.test0(time, width: width, height: height)
    },
    { [weak self] time, width, height in
        guard let self else {
            return RASceneList()
        }
        return self.testQuadratics(time, width: width, height: height)
    },
    { [weak self] time, width, height in
        guard let self else {
            return RASceneList()
        }
        return self.testCubics(time, width: width, height: height)
    },
    ]
    
    var index = 0
    var flag = false
    var paused = false
    var useCurves = true
    var showOpaques = true
    var showOutlines = false
    var t = 0.0
    var ctm = CGAffineTransform.identity
    var bounds = CGRect.zero
    var pastedScene: RAScene?
    
    func handleEvent(_ event: Event) -> Bool {
        switch event {
        case .keyDown(let character, let flags):
            switch character {
            case "1"..."9":
                let i = Int(character.asciiValue ?? 0) - Int(Character("0").asciiValue ?? 0)
                let idx = max(0, i - 1)
                let limit = max(0, closures.count - 1)
                index = min(limit, idx)
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
        let list = closures[index](t, width, height)
        list.ctm = ctm
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
    
    func testQuadratics(_ time: Double, width: Double, height: Double) -> RASceneList {
        let path = RAPath()
        path.move(to: 0, y: 0)
        path.quad(to: 0.25 * width, y1: 0.5 * height, x2: width, y2: 0)
        
        let scene = RAScene()
        scene.add(path, ctm: .identity, color: CGColor(gray: 0, alpha: 1), width: (flag ? 1e-2 : 1e-1) * width, flags: 0, clip: .zero)
        
        let list = RASceneList()
        list.add(scene, ctm: .identity, clip: .zero)
        return list
    }
    func testCubics(_ time: Double, width: Double, height: Double) -> RASceneList {
        let count = 200
        let bounds = CGRect(x: 0, y: 0, width: width, height: height)
        let dim = min(bounds.width, bounds.height)
        let radius = 0.5 * dim
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        let path = RAPath()
        for i in 0 ..< count {
            let ti = Double(i) / Double(count)
            let origin = CGPoint(center: center, r: 0.5 * radius, theta: ti * 2 * Double.pi)
            if flag {
                path.add(CGRect(x: origin.x - radius, y: origin.y - radius, width: dim, height: dim))
            } else {
                path.addEllipse(CGRect(x: origin.x - radius, y: origin.y - radius, width: dim, height: dim))
            }
        }
        let scene = RAScene()
        scene.add(path, ctm: .identity, color: CGColor(gray: 0, alpha: 1), width: 0, flags: RASceneFlags.fillEvenOdd.rawValue, clip: .zero)

        let list = RASceneList()
        list.add(scene, ctm: .identity, clip: .zero)
        return list
    }
    
    func test0(_ time: Double, width: Double, height: Double) -> RASceneList {
        let ts = 0.1 * time
        let t = ts - floor(ts)
        let dim = min(width, height)
        let unitRect = CGRect(x: 0, y: 0, width: 1, height: 1)
        let unitCenter = CGPoint(x: unitRect.midX, y: unitRect.midY)
        let path = RAPath()
//        path.add(unitRect)
        path.addEllipse(unitRect)
        path.close()
        
        let scene = RAScene()
        let count = flag ? 2000 : 20
        let r = 0.5 * dim
        let center = CGPoint(x: r, y: r)
        let scale = 0.125 * dim
        for i in 0 ..< count {
            let ti = Double(i) / Double(count)
            let hsv = NSColor(hue: ti, saturation: 1, brightness: 1, alpha: 1).cgColor
            let radial = CGPoint(center: center, r: r, theta: ti * 2 * Double.pi)
            
            let ctm = CGAffineTransform(
                center: unitCenter,
                rotation: t * 2 * Double.pi,
                scale: CGSize(width: scale, height: scale),
                translation: CGVector(dx: radial.x - unitCenter.x, dy: radial.y - unitCenter.y)
            )
            scene.add(path,
                ctm: ctm,
                color: hsv,
                width: 0.1,
                flags: 0,
                clip: .zero
            )
        }
        let list = RASceneList()
        list.add(scene,
            ctm: .identity,
            clip: .zero
        )
        if let pasted = pastedScene {
            list.add(pasted,
                ctm: .identity,
                clip: .zero
            )
        }
        return list
    }
}
