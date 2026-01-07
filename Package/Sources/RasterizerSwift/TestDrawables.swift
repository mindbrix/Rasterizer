//
//  TestDrawables.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 13/12/2025.
//

import Foundation
@preconcurrency import RasterizerObjC

protocol RADrawable {
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList
}

struct RAState {
    let flag, useRect: Bool
    let time: Double
}

class TestDispatch: RADrawable {
    static func writeScene(_ scene: RAScene, range: NSRange, bounds: CGRect, state: RAState) {
        let b = bounds.boundsInRange(range)
        let path = TestCubics.getPathAtTime(bounds: b, state: state)
        let color = RAPaint()
        
        scene.addFill(path, ctm: .identity, color: color, evenOdd: true)
    }
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let count = 100
        
        var scenes: [RAScene] = []
        for _ in 0..<count {
            scenes.append(RAScene())
        }
        let raState = RAState(flag: state.flag, useRect: state.useRect, time: time)
        DispatchQueue.concurrentPerform(iterations: count, execute: { [scenes] i in
            Self.writeScene(scenes[i], range: NSRange(location: i, length: count), bounds: bounds, state: raState)
        })
        let list = RASceneList()
        for scn in scenes {
            list.add(scn)
        }
        return list
//        let scene = RAScene()
//        for scn in scenes {
//            scene.add(scn)
//        }
//        return scene.createList()
    }
}

class TestImage: RADrawable {
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let scene = RAScene()
        if let image = NSImage(systemSymbolName: "airplane", accessibilityDescription: nil),
           let imageRef = image.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            let color = RAPaint(cgImage: imageRef)
            let rect = CGRect(x: 0, y: 0, width: image.size.width, height: image.size.height)
            let path = RAPath(rect: rect)
            scene.addFill(path, ctm: .identity, color: color, evenOdd: false)
        }
        return scene.createList()
    }
}
class TestDasher: RADrawable {
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let width = 10.0
        let b = bounds.insetBy(dx: 0.5 * width, dy: 0.5 * width)
        if b.width == 0 || b.height == 0 {
            return RASceneList()
        }
        let path = state.useRect ? RAPath(rect: b) : RAPath(ellipse: b)
        let perimeter = state.useRect ? b.perimeter() : b.ellipsePerimeter()
        let capStyle: RACapStyle = state.flag ? .capRound : .capButt
        let length = perimeter / 60
        let capLen = capStyle == .capRound ? width : 1
        let l0 = max(0, 0.666 * length - capLen)
        let lengths = [l0 as NSNumber, length - l0 as NSNumber]
        let dashed = path.dashedCopy(withPhase: time * length, lengths: lengths)
        
        let scene = RAScene()
        scene.addStroke(dashed,
            ctm: .identity,
            color: RAPaint(),
            width: width,
            capStyle: capStyle,
            joinStyle: .joinRound)
        return scene.createList()
    }
}
class TestGradients: RADrawable {
    static func gradientForBounds(_ bounds: CGRect, isRadial: Bool) -> RAPaint {
        let colors: [RAPaint] = [
            RAPaint(red: 1, green: 0, blue: 0, alpha: 1),
            RAPaint(red: 0, green: 1, blue: 0, alpha: 1),
            RAPaint(red: 0, green: 0, blue: 1, alpha: 1)
        ]
        let locations: [NSNumber] = [ 0, 0.5, 1 ]
        if isRadial {
            let center = CGPoint(x: bounds.midX, y: bounds.midY)
            let radius = 0.5 * min(bounds.width, bounds.height)
            return RAPaint(radial: colors, locations: locations, center: center, radius: radius)
        } else {
            let start = CGPoint(x: bounds.minX, y: bounds.minY)
            let end = CGPoint(x: bounds.maxX, y: bounds.minY)
            return RAPaint(linear: colors, locations: locations, start: start, end: end)
        }
    }
    
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let inset = 40.0
        let b = bounds.insetBy(dx: inset, dy: inset)
        let gradient = Self.gradientForBounds(bounds, isRadial: !state.useRect)
        
        let rect = RAPath(rect: bounds)
        let ellipse = RAPath(ellipse: bounds)
        
        let scene = RAScene()
        scene.addFill(rect, ctm: .identity, color: gradient, evenOdd: false, clip: .zero, clipPath: RAPath(rect: b))
        scene.addFill(ellipse, ctm: .identity, color: RAPaint(gray: 1, alpha: 1), evenOdd: false, clip: .zero, clipPath: RAPath(ellipse: b))
        
        return scene.createList()
    }
}

class TestQuadratics: RADrawable {
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let width = bounds.width
        let height = bounds.height
        let dim = min(width, height)
        let stroke = (state.flag ? 1e-2 : 1e-1) * dim
        let sine = cos(time * 2 * Double.pi)
        let color = RAPaint(gray: 0, alpha: 1)
        
        let path = RAPath()
        path.move(to: 0, y: 0)
        path.quad(to: 0.5 * dim + sine * dim, y1: dim, x2: dim, y2: 0)
        
        let scene = RAScene()
        
        scene.addStroke(path, ctm: .identity, color: color, width: stroke, capStyle: .capButt, joinStyle: .joinMiter)
        return scene.createList()
    }
}

class TestCubics: RADrawable {
    static func getPathAtTime(bounds: CGRect, state: RAState) -> RAPath {
        let count = state.flag ? 72 : 36
        let dim = min(bounds.width, bounds.height)
        let radius = 0.25 * dim
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        let path = RAPath()
        for i in 0 ..< count {
            let ti = Double(i) / Double(count)
            let ts = 2 * state.time / Double(count) + ti
            let t = ts - floor(ts)
            let origin = CGPoint(center: center, r: radius, theta: (i % 2 == 0 ? 1 : -1) * t * 2 * Double.pi)
            if state.useRect {
                path.add(CGRect(x: origin.x - radius, y: origin.y - radius, width: 2 * radius, height: 2 * radius))
            } else {
                path.addEllipse(CGRect(x: origin.x - radius, y: origin.y - radius, width: 2 * radius, height: 2 * radius))
            }
        }
        return path
    }
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let path = Self.getPathAtTime(bounds: bounds, state: RAState(flag: state.flag, useRect: state.useRect, time: time))
        
        let scene = RAScene()
        scene.addFill(path, ctm: .identity, color: RAPaint(gray: 0, alpha: 1), evenOdd: true)
        return scene.createList()
    }
}

class Test0: RADrawable {
    let unitRectPath = RAPath(rect: CGRect(x: 0, y: 0, width: 1, height: 1))
    let unitEllipsePath = RAPath(ellipse: CGRect(x: 0, y: 0, width: 1, height: 1))
    
    func getListAtTime(_ time: Double, bounds: CGRect, state: SwiftDemo) -> RASceneList {
        let ts = 0.1 * time
        let dim = min(bounds.width, bounds.height)
        let unitRect = CGRect(x: 0, y: 0, width: 1, height: 1)
        let unitCenter = CGPoint(x: unitRect.midX, y: unitRect.midY)
        let unitWidth = 0.1
        
        let scene = RAScene()
        let count = state.flag ? 80 : 20
        let r1 = 0.0625 * dim
        let r0 = 0.5 * dim - r1 - unitWidth * r1
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        for i in 0 ..< count {
            let tl = ts + Double(i) / Double(count)
            let ti = tl - floor(tl)
            let colors: [RAPaint] = [
                RAPaint(hue: ti, saturation: 1, value: 1, alpha: 1),
                RAPaint(gray: 0, alpha: 1)
            ]
            let locations: [NSNumber] = [ 0, 1 ]
            let gradient = RAPaint(radial: colors, locations: locations, center: unitCenter, radius: 0.5)
            let radial = CGPoint(center: center, r: r0, theta: ti * 2 * Double.pi)
            
            let ctm = CGAffineTransform(
                center: unitCenter,
                rotation: -(1 - ti) * 2 * Double.pi,
                scale: CGSize(width: 2 * r1, height: 2 * r1),
                translation: CGVector(dx: radial.x - unitCenter.x, dy: radial.y - unitCenter.y)
            )
            let path = state.useRect ? unitRectPath : unitEllipsePath
            scene.addFill(path, ctm: ctm, color: gradient, evenOdd: false)
        }
        return scene.createList()
    }
}
