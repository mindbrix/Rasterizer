//
//  SwiftDemoView.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation


extension CGAffineTransform {
    init(rotation: Double, scale: Double, cx: Double, cy: Double) {
        self = CGAffineTransform(translationX: -cx, y: -cy)
            .concatenating(CGAffineTransform(rotationAngle: rotation))
            .concatenating(CGAffineTransform(scaleX: scale, y: scale))
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
            scale: 0.5,
            cx: rect.midX,
            cy: rect.midY
        )
        let color = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let scene = RasterizerScene()
        scene.add(path,
            ctm: ctm,
            color: color,
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
