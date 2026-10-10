//
//  ViewController.swift
//  RasterizeriOS
//
//  Created by Nigel Barber on 17/01/2026.
//

import UIKit
import UniformTypeIdentifiers
import RasterizerObjC
import RasterizerSwift

// A document a tap shows: a bundled or imported SVG, or a PDF, whose pages the page stepper shows
struct Document {
    let url: URL
    let pageCount: Int?     // nil for an SVG

    init(url: URL) {
        self.url = url
        pageCount = UTType(filenameExtension: url.pathExtension)?.conforms(to: .pdf) == true ? max(1, RAScene.pdfPageCount(from: url)) : nil
    }
}

class ViewController: UIViewController {
    var ctm = CGAffineTransform.identity {
        didSet {
            redraw = true
        }
    }
    var down = CGAffineTransform.identity
    var redraw = false
    // The bundled SVGs, then the user's imported files, in the order they were imported, which taps step through
    let svgNames = ["Anime_Girl", "AntigenicShift_HiRes", "car", "contour", "drops", "hawaii",  "Manchester_Union_Democrat_office_1877", "paris-30k", "PToT_hi-res_source_nobackground", "reschart", "Sun_poster", "tiger"]
    var documents: [Document] = []
    var documentIndex = 0, pageIndex = 0
    var bundledCount = 0
    let openButton = UIButton(configuration: .filled())
    let removeButton = UIButton(configuration: .filled())
    // The PDF page stepper, shown for PDFs with more than one page
    let pageView = UIStackView(), pageLabel = UILabel(), pageStepper = UIStepper()
    var documentList: RASceneList? {
        didSet {
            if let documentList {
                ctm = view.bounds.fitTransform(b: documentList.bounds)
            } else {
                ctm = .identity
            }
            if documentList !== oldValue {
                (homes, cells, gridProgress, toGrid, animating) = ([], [], 0, false, false)
                setSymbol(gridButton, "square.grid.3x3", "Grid")
                gridButton.isHidden = documentList == nil
            }
            removeButton.isHidden = importedURL == nil
            let pageCount = documents.indices.contains(documentIndex) ? documents[documentIndex].pageCount ?? 0 : 0
            pageView.isHidden = pageCount < 2
            pageStepper.maximumValue = Double(max(1, pageCount - 1))
            pageStepper.value = Double(pageIndex)
            pageLabel.text = "\(pageIndex + 1) / \(pageCount)"
        }
    }
    var documentScene: RAScene?
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
            let tap = UITapGestureRecognizer(target: self, action: #selector(onTap))
            tap.delegate = self
            view.addGestureRecognizer(tap)
            view.addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(onGesture)))
            view.addGestureRecognizer(UIPinchGestureRecognizer(target: self, action: #selector(onGesture)))
            view.addGestureRecognizer(UIRotationGestureRecognizer(target: self, action: #selector(onGesture)))
        }
        for (button, symbol, label, action) in [(gridButton, "square.grid.3x3", "Grid", #selector(onGrid)), (openButton, "folder", "Open", #selector(onOpen)), (removeButton, "trash", "Remove", #selector(onRemove))] {
            setSymbol(button, symbol, label)
            button.configuration?.cornerStyle = .capsule
            button.addTarget(self, action: action, for: .touchUpInside)
            button.translatesAutoresizingMaskIntoConstraints = false
            view.addSubview(button)
        }
        removeButton.configuration?.baseBackgroundColor = .systemRed
        pageLabel.font = .monospacedDigitSystemFont(ofSize: UIFont.labelFontSize, weight: .medium)
        pageStepper.addTarget(self, action: #selector(onPage), for: .valueChanged)
        pageView.addArrangedSubview(pageLabel)
        pageView.addArrangedSubview(pageStepper)
        pageView.spacing = 8
        pageView.alignment = .center
        pageView.isLayoutMarginsRelativeArrangement = true
        pageView.directionalLayoutMargins = NSDirectionalEdgeInsets(top: 6, leading: 12, bottom: 6, trailing: 6)
        pageView.backgroundColor = .systemBackground.withAlphaComponent(0.85)
        pageView.layer.cornerRadius = 22
        pageView.layer.cornerCurve = .continuous
        pageView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(pageView)
        NSLayoutConstraint.activate([
            gridButton.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -16),
            gridButton.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -16),
            openButton.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 16),
            openButton.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -16),
            removeButton.leadingAnchor.constraint(equalTo: openButton.trailingAnchor, constant: 8),
            removeButton.bottomAnchor.constraint(equalTo: openButton.bottomAnchor),
            pageView.centerXAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerXAnchor).withPriority(.defaultHigh),
            pageView.leadingAnchor.constraint(greaterThanOrEqualTo: removeButton.trailingAnchor, constant: 8),     // Off centre on narrow iPhones
            pageView.centerYAnchor.constraint(equalTo: openButton.centerYAnchor),
            pageView.heightAnchor.constraint(equalToConstant: 44),
        ] + [gridButton, openButton, removeButton].flatMap { [     // Circles the stepper's height
            $0.heightAnchor.constraint(equalTo: pageView.heightAnchor),
            $0.widthAnchor.constraint(equalTo: $0.heightAnchor),
        ] })
        documents = svgNames.compactMap { Bundle.main.url(forResource: $0, withExtension: "svg") }.map(Document.init)
        bundledCount = documents.count
        documents += importedFiles().map(Document.init)
        documentList = makeDocumentList()
    }
    
    // The buttons show SF Symbols, which are compact enough for iPhone, with their names for VoiceOver
    func setSymbol(_ button: UIButton, _ symbol: String, _ label: String) {
        button.configuration?.image = UIImage(systemName: symbol)
        button.accessibilityLabel = label
    }

    override func didRotate(from fromInterfaceOrientation: UIInterfaceOrientation) {
        let list = documentList
        documentList = list
    }
                
    func makeDocumentList() -> RASceneList? {
        guard documents.indices.contains(documentIndex) else {
            return nil
        }
        let document = documents[documentIndex], scene = RAScene()
        let ctm = document.pageCount == nil ? scene.addSvg(from: document.url) : scene.addPdf(from: document.url, pageIndex: pageIndex)
        let list = RASceneList()
        list.add(scene, ctm: ctm, clip: .zero)
        documentScene = scene
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
        guard let scene = documentScene else {
            return
        }
        if homes.isEmpty {
            makeGrid(scene)
        }
        toGrid.toggle()
        gridFrom = gridProgress
        gridStart = CACurrentMediaTime()
        animating = true
        setSymbol(gridButton, toGrid ? "arrow.uturn.backward" : "square.grid.3x3", toGrid ? "Restore" : "Grid")
    }

    // Moves the draws gridProgress of the way from their original transforms to their grid cells
    func stepGrid(_ time: Double) {
        guard animating, let scene = documentScene, !homes.isEmpty else {
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

    @objc func onTap(_ recognizer: UITapGestureRecognizer) {
        documentIndex = documents.isEmpty ? 0 : (documentIndex + 1) % documents.count
        pageIndex = 0
        documentList = makeDocumentList()
    }

    @objc func onPage() {
        pageIndex = Int(pageStepper.value)
        documentList = makeDocumentList()
    }

    // The user's SVG & PDF files, copied into Documents/Imported, so they're kept between launches
    var importedDirectory: URL {
        URL.documentsDirectory.appending(path: "Imported", directoryHint: .isDirectory)
    }

    // The imported files, oldest first
    func importedFiles() -> [URL] {
        let files = (try? FileManager.default.contentsOfDirectory(at: importedDirectory, includingPropertiesForKeys: [.creationDateKey])) ?? []
        let created = { (url: URL) in (try? url.resourceValues(forKeys: [.creationDateKey]).creationDate) ?? .distantPast }
        return files.filter { UTType(filenameExtension: $0.pathExtension).map { $0.conforms(to: .svg) || $0.conforms(to: .pdf) } ?? false }
            .sorted { created($0) < created($1) }
    }

    @objc func onOpen() {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.svg, .pdf], asCopy: true)
        picker.allowsMultipleSelection = true
        picker.delegate = self
        present(picker, animated: true)
    }

    // Moves each picked copy into Documents/Imported, with a unique name & its import time as its creation date, so they sort in
    // the order they were imported, appends its document, & shows the first
    func importFiles(_ urls: [URL]) {
        let fileManager = FileManager.default
        try? fileManager.createDirectory(at: importedDirectory, withIntermediateDirectories: true)
        let first = documents.count, now = Date()
        for (i, url) in urls.enumerated() {
            let name = url.deletingPathExtension().lastPathComponent, ext = url.pathExtension
            var destination = importedDirectory.appending(path: url.lastPathComponent), n = 2
            while fileManager.fileExists(atPath: destination.path) {
                destination = importedDirectory.appending(path: "\(name) \(n).\(ext)")
                n += 1
            }
            do {
                try fileManager.moveItem(at: url, to: destination)
                try? fileManager.setAttributes([.creationDate: now.addingTimeInterval(Double(i) * 1e-3)], ofItemAtPath: destination.path)
                documents.append(Document(url: destination))
            } catch {
                print("Couldn't import \(url.lastPathComponent): \(error)")
            }
        }
        if documents.count > first {
            (documentIndex, pageIndex) = (first, 0)
            documentList = makeDocumentList()
        }
    }

    // The shown document's file, if it was imported, so it can be removed
    var importedURL: URL? {
        documentIndex >= bundledCount && documents.indices.contains(documentIndex) ? documents[documentIndex].url : nil
    }

    @objc func onRemove() {
        guard let url = importedURL else {
            return
        }
        let pages = documents[documentIndex].pageCount ?? 1
        let alert = UIAlertController(title: "Remove \(url.lastPathComponent)?", message: pages > 1 ? "All \(pages) pages will be removed." : nil, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Remove", style: .destructive) { _ in self.removeFile(url) })
        present(alert, animated: true)
    }

    // Deletes an imported file & its document, & shows the document after it
    func removeFile(_ url: URL) {
        do {
            try FileManager.default.removeItem(at: url)
        } catch {
            print("Couldn't remove \(url.lastPathComponent): \(error)")
            return
        }
        let index = documents.firstIndex { $0.url == url } ?? documentIndex
        documents.removeAll { $0.url == url }
        documentIndex = documents.isEmpty ? 0 : index % documents.count
        pageIndex = 0
        documentList = makeDocumentList()
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

extension ViewController: UIGestureRecognizerDelegate {
    // Taps on the page stepper's background don't step to the next file
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldReceive touch: UITouch) -> Bool {
        !(touch.view?.isDescendant(of: pageView) ?? false)
    }
}

extension ViewController: UIDocumentPickerDelegate {
    func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        importFiles(urls)
    }
}

extension ViewController: RASceneListDelegate {
    func shouldRedraw(atTime time: Double, scale: Double, width: Double, height: Double) -> Bool {
        redraw = redraw || documentList == nil || animating
        return redraw
    }
    
    func getListAtTime(_ time: Double, scale: Double, width: Double, height: Double) -> RASceneList {
        redraw = false;
        stepGrid(time)
        let list = documentList ?? CounterRotatingCircles(time, width: width, height: height)
        list.ctm = ctm
        list.useClips = gridProgress == 0     // Clip bounds don't move with the draws
        list.clearColor = RAPaint(gray: 0.66, alpha: 1)
        return list
    }
}

private extension NSLayoutConstraint {
    func withPriority(_ priority: UILayoutPriority) -> NSLayoutConstraint {
        self.priority = priority
        return self
    }
}

private extension CGAffineTransform {
    func interpolated(to t: CGAffineTransform, by u: Double) -> CGAffineTransform {
        let v = 1 - u
        return CGAffineTransform(a: v * a + u * t.a, b: v * b + u * t.b, c: v * c + u * t.c, d: v * d + u * t.d, tx: v * tx + u * t.tx, ty: v * ty + u * t.ty)
    }
}
