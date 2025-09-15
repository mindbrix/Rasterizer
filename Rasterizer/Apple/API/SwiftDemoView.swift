//
//  SwiftDemoView.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation


extension CGAffineTransform {
    init(rotation: Double, sx: Double, sy: Double, cx: Double, cy: Double) {
        self = CGAffineTransform(translationX: -cx, y: -cy)
            .concatenating(CGAffineTransform(rotationAngle: rotation))
            .concatenating(CGAffineTransform(scaleX: sx, y: sy))
            .concatenating(CGAffineTransform(translationX: cx, y: cy))
    }
}


public class SwiftDemoView: RasterizerView {
    let demo = SwiftDemo()
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        listDelegate = demo
    }
}

class SwiftDemo: NSObject, SceneListDelegate {
    func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RasterizerSceneList! {
        test0(time, width: width, height: height)
    }
    
    func test0(_ time: Double, width: Double, height: Double) -> RasterizerSceneList {
        let t = time - floor(time)
        let dim = min(width, height)
        let rect = CGRect(x: 0, y: 0, width: dim, height: dim)
        let path = RasterizerPath()
        path.add(rect)
        path.addEllipse(rect)
        
        let ctm = CGAffineTransform(
            rotation: t * 2 * Double.pi,
            sx: 0.5,
            sy: 0.5,
            cx: rect.midX,
            cy: rect.midY
        )
        let hsv = NSColor.init(hue: t, saturation: 1, brightness: 1, alpha: 1).cgColor
        let scene = RasterizerScene()
        scene.add(path,
            ctm: ctm,
            color: hsv,
            width: 0,
            flags: SceneFlags.fillEvenOdd.rawValue
        )
        let list = RasterizerSceneList()
        list.add(scene,
            ctm: .identity
        )
        return list
    }
}
