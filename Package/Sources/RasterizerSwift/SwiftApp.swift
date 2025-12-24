//
//  SwiftApp.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 14/12/2025.
//

import Foundation
import RasterizerObjC


class Store {
    typealias keyType = String
    typealias ValueType = any Hashable
    typealias DictType = [keyType: ValueType]
    
    func getValue(key: keyType) -> ValueType? {
        dict[key]
    }
    func setValue(value: ValueType?, key: keyType) {
        dict[key] = value
    }
    var dict: DictType = [:]
}

enum Control {
    typealias Closure = (SwiftApp) -> Void
    
    case button(label: String, closure: Closure)
    case label(label: String)
    case slider(key: Store.keyType, closure: Closure)
    case text(label: String, key: Store.keyType)
    
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
    var key: Store.keyType? {
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
        line = RALine(attributedString: attributedString)
        bounds = line.bounds
    }
    let attributedString: NSAttributedString
    let line: RALine
    let bounds: CGRect
}

struct SliderState: Hashable {
    let min, max, current: Double
}

struct Tappable {
    let index: Int
    let range: NSRange
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
    var observers: [Store.keyType: Set<String>] = [:]
    var tappables: [Tappable] = []
    var tapMap: [NSRange: CGRect] = [:]
    var tapped: Tappable?
    var down: CGPoint = .zero
    var last: CGPoint = .zero
    var showBounds = false
    
    func mouseDown(_ bounds: CGRect, p: CGPoint) {
        guard let tappable = tappables.reversed().filter({ tappable in
            let b = tapMap[tappable.range] ?? .zero
            return b.contains(p)
            }).first else {
            return
        }
        down = p
        last = p
        tapped = tappable
        tappable.control.closure?(self)
    }
    func mouseMoved(_ bounds: CGRect, p: CGPoint) {
        guard let range = tapped?.range, let b = tapMap[range] else {
            return
        }
        if let key = tapped?.control.key, let state = store.getValue(key: key) as? SliderState {
            let dt = (p.x - last.x) / b.width
            let t = max(state.min, min(state.max, state.current + dt))
            store.setValue(value: SliderState(min: state.min, max: state.max, current: t), key: key)
        }
        last = p
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
        tapMap.removeAll()
        
//        font = Font(name: font.name, size: RAText.fontSize(for: font.name, lineHeight: bounds.height / Double(page.controls.count)))
        
        if showBounds {
            CGRect.drawGridIn(bounds, count: page.controls.count, scene: scene)
        }
        let mutable = NSMutableAttributedString()
        var range = NSRange(location: 0, length: 0)
        
        for (i, control) in page.controls.enumerated() {
            let isActive = i == (tapped?.index ?? -1)
            let runs = runsFor(control: control, isActive: isActive)
            for run in runs {
                range.length = run.attributedString.length
                mutable.append(run.attributedString)
                if control.closure != nil {
                    tappables.append(Tappable(index: i, range: range, control: control))
                }
                range.location += range.length
            }
            mutable.append(NSAttributedString(string: "\n"))
            range.location += 1
        }
        let gutter = bounds.height
        let b = CGRect(x: bounds.minX, y: bounds.minY - gutter, width: bounds.width, height: bounds.height + gutter)
        let frame = RAFrame(attributedString: mutable, in: b)
        
        frame.applyRuns({ range, bounds in
            let range = NSRange(location: range.location, length: range.length)
            self.tapMap[range] = bounds
            if self.showBounds {
                scene.addRect(bounds, ctm: .identity, width: 1, color: RAPaint())
            }
        })
        scene.addText(mutable, in: b, ctm: .identity, clip: .zero)
        return scene
    }
    
    func runsFor(control: Control, isActive: Bool) -> [Run] {
        let red = RAPaint(red: 1, green: 0, blue: 0, alpha: 1)
        let blue = RAPaint(red: 0, green: 0, blue: 1, alpha: 1)
        let black = RAPaint()
        let gray = RAPaint(gray: 0.66, alpha: 1)
        
        switch control {
        case .button(let label, _):
            return [
                Run(string: label, font: font, color: isActive ? red : blue)
            ]
        case .label(let label):
            return [
                Run(string: label, font: font, color: black)
            ]
        case .slider(let key, _):
            let current = (store.getValue(key: key) as? SliderState)?.current ?? 0.0
            return [
                Run(string: "\(key):", font: font, color: gray),
                Run(string: String(format: "%.2f", current), font: font, color: isActive ? red : black)
            ]
        case .text(_, let key):
            return [
                Run(string: "\(key):", font: font, color: gray),
                Run(string: store.getValue(key: key) as? String ?? "", font: font, color: black)
            ]
        }
    }
}
