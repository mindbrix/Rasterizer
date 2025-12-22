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
    typealias ValueType = any Hashable
    
    enum Key: String, CaseIterable {
        case username
        case password
        case tapcount
    }
    func getValue(key: Key) -> ValueType? {
        dict[key]
    }
    func intValue(key: Key) -> Int {
        getValue(key: key) as? Int ?? 0
    }
    func stringValue(key: Key) -> String {
        getValue(key: key) as? String ?? "88"
    }
    func setValue(value: ValueType, key: Key) {
        dict[key] = value
    }
    var dict: [Key: ValueType] = [:]
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
    
    var closure: Closure? {
        switch self {
        case .button(_, let closure):
            closure
        default:
            nil
        }
    }
    var key: Store.Key? {
        switch self {
        case .text(_, let key):
            key
        default:
            nil
        }
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
    var observers: [Store.Key: Set<PageID>] = [:]
    var tappables: [Tappable] = []
    
    func drawIn(_ bounds: CGRect) -> RAScene {
        let scene = RAScene()
        guard pageID != .Null, let delegate, let controls = delegate.controlsFor(pageID) else {
            return scene
        }
        for key in observers.keys {
            observers[key]?.remove(pageID)
        }
        for control in controls {
            if let key = control.key {
                let entry = observers[key] ?? []
                observers[key] = entry.union([pageID])
            }
        }
        tappables.removeAll()
        CGRect.drawGridIn(bounds, count: controls.count, scene: scene)
        for (i, control) in controls.enumerated() {
            let b = CGRect.boundsForIndex(bounds, index: i, count: controls.count)
            let runs = runsFor(control: control)
            let origin = originIn(b, runs: runs, alignx: .mid, aligny: .mid)
            var tx = 0.0
            for run in runs {
                let ctm = CGAffineTransform(translationX: tx + origin.x, y: origin.y)
                scene.addRect(run.bounds, ctm: ctm, width: 1, color: RAPaint())
                scene.addTextLine(run.attributedString, ctm: ctm, clip: .zero)
                
                if control.closure != nil {
                    tappables.append(Tappable(bounds: run.bounds.applying(ctm), control: control))
                }
                tx += run.bounds.width
            }
        }
        return scene
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
                Run(string: store.stringValue(key: key), font: font, color: gray)
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
