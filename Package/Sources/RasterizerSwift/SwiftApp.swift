//
//  SwiftApp.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 14/12/2025.
//

import Foundation
import RasterizerObjC


class Store {
    enum Key: String, CaseIterable {
        case username
        case password
        case slider0
        case tapcount
    }
    typealias ValueType = any Hashable
    typealias DictType = [Key: ValueType]
    
    func getValue(key: Key) -> ValueType? {
        dict[key]
    }
    func setValue(value: ValueType?, key: Key) {
        dict[key] = value
    }
    var dict: DictType = [:]
}

enum Control {
    typealias Closure = (SwiftApp) -> Void
    
    case button(label: String, closure: Closure)
    case label(label: String)
    case slider(key: Store.Key, closure: Closure)
    case text(label: String, key: Store.Key)
    
    var closure: Closure? {
        switch self {
        case .button(_, let closure):
            closure
        case .slider(_, let closure):
            closure
        default:
            nil
        }
    }
    var key: Store.Key? {
        switch self {
        case .slider(let key, _):
            key
        case .text(_, let key):
            key
        default:
            nil
        }
    }
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

struct SliderState: Hashable {
    let min, max, current: Double
}

struct Tappable {
    let index: Int
    let bounds: CGRect
    let control: Control
}

struct Page {
    let defaults: Store.DictType
    let controls: [Control]
}

class SwiftApp {
    enum Alignment: Double {
        case min = 0, mid = 0.5, max = 1
    }
    protocol PageDelegate: AnyObject {
        func pageFor(_ pageID: String) -> Page?
    }
    weak var pageDelegate: PageDelegate?
    var pageID: String?
    var font = Font(name: "HelveticaNeue-Medium", size: 72)
    let store = Store()
    var observers: [Store.Key: Set<String>] = [:]
    var tappables: [Tappable] = []
    var tapped: Tappable?
    var down: CGPoint = .zero
    var last: CGPoint = .zero
    
    func mouseDown(_ bounds: CGRect, p: CGPoint) {
        guard let tappable = tappables.reversed().filter({ $0.bounds.contains(p) }).first else {
            return
        }
        down = p
        last = p
        tapped = tappable
        tappable.control.closure?(self)
    }
    func mouseMoved(_ bounds: CGRect, p: CGPoint) {
        guard let b = tapped?.bounds else {
            return
        }
        last = p
        let dt = (p.x - down.x) / b.width
        print(dt)
    }
    func mouseUp(_ bounds: CGRect, p: CGPoint) {
        last = p
        tapped = nil
    }
    
    func createSceneIn(_ bounds: CGRect) -> RAScene {
        let scene = RAScene()
        guard let pageID, let pageDelegate, let page = pageDelegate.pageFor(pageID) else {
            return scene
        }
        for key in page.defaults.keys {
            if store.getValue(key: key) == nil, let value = page.defaults[key] {
                store.setValue(value: value, key: key)
            }
        }
        for key in observers.keys {
            observers[key]?.remove(pageID)
        }
        for control in page.controls {
            if let key = control.key {
                let entry = observers[key] ?? []
                observers[key] = entry.union([pageID])
            }
        }
        tappables.removeAll()
        CGRect.drawGridIn(bounds, count: page.controls.count, scene: scene)
        for (i, control) in page.controls.enumerated() {
            let b = CGRect.boundsForIndex(bounds, index: i, count: page.controls.count)
            let isActive = i == (tapped?.index ?? -1)
            let runs = runsFor(control: control, isActive: isActive)
            let origin = originIn(b, runs: runs, alignx: .mid, aligny: .mid)
            var tx = 0.0
            for run in runs {
                let ctm = CGAffineTransform(translationX: tx + origin.x, y: origin.y)
                scene.addRect(run.bounds, ctm: ctm, width: 1, color: RAPaint())
                scene.addTextLine(run.attributedString, ctm: ctm, clip: .zero)
                
                if control.closure != nil {
                    tappables.append(Tappable(index: i, bounds: run.bounds.applying(ctm), control: control))
                }
                tx += run.bounds.width
            }
        }
        return scene
    }
    
    func runsFor(control: Control, isActive: Bool) -> [Run] {
        let red = RAPaint(red: 1, green: 0, blue: 0, alpha: 1)
        let black = RAPaint()
        let gray = RAPaint(gray: 0.66, alpha: 1)
        
        switch control {
        case .button(let label, _):
            return [
                Run(string: label, font: font, color: isActive ? red : gray)
            ]
        case .label(let label):
            return [
                Run(string: label, font: font, color: black)
            ]
        case .slider(_, _):
            return [
                Run(string: "Slider", font: font, color: isActive ? red : gray)
            ]
        case .text(let label, let key):
            return [
                Run(string: label, font: font, color: black),
                Run(string: store.getValue(key: key) as? String ?? "", font: font, color: gray)
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
