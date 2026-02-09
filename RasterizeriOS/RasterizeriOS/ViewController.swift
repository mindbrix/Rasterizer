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
    var ctm = CGAffineTransform.identity {
        didSet {
            redraw = true
        }
    }
    var down = CGAffineTransform.identity
    var redraw = false
    var svgIndex = 0
    let svgNames = ["Anime_Girl", "AntigenicShift_HiRes", "car", "contour", "drops", "hawaii",  "Manchester_Union_Democrat_office_1877", "paris-30k", "PToT_hi-res_source_nobackground", "reschart", "Sun_poster", "tiger"]
    var svgList: RASceneList? {
        didSet {
            if let svgList {
                ctm = view.bounds.fitTransform(b: svgList.bounds)
            } else {
                ctm = .identity
            }
        }
    }
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        if let view = self.view as? RasterizerView {
            view.listDelegate = self
            view.isUserInteractionEnabled = true
            view.addGestureRecognizer(UILongPressGestureRecognizer(target: self, action: #selector(onLongPress)))
            view.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(onTap)))
            view.addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(onGesture)))
            view.addGestureRecognizer(UIPinchGestureRecognizer(target: self, action: #selector(onGesture)))
            view.addGestureRecognizer(UIRotationGestureRecognizer(target: self, action: #selector(onGesture)))
        }
        svgList = makeSvgList()
    }
    
    override func didRotate(from fromInterfaceOrientation: UIInterfaceOrientation) {
        let list = svgList
        svgList = list
    }
                
    func makeSvgList() -> RASceneList? {
        guard let url = Bundle.main.url(forResource: svgNames[svgIndex], withExtension: "svg") else {
            return nil
        }
        let scene = RAScene()
        let ctm = scene.addSvg(from: url)
        let list = RASceneList()
        list.add(scene, ctm: ctm, clip: .zero)
        return list
    }

    @objc func onLongPress(_ recognizer: UILongPressGestureRecognizer) {
        svgList = nil
    }
        
    @objc func onTap(_ recognizer: UITapGestureRecognizer) {
        svgIndex = (svgIndex + 1) % svgNames.count
        svgList = makeSvgList()
    }
        
    @objc func onGesture(_ recognizer: UIGestureRecognizer) {
        switch recognizer.state {
        case .began:
            down = ctm
        case .changed:
            let cx = view.bounds.midX, cy = view.bounds.midY
            if let t = (recognizer as? UIPanGestureRecognizer)?.translation(in: view) {
                ctm = .init(a: down.a, b: down.b, c: down.c, d: down.d, tx: down.tx + t.x, ty: down.ty - t.y)
            } else if let s = (recognizer as? UIPinchGestureRecognizer)?.scale {
                ctm = down.concatAroundCenter(t: CGAffineTransform(scaleX: s, y: s), cx: cx, cy: cy)
            } else if let r = (recognizer as? UIRotationGestureRecognizer)?.rotation {
                ctm = down.concatAroundCenter(t: CGAffineTransform(rotationAngle: -r), cx: cx, cy: cy)
            }
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
