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
    nonisolated public func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    nonisolated public func getList(_ width: Float, height: Float) -> RasterizerSceneList! {
       test0
    }
    
    var test0: RasterizerSceneList {
        let time = 0.1 * Date.timeIntervalSinceReferenceDate
        let t = time - floor(time)
        let dim = 100.0
        let inset = 0.1 * dim
        let rect = CGRect(x: 0, y: 0, width: dim, height: dim)
        let clip = rect.insetBy(dx: inset, dy: inset)
        let path = RasterizerPath()
        path.addEllipse(rect)
        
        let color = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let scene = RasterizerScene()
        scene.add(path, ctm: CGAffineTransform(rotationAngle: t * 2 * Double.pi), color: color, width: 0, flags: 0, clip: clip)
        
        let list = RasterizerSceneList()
        list.add(scene, ctm: .identity)
        list.ctm = CGAffineTransform(scaleX: 4, y: 4)
        return list
    }
}
