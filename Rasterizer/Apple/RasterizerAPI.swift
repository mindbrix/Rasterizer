//
//  RasterizerAPI.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation

struct SwiftDemo {
    var test0: RasterizerSceneList {
        let dim = 100
        let rect = CGRect(x: 0, y: 0, width: dim, height: dim)
        let path = RasterizerPath()
        path.addEllipse(rect)
        
        let color = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let scene = RasterizerScene()
        scene.add(path, ctm: .identity, color: color, width: 0, flags: 0)
        
        let list = RasterizerSceneList()
        list.add(scene, ctm: .identity)
        list.ctm = CGAffineTransform(scaleX: 4, y: 4)
        
        return list
    }
}
