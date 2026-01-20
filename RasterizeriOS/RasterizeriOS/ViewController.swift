//
//  ViewController.swift
//  RasterizeriOS
//
//  Created by Nigel Barber on 17/01/2026.
//

import UIKit
import RasterizerObjC
import RasterizerSwift

class ViewController: UIViewController {
    var ctm = CGAffineTransform.identity
    var down = CGAffineTransform.identity
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        if let view = self.view as? RasterizerView {
            view.listDelegate = self
            view.isUserInteractionEnabled = true
            view.addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(wasPanned)))
            view.addGestureRecognizer(UIPinchGestureRecognizer(target: self, action: #selector(wasPinched)))
            view.addGestureRecognizer(UIRotationGestureRecognizer(target: self, action: #selector(wasRotated)))
        }
    }
    
    @objc func wasPanned(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            down = ctm
        case .changed:
            let tx = recognizer.translation(in: self.view)
            ctm = down
            ctm.tx += tx.x
            ctm.ty -= tx.y
        default:
            break
        }
    }
    
    @objc func wasPinched(_ recognizer: UIPinchGestureRecognizer) {
        switch recognizer.state {
        case .began:
            down = ctm
        case .changed:
            let s = recognizer.scale
            let cx = self.view.bounds.midX
            let cy = self.view.bounds.midY
            ctm = down.concatAroundCenter(t: CGAffineTransform(scaleX: s, y: s), cx: cx, cy: cy)
        default:
            break
        }
    }
    
    @objc func wasRotated(_ recognizer: UIRotationGestureRecognizer) {
        switch recognizer.state {
        case .began:
            down = ctm
        case .changed:
            let r = recognizer.rotation
            let cx = self.view.bounds.midX
            let cy = self.view.bounds.midY
            ctm = down.concatAroundCenter(t: CGAffineTransform(rotationAngle: -r), cx: cx, cy: cy)
        default:
            break
        }
    }
}

extension ViewController: RASceneListDelegate {
    func shouldRedraw(atTime time: Double, width: Double, height: Double) -> Bool {
        true
    }
    
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        let list = CounterRotatingCircles(time, width: width, height: height)
        list.ctm = ctm
        return list
    }
}
