//
//  RasterizerAPI.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation

public class SwiftDemoView: RasterizerView {
    let demo = SwiftDemo()
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        listDelegate = demo
    }
}

class SwiftDemo: NSObject, ListDelegate {
    func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    func getListAtTime(_ time: Double, width: Float, height: Float) -> RasterizerSceneList! {
        test0(time: time)
    }
    
    func test0(time: Double) -> RasterizerSceneList {
        let t = time - floor(time)
        let dim = 100.0
        let rect = CGRect(x: 0, y: 0, width: dim, height: dim)
        let path = RasterizerPath()
        path.add(rect)
        path.addEllipse(rect)
        
        let tx = rect.midX
        let ty = rect.midY
        let rotation = CGAffineTransform(rotationAngle: t * 2 * Double.pi)
        let ctm = CGAffineTransform(translationX: -tx, y: -ty).concatenating(rotation).concatenating(CGAffineTransform(translationX: tx, y: ty))
        let color = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let scene = RasterizerScene()
        scene.add(path, ctm: ctm, color: color, width: 0, flags: SceneFlags.fillEvenOdd.rawValue)
        
        let list = RasterizerSceneList()
        list.add(scene, ctm: .identity)
        list.ctm = CGAffineTransform(scaleX: 4, y: 4)
        return list
    }
}
