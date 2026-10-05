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
    let testSvgNames = ["tiger", "car", "drops", "hawaii", "reschart", "paris-30k"]
    let freshSvgNames = ["Anime_Girl", "AntigenicShift_HiRes", "contour", "Manchester_Union_Democrat_office_1877", "PToT_hi-res_source_nobackground", "Sun_poster"]
    var mutationTest: MutationTest? {
        didSet {
            ctm = .identity
            testButton?.setTitle(mutationTest == nil ? "Run Tests" : "Stop Tests", for: .normal)
            statusLabel?.isHidden = mutationTest == nil
        }
    }
    weak var testButton: UIButton?
    weak var statusLabel: UILabel?

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
        addTestControls()
    }

    func addTestControls() {
        let button = UIButton(type: .system)
        button.translatesAutoresizingMaskIntoConstraints = false
        button.setTitleColor(.white, for: .normal)
        button.backgroundColor = UIColor.black.withAlphaComponent(0.55)
        button.layer.cornerRadius = 8
        button.contentEdgeInsets = UIEdgeInsets(top: 6, left: 12, bottom: 6, right: 12)
        button.setTitle("Run Tests", for: .normal)
        button.addTarget(self, action: #selector(onToggleTests), for: .touchUpInside)
        view.addSubview(button)

        let label = UILabel()
        label.translatesAutoresizingMaskIntoConstraints = false
        label.font = .monospacedSystemFont(ofSize: 12, weight: .medium)
        label.textColor = .white
        label.backgroundColor = UIColor.black.withAlphaComponent(0.55)
        label.numberOfLines = 0
        label.isHidden = true
        view.addSubview(label)

        NSLayoutConstraint.activate([
            button.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 8),
            button.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -8),
            label.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 8),
            label.trailingAnchor.constraint(lessThanOrEqualTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -8),
            label.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -8)
        ])
        testButton = button
        statusLabel = label
    }

    @objc func onToggleTests() {
        if mutationTest == nil {
            let urls = testSvgNames.compactMap { Bundle.main.url(forResource: $0, withExtension: "svg") }
            let freshUrls = freshSvgNames.compactMap { Bundle.main.url(forResource: $0, withExtension: "svg") }
            mutationTest = MutationTest(urls: urls, freshUrls: freshUrls, in: view.bounds)
        } else {
            mutationTest = nil
            svgList = makeSvgList()
        }
        redraw = true
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
        guard mutationTest == nil else { return }
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
        redraw = redraw || svgList == nil || mutationTest != nil
        return redraw
    }

    func getListAtTime(_ time: Double, scale: Double, width: Double, height: Double) -> RASceneList {
        redraw = false;
        if let mutationTest {
            let t0 = CACurrentMediaTime()
            mutationTest.step()
            mutationTest.recordFrame(at: time, stepTime: CACurrentMediaTime() - t0)
            let status = mutationTest.status
            DispatchQueue.main.async { [weak self] in
                self?.statusLabel?.text = status
            }
            mutationTest.list.ctm = ctm
            return mutationTest.list
        }
        let list = svgList ?? CounterRotatingCircles(time, width: width, height: height)
        list.ctm = ctm
        return list
    }
}


// MARK: - Mutation tests

// Exercises incremental scene preparation: loads several SVGs, each into its own scene, then every frame applies a batch of
// random in-place mutations, validating every scene's incremental state every validateEvery frames. The phases cycle so each
// kind of edit is isolated before they are mixed, and each phase starts from copies of the SVGs, which are parsed only once.
// Each phase also introduces a scene of new geometry, derived in one large prepare, and logs that frame. Its SVG is parsed
// beforehand & never prepared, so its geometry is new to the cache each time it is introduced.
// The generator is seeded, so a failing run can be replayed.
final class MutationTest {
    enum Phase: Int, CaseIterable {
        case transforms, colors, paths, hideAndAppend, mixed
    }
    struct Item {
        let name: String
        let scene: RAScene
        let bounds: CGRect
        let shapes: [RAPath]
    }
    private(set) var list = RASceneList()
    let framesPerPhase = 240
    let mutationsPerFrame = 64
    let validateEvery = 10
    let urls: [URL]
    private var freshSources: [(name: String, scene: RAScene, ctm: CGAffineTransform)] = []    // Introduced in turn, one per phase
    let rect: CGRect
    private(set) var items: [Item] = []
    private var pristine: [(url: URL, scene: RAScene)] = []
    private(set) var frame = 0
    private(set) var mutations = 0
    private(set) var failures: [String] = []
    private(set) var stalls: [String] = []
    private var stallCount = 0, stallsWithGrows = 0
    private var lastTime: Double?
    private var mutateTime = 0.0, validateTime = 0.0, worstInterval = 0.0, worstStall = ""
    private var lastStatistics: [String: NSNumber] = [:]
    private var freshScene: (name: String, draws: Int, frame: Int)?
    private(set) var freshScenes: [String] = []
    private var rng: SplitMix64

    var phase: Phase {
        Phase.allCases[(frame / framesPerPhase) % Phase.allCases.count]
    }
    var status: String {
        let draws = items.map { "\($0.name) \($0.scene.count)" }.joined(separator: "  ")
        let statistics = RAScene.cacheStatistics()
        let mb = { (key: String) in String(format: "%.1f", (statistics[key]?.doubleValue ?? 0) / 1e6) }
        let count = { (key: String) in statistics[key]?.intValue ?? 0 }
        let storage = { (name: String) in
            "\(name) cap \(mb(name + "Capacity")) end \(mb(name + "End")) used \(mb(name + "Used")) waste \(mb(name + "Waste")) free \(mb(name + "Free")) MB, \(count(name + "Grows")) grows"
        }
        var lines = ["phase \(phase)  frame \(frame)  mutations \(mutations)  failures \(failures.count)", draws,
                     "cache \(count("entries")) entries, retiring \(mb("retiringBytes")) MB", storage("p16"), storage("outline"),
                     "stalls \(stallCount)  with storage grows \(stallsWithGrows)",
                     String(format: "per frame: mutate %.1f ms  prepare & validate %.1f ms", 1000 * mutateTime / Double(max(frame, 1)), 1000 * validateTime / Double(max(frame, 1))),
                     "worst \(worstStall)"]
        lines += freshScenes.suffix(2)
        lines += stalls.suffix(3)
        lines += failures.prefix(3)
        return lines.joined(separator: "\n")
    }

    init(urls: [URL], freshUrls: [URL], in rect: CGRect, seed: UInt64 = 1) {
        self.urls = urls
        self.rect = rect
        rng = SplitMix64(seed: seed)
        for url in urls {
            let scene = RAScene()
            pristine.append((url, scene))
            svgCtms.append(scene.addSvg(from: url))
            _ = scene.validate()    // Prepares, so each phase's copies carry its cache state
        }
        for url in freshUrls {
            let scene = RAScene(), ctm = scene.addSvg(from: url)
            freshSources.append((url.deletingPathExtension().lastPathComponent, scene, ctm))
        }
        load()
    }
    private var svgCtms: [CGAffineTransform] = []

    private func load() {
        list = RASceneList()
        items = []
        let cells = urls.count + (freshSources.isEmpty ? 0 : 1)
        let cols = max(1, Int(ceil(sqrt(Double(cells))))), rows = max(1, (cells + cols - 1) / cols)
        let cw = rect.width / Double(cols), ch = rect.height / Double(rows)
        let add = { (k: Int, name: String, scene: RAScene, svgCtm: CGAffineTransform) in
            let bounds = scene.bounds
            guard scene.count > 0, !bounds.isEmpty else { return }
            let cell = CGRect(x: self.rect.minX + cw * Double(k % cols), y: self.rect.minY + ch * Double(k / cols), width: cw, height: ch).insetBy(dx: 4, dy: 4)
            self.list.add(scene, ctm: svgCtm.concatenating(cell.fitTransform(b: bounds.applying(svgCtm))), clip: .zero)
            self.items.append(Item(name: name, scene: scene, bounds: bounds, shapes: self.makeShapes(in: bounds)))
        }
        for (k, (url, source)) in pristine.enumerated() {
            let scene = RAScene()
            scene.addDraws(from: source)
            add(k, url.deletingPathExtension().lastPathComponent, scene, svgCtms[k])
        }
        guard !freshSources.isEmpty else { return }
        let fresh = freshSources[(frame / framesPerPhase) % freshSources.count], scene = RAScene()
        scene.addDraws(from: fresh.scene)
        freshScene = (fresh.name, Int(scene.count), frame)
        add(urls.count, fresh.name, scene, fresh.ctm)
    }

    func step() {
        if frame > 0 && frame % framesPerPhase == 0 {
            load()
        }
        let t0 = CACurrentMediaTime()
        for _ in 0 ..< mutationsPerFrame {
            let kind = phase == .mixed ? Phase.allCases[rng.int(Phase.allCases.count - 1)] : phase
            mutate(kind)
        }
        frame += 1
        let t1 = CACurrentMediaTime()
        mutateTime += t1 - t0
        defer { validateTime += CACurrentMediaTime() - t1 }
        guard frame % validateEvery == 0 else { return }
        for item in items {
            if let error = item.scene.validate() {
                fail("\(item.name) frame \(frame) \(phase): \(error)")
            }
        }
        if let error = RAScene.validateCache() {
            fail("cache frame \(frame) \(phase): \(error)")
        }
    }

    // Logs frames over 25 ms with the time spent mutating, preparing & validating, and whether the cache's storage grew
    func recordFrame(at time: Double, stepTime: Double) {
        let statistics = RAScene.cacheStatistics()
        let delta = { (key: String) in (statistics[key]?.intValue ?? 0) - (self.lastStatistics[key]?.intValue ?? 0) }
        if let lastTime, time - lastTime > 0.025 {
            let grows = delta("p16Grows") + delta("outlineGrows")
            stallCount += 1
            stallsWithGrows += grows > 0 ? 1 : 0
            let stall = String(format: "stall frame %d %@: %.0f ms, step %.0f ms, storage grows %d",
                               frame, "\(phase)", 1000 * (time - lastTime), 1000 * stepTime,
                               grows)
            print("MutationTest \(stall)")
            stalls.append(stall)
            if time - lastTime > worstInterval {
                worstInterval = time - lastTime
                worstStall = stall
            }
        }
        // The fresh scene is prepared when the frame that loaded it renders, so its cost lands in the following frame's interval
        if let freshScene, frame == freshScene.frame + 2, let lastTime {
            let line = String(format: "new scene %@ (%d draws) frame %d: frame %.0f ms, storage grows %d",
                              freshScene.name, freshScene.draws, frame, 1000 * (time - lastTime),
                              delta("p16Grows") + delta("outlineGrows"))
            print("MutationTest \(line)")
            freshScenes.append(line)
        }
        lastTime = time
        lastStatistics = statistics
    }

    private func fail(_ failure: String) {
        print("MutationTest FAILED: \(failure)")
        failures.append(failure)
    }

    private func mutate(_ kind: Phase) {
        guard !items.isEmpty else { return }
        let item = items[rng.int(items.count)], scene = item.scene, n = Int(scene.count)
        mutations += 1
        guard n > 0 else { return }
        let i = UInt(rng.int(n))
        switch kind {
        case .transforms:
            // SVG draws have identity ctms, so jitter around the origin, occasionally restoring it
            let d = 0.02 * max(item.bounds.width, item.bounds.height)
            let ctm = rng.int(8) == 0 ? .identity : CGAffineTransform(translationX: d * (rng.unit() - 0.5), y: d * (rng.unit() - 0.5))
            let draw = scene.draw(at: i)
            draw.ctm = ctm
            scene.setDraw(draw, at: i)
        case .colors:
            // Transparent paints make draws invalid, which releases and later re-acquires their P16 entries
            let draw = scene.draw(at: i)
            draw.color = RAPaint(hue: rng.unit(), saturation: 0.7, value: 0.9, alpha: rng.int(8) == 0 ? 0 : 1)
            scene.setDraw(draw, at: i)
        case .paths:
            // A small shared pool, so many draws share and release the same geometries
            let draw = scene.draw(at: i)
            draw.path = item.shapes[rng.int(item.shapes.count)]
            scene.setDraw(draw, at: i)
        case .hideAndAppend:
            // Scenes only grow, so draws are hidden & shown instead of removed. Hiding releases cache references, and a
            // large range occasionally retires many entries at once, exercising free range reuse.
            if rng.int(2) == 0 {
                let length = rng.int(256) == 0 ? (n - Int(i)) / 2 + 1 : min(n - Int(i), 1 + rng.int(8)), hidden = rng.int(2) == 0
                for k in Int(i) ..< Int(i) + length {
                    let draw = scene.draw(at: UInt(k))
                    draw.hidden = hidden
                    scene.setDraw(draw, at: UInt(k))
                }
            } else {
                let color = RAPaint(hue: rng.unit(), saturation: 0.7, value: 0.9, alpha: 1)
                scene.addFill(item.shapes[rng.int(item.shapes.count)], ctm: .identity, color: color, evenOdd: false)
            }
        case .mixed:
            break
        }
    }

    private func makeShapes(in bounds: CGRect) -> [RAPath] {
        (0 ..< 12).map { k in
            let size = (0.02 + 0.08 * rng.unit()) * max(bounds.width, bounds.height)
            let rect = CGRect(x: bounds.minX + rng.unit() * (bounds.width - size), y: bounds.minY + rng.unit() * (bounds.height - size),
                              width: size, height: size)
            switch k % 4 {
            case 0: return RAPath(ellipse: rect)
            case 1: return RAPath(rect: rect)
            case 2: return RAPath(roundedRect: rect, cornerWidth: 0.2 * size, cornerHeight: 0.2 * size)
            default:
                let star = RAPath(), points = 5
                for p in 0 ..< 2 * points {
                    let r = (p % 2 == 0 ? 0.5 : 0.2) * size, theta = Double(p) * Double.pi / Double(points)
                    let x = rect.midX + r * sin(theta), y = rect.midY + r * cos(theta)
                    if p == 0 {
                        star.move(to: x, y: y)
                    } else {
                        star.line(to: x, y: y)
                    }
                }
                star.close()
                return star
            }
        }
    }
}

// Deterministic random numbers for repeatable test runs
struct SplitMix64 {
    private var state: UInt64
    init(seed: UInt64) {
        state = seed
    }
    mutating func next() -> UInt64 {
        state &+= 0x9E3779B97F4A7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
        z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
        return z ^ (z >> 31)
    }
    mutating func int(_ n: Int) -> Int {
        Int(next() % UInt64(max(n, 1)))
    }
    mutating func unit() -> Double {
        Double(next() >> 11) / Double(1 << 53)
    }
}
