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
    static let useSvg = true
    
    var redraw = false
    var ctm = CGAffineTransform.identity {
        didSet {
            redraw = true
        }
    }
    var down = CGAffineTransform.identity
    var svgList: RASceneList?
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        if let view = self.view as? RasterizerView {
            view.listDelegate = self
            view.isUserInteractionEnabled = true
            view.addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(wasPanned)))
            view.addGestureRecognizer(UIPinchGestureRecognizer(target: self, action: #selector(wasPinched)))
            view.addGestureRecognizer(UIRotationGestureRecognizer(target: self, action: #selector(wasRotated)))
        }
        
        if Self.useSvg,
            let url = Bundle.main.url(forResource: "Anime_Girl", withExtension: "svg") {
            let scene = RAScene()
            let ctm = scene.addSvg(from: url)
            let list = RASceneList()
            list.add(scene, ctm: ctm, clip: .zero)
            svgList = list
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
        let should = redraw || svgList == nil
        redraw = false
        return should
    }
    
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        let list = svgList ?? CounterRotatingCircles(time, width: width, height: height)
        list.ctm = ctm
        return list
    }
}
