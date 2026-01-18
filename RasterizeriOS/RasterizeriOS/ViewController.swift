//
//  ViewController.swift
//  RasterizeriOS
//
//  Created by Nigel Barber on 17/01/2026.
//

import UIKit
import RasterizerObjC

class ViewController: UIViewController, RASceneListDelegate {
    func shouldRedraw(atTime time: Double, width: Double, height: Double) -> Bool {
        true
    }
    
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        let red = RAPaint(red: 1, green: 0, blue: 0, alpha: 1)
        let b = CGRect(x: 0, y: 0, width: width, height: height)
        let scene = RAScene()
        scene.addFill(RAPath(ellipse: b), ctm: .identity, color: red, evenOdd: false)
        return RASceneList(scene: scene)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        
        if let view = self.view as? RasterizerView {
            view.listDelegate = self
        }
    }
}
