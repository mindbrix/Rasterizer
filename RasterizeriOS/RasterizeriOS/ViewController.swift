//
//  ViewController.swift
//  RasterizeriOS
//
//  Created by Nigel Barber on 17/01/2026.
//

import UIKit
import RasterizerObjC
import RasterizerSwift

class ViewController: UIViewController, RASceneListDelegate {
    func shouldRedraw(atTime time: Double, width: Double, height: Double) -> Bool {
        true
    }
    
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        CounterRotatingCircles(time, width: width, height: height)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        
        if let view = self.view as? RasterizerView {
            view.listDelegate = self
        }
    }
}
