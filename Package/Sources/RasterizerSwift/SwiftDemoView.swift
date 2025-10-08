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
        demo.bounds = self.bounds
        if !demo.handleEvent(.keyDown(keyCode: event.keyCode, flags: event.modifierFlags)) {
            super.keyDown(with: event)
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
    enum KeyCode: UInt16 {
        case kA = 0
        case kC = 8
        case kF = 3
        case kG = 5
        case kH = 4
        case kV = 9
        //, kI = 34, kL = 37, kO = 31, kP = 35, kS = 1, kT = 17, k1 = 18, k0 = 29, kMinus = 27, kPlus = 24 }
    }
    enum Event {
        case keyDown(keyCode: UInt16, flags: NSEvent.ModifierFlags)
        case magnify(scale: Double)
        case rotate(angle: Float)
        case translate(tx: Double, ty: Double)
    }
    
    var flag = false
    var ctm = CGAffineTransform.identity
    var bounds = CGRect.zero
    var pastedScene: RAScene?
    
    func handleEvent(_ event: Event) -> Bool {
        switch event {
        case .keyDown(let keyCode, let flags):
            switch keyCode {
            case KeyCode.kA.rawValue:
                if (flags.contains(.shift)) {
                    flag.toggle()
                }
            case KeyCode.kC.rawValue:
                ctm = .identity
            case KeyCode.kV.rawValue:
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
        
        return test0(time, width: width, height: height)
    }
    
    func test0(_ time: Double, width: Double, height: Double) -> RASceneList {
        let ts = 0.1 * time
        let t = ts - floor(ts)
        let dim = min(width, height)
        let unitRect = CGRect(x: 0, y: 0, width: 1, height: 1)
        let unitCenter = CGPoint(x: unitRect.midX, y: unitRect.midY)
        let path = RAPath()
        path.add(unitRect)
//        path.addEllipse(unitRect)
        path.close()
        
        let scene = RAScene()
        let count = flag ? 2000 : 200
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
                flags: RASceneFlags.fillEvenOdd.rawValue,
                clip: .zero
            )
        }
        let list = RASceneList()
        list.add(scene,
            ctm: ctm,
            clip: .zero
        )
        if let pasted = pastedScene {
            list.add(pasted,
                ctm: ctm,
               clip: .zero
            )
        }
        return list
    }
}
