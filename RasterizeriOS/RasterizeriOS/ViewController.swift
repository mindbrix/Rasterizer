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
            if svgList !== oldValue {
                (homes, cells, gridProgress, toGrid, animating) = ([], [], 0, false, false)
                gridButton.configuration?.title = "Grid"
                gridButton.isHidden = svgList == nil
            }
        }
    }
    var svgScene: RAScene?
    var svgCtm = CGAffineTransform.identity
    // The global clip: a long press toggles clipping every draw by the animated CounterRotatingCircles path, fixed to the view
    var clipping = false {
        didSet {
            redraw = true
        }
    }
    var clipped: RAScene?       // The scene whose draws hold the clip
    // The grid animation: each draw's original & grid cell transforms, & progress from 0 (original) to 1 (grid)
    let gridButton = UIButton(configuration: .filled())
    var homes: [CGAffineTransform] = [], cells: [CGAffineTransform] = []
    var gridProgress = 0.0, gridFrom = 0.0, gridStart = 0.0, toGrid = false, animating = false
    let gridDuration = 0.5
    
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
        gridButton.configuration?.title = "Grid"
        gridButton.configuration?.cornerStyle = .capsule
        gridButton.addTarget(self, action: #selector(onGrid), for: .touchUpInside)
        gridButton.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(gridButton)
        NSLayoutConstraint.activate([
            gridButton.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -16),
            gridButton.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -16),
        ])
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
        (svgScene, svgCtm) = (scene, ctm)
        return list
    }

    // Lays out a grid cell for each draw over the scene's bounds, & records each draw's original transform
    func makeGrid(_ scene: RAScene) {
        let count = scene.count, area = scene.bounds
        guard count > 0, !area.isNull, area.width > 0, area.height > 0 else {
            return
        }
        let cols = max(1, Int(ceil(sqrt(Double(count) * area.width / area.height))))
        let rows = (count + cols - 1) / cols
        let w = area.width / CGFloat(cols), h = area.height / CGFloat(rows)
        homes = Array(repeating: .identity, count: count)
        cells = homes
        scene.updateDraws(in: NSRange(location: 0, length: count)) { i, draw in
            homes[i] = draw.ctm
            cells[i] = draw.ctm
            let path = draw.path.bounds     // An empty path's bounds are infinite
            if !draw.hidden, path.width.isFinite, path.height.isFinite, path.width > 0 || path.height > 0 {
                let outset = 0.5 * max(0, draw.width)       // Positive stroke widths scale with ctm
                let cell = CGRect(x: area.minX + CGFloat(i % cols) * w, y: area.minY + CGFloat(i / cols) * h, width: w, height: h)
                cells[i] = cell.insetBy(dx: 0.05 * w, dy: 0.05 * h).fitTransform(b: path.insetBy(dx: -outset, dy: -outset))
            }
            return false
        }
    }

    @objc func onGrid() {
        guard let scene = svgScene else {
            return
        }
        if homes.isEmpty {
            makeGrid(scene)
        }
        toGrid.toggle()
        gridFrom = gridProgress
        gridStart = CACurrentMediaTime()
        animating = true
        gridButton.configuration?.title = toGrid ? "Restore" : "Grid"
    }

    // Moves the draws gridProgress of the way from their original transforms to their grid cells
    func stepGrid(_ time: Double) {
        guard animating, let scene = svgScene, !homes.isEmpty else {
            return
        }
        let t = min(1, max(0, (time - gridStart) / gridDuration)), eased = t * t * (3 - 2 * t)
        gridProgress = gridFrom + ((toGrid ? 1 : 0) - gridFrom) * eased
        animating = t < 1
        let homes = self.homes, cells = self.cells, u = gridProgress
        scene.updateDraws(in: NSRange(location: 0, length: homes.count)) { i, draw in
            draw.ctm = homes[i].interpolated(to: cells[i], by: u)
            return true
        }
    }

    // Sets every draw's clip path to the circles at time, mapped from the view to the scene, or clears it when clipping stops.
    // A clip path change needs no re-prepare, & the draws share one path, so it's one clip mask per frame. Grid steps only move
    // the draws' transforms, so the two animate together
    func stepClip(_ time: Double, width: Double, height: Double) {
        let scene = clipping && svgList != nil ? svgScene : nil
        if let old = clipped, old !== scene {
            old.updateDraws(in: NSRange(location: 0, length: old.count)) { _, draw in
                draw.clipPath = nil
                return true
            }
        }
        clipped = scene
        guard let scene else {
            return
        }
        let toScene = svgCtm.concatenating(ctm).inverted()
        let path = CounterRotatingCirclesPath(0.33 * time, width: width, height: height, transform: toScene)
        scene.updateDraws(in: NSRange(location: 0, length: scene.count)) { _, draw in
            draw.clipPath = path
            draw.clipEvenOdd = true     // The circles' overlaps are holes
            return true
        }
    }

    @objc func onLongPress(_ recognizer: UILongPressGestureRecognizer) {
        if recognizer.state == .began {
            clipping.toggle()
        }
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
    func shouldRedraw(atTime time: Double, scale: Double, width: Double, height: Double) -> Bool {
        redraw = redraw || svgList == nil || animating || clipping || clipped != nil
        return redraw
    }
    
    func getListAtTime(_ time: Double, scale: Double, width: Double, height: Double) -> RASceneList {
        redraw = false;
        stepGrid(time)
        stepClip(time, width: width, height: height)
        let list = svgList ?? CounterRotatingCircles(time, width: width, height: height)
        list.ctm = ctm
        list.useClips = gridProgress == 0 || clipped != nil     // Clip bounds don't move with the draws, but the global clip is fixed to the view
        list.clearColor = RAPaint(gray: 0.66, alpha: 1)
        return list
    }
}

private extension CGAffineTransform {
    func interpolated(to t: CGAffineTransform, by u: Double) -> CGAffineTransform {
        let v = 1 - u
        return CGAffineTransform(a: v * a + u * t.a, b: v * b + u * t.b, c: v * c + u * t.c, d: v * d + u * t.d, tx: v * tx + u * t.tx, ty: v * ty + u * t.ty)
    }
}
