//
//  SwiftDemoView.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation


extension CGAffineTransform {
    init(center: CGPoint, rotation: Double, scale: CGSize, translation: CGVector) {
        self = CGAffineTransform(translationX: -center.x, y: -center.y)
            .concatenating(CGAffineTransform(scaleX: scale.width, y: scale.height))
            .concatenating(CGAffineTransform(rotationAngle: rotation))
            .concatenating(CGAffineTransform(translationX: center.x + translation.dx, y: center.y + translation.dy))
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
}

class SwiftDemo: NSObject, RASceneListDelegate {
    let font = RAFont(name: "HelveticaNeue-Medium")
    
    func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList! {
        test0(time, width: width, height: height)
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
        let count = 200
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
                flags: RASceneFlags.fillEvenOdd.rawValue
            )
        }
        
        let textCTM = CGAffineTransform(
            center: .zero,
            rotation: t * 2 * Double.pi,
            scale: CGSize(width: 1, height: 1),
            translation: CGVector(dx: r, dy: r)
        )
        scene.addText("Hello\n124", font: font, pointSize: 32, ctm: textCTM, color: CGColor(gray: 0, alpha: 1))
        
        let ctm = CGAffineTransform(
            center: center,
            rotation: 0,
            scale: CGSize(width: 1, height: 1),
            translation: .zero
        )
        let list = RASceneList()
        list.add(scene,
            ctm: ctm
        )
        return list
    }
}
