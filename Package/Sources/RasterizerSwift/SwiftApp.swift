//
//  SwiftApp.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 14/12/2025.
//

import Foundation
import RasterizerObjC


enum PageID: String, CaseIterable {
    case Null
    case LogIn
}

class Store {
    struct Entry {
        let hash: Int
        let object: any Hashable
        let observers: Set<PageID>
    }
    enum Key: String, CaseIterable {
        case username
        case password
        case tapcount
    }
    func getValue(key: Key, pageID: PageID) -> Any? {
        guard let entry = dict[key] else {
            return nil
        }
        setValue(value: entry.object, key: key, pageID: pageID)
        return entry.object
    }
    func intValue(key: Key, pageID: PageID) -> Int {
        getValue(key: key, pageID: pageID) as? Int ?? 0
    }
    func stringValue(key: Key, pageID: PageID) -> String {
        getValue(key: key, pageID: pageID) as? String ?? "88"
    }
    func setValue(value: any Hashable, key: Key, pageID: PageID) {
        let observers = dict[key]?.observers ?? []
        let entry = Entry(hash: value.hashValue, object: value, observers: observers.union([pageID]))
        dict[key] = entry
    }
    var dict: [Key: Entry] = [:]
}

enum Label: String {
    case UserName = "User name"
    case Password
    case LogIn = "Log in"
    case Welcome
}

enum Control {
    typealias Closure = (SwiftApp) -> Void
    
    case button(label: Label, closure: Closure)
    case label(label: Label)
    case text(label: Label, key: Store.Key)
    
    var isTappable: Bool {
        closure != nil
    }
    var closure: Closure? {
        switch self {
        case .button(_, let closure):
            return closure
        default:
            break
        }
        return nil
    }
}

struct Page {
   let controls: [Control]
}

struct Font {
    let name: String
    let size: Double
}

struct Run {
    init(string: String, font: Font, color: RAPaint) {
        attributedString = RAText.createAttributedString(string, fontName: font.name, fontSize: font.size, color: color)
        bounds = RAText.bounds(for: attributedString)
    }
    let attributedString: NSAttributedString
    let bounds: CGRect
}

struct Tappable {
    let bounds: CGRect
    let control: Control
}

class SwiftApp {
    enum Alignment: Double {
        case min = 0, mid = 0.5, max = 1
    }
    protocol Delegate: AnyObject {
        func controlsFor(_ pageID: PageID) -> [Control]?
    }
    weak var delegate: Delegate?
    var pageID = PageID.Null
    var font = Font(name: "HelveticaNeue-Medium", size: 72)
    let store = Store()
    var tappables: [Tappable] = []
    
    func drawIn(_ bounds: CGRect, scene: RAScene) {
        guard pageID != .Null, let delegate, let controls = delegate.controlsFor(pageID) else {
            return
        }
        CGRect.drawGridIn(bounds, count: controls.count, scene: scene)
        
        tappables.removeAll()
        for (i, control) in controls.enumerated() {
            let b = CGRect.boundsForIndex(bounds, index: i, count: controls.count)
            let runs = runsFor(control: control)
            let origin = originIn(b, runs: runs, alignx: .mid, aligny: .mid)
            var tx = 0.0
            for run in runs {
                let ctm = CGAffineTransform(translationX: tx + origin.x, y: origin.y)
                scene.addRect(run.bounds, ctm: ctm, width: 1, color: RAPaint())
                scene.addTextLine(run.attributedString, ctm: ctm, clip: .zero)
                
                if control.isTappable {
                    tappables.append(Tappable(bounds: run.bounds.applying(ctm), control: control))
                }
                tx += run.bounds.width
            }
        }
    }
    func mouseDownIn(_ bounds: CGRect, mx: Double, my: Double) {
        for tappable in tappables.reversed() {
            if tappable.bounds.contains(CGPoint(x: mx, y: my)) {
                tappable.control.closure?(self)
            }
        }
    }
    
    func runsFor(control: Control) -> [Run] {
        let red = RAPaint(red: 1, green: 0, blue: 0, alpha: 1)
        let black = RAPaint()
        let gray = RAPaint(gray: 0.66, alpha: 1)
        
        switch control {
        case .button(let label, _):
            return [
                Run(string: label.rawValue, font: font, color: red)
            ]
        case .label(let label):
            return [
                Run(string: label.rawValue, font: font, color: black)
            ]
        case .text(let label, let key):
            return [
                Run(string: label.rawValue, font: font, color: black),
                Run(string: store.stringValue(key: key, pageID: pageID), font: font, color: gray)
            ]
        }
    }
    
    func originIn(_ bounds: CGRect, runs: [Run], alignx: Alignment, aligny: Alignment) -> CGPoint {
        let width = runs.reduce(0.0) { result, run in
            result + run.bounds.width
        }
        let height = runs.reduce(0.0) { result, run in
            max(result, run.bounds.maxY)
        }
        let tx = alignx.rawValue * (bounds.width - width)
        let ty = aligny.rawValue * (bounds.height - height)
        
        return CGPoint(x: bounds.minX + tx, y: bounds.minY + ty)
    }
}
