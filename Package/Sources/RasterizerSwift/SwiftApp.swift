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
    typealias ValueType = any Codable & Hashable
    typealias DictType = [keyType: ValueType]
    
    func get<T>(key: keyType) -> T? where T: Decodable {
        dict[key] as? T
    }
    func set<T>(value: T?, key: keyType) {
        dict[key] = value as? ValueType
    }
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
    
    case button(label: String, closure: Closure?)
    case label(label: String)
    case object(key: Store.keyType, closure: Closure?)
    case slider(key: Store.keyType, closure: Closure?)
    case flag(key: Store.keyType, closure: Closure?)
    case text(label: String, key: Store.keyType)
    
    var closure: Closure? {
        switch self {
        case .button(_, let closure), .flag(_, let closure), .object(_, let closure), .slider(_, let closure):
            closure
        default:
            nil
        }
    }
    var key: Store.keyType? {
        switch self {
        case .flag(let key, _), .object(let key, _), .slider(let key, _), .text(_, let key):
            key
        default:
            nil
        }
    }
    var isTappable: Bool {
        switch self {
        case .button(_, _), .flag(_, _), .object(_, _), .slider(_, _):
            true
        default:
            false
        }
    }
}

struct Font {
    let name: String
    let size: Double
}

struct Run {
    init(string: String, font: Font, color: CGColor) {
        attributedString = RAText.createAttributedString(string, fontName: font.name, fontSize: font.size, color: color)
    }
    let attributedString: NSAttributedString
}

struct SliderState: Codable, Hashable {
    let min, max, current: Double
}

struct Tappable {
    let index: Int
    let range: NSRange
    let control: Control
}

struct Page {
    let alignment: NSTextAlignment
    let controls: [Control]
}

class SwiftApp {
    protocol PageDelegate: AnyObject {
        func pageFor(_ pageID: String) -> Page?
    }
    weak var pageDelegate: PageDelegate?
    var pageID: String?
    var font = Font(name: "HelveticaNeue-Medium", size: 28)
    let store = Store()
    var observers: [Store.keyType: Set<String>] = [:]
    var tappables: [Tappable] = []
    var showTapMap = false
    var tapMap: [NSRange: CGRect] = [:]
    var tapped: Tappable?
    var down: CGPoint = .zero
    var last: CGPoint = .zero
    
    func mouseDown(_ bounds: CGRect, p: CGPoint) {
        guard let tappable = tappables.reversed().filter({ (tapMap[$0.range] ?? .zero).contains(p) }).first else {
            return
        }
        down = p
        last = p
        tapped = tappable
    }
    func mouseMoved(_ bounds: CGRect, p: CGPoint) {
        guard let tapped, let b = tapMap[tapped.range] else {
            return
        }
        if case Control.slider = tapped.control, let key = tapped.control.key {
            let state = store.getValue(key: key) as? SliderState ?? SliderState(min: 0, max: 1, current: 0)
            let dt = (p.x - last.x) / b.width
            let t = max(state.min, min(state.max, state.current + dt))
            store.setValue(value: SliderState(min: state.min, max: state.max, current: t), key: key)
        }
        last = p
    }
    func mouseUp(_ bounds: CGRect, p: CGPoint) {
        guard let tapped else {
            return
        }
        var dict: Store.DictType = [:]
        if let key = tapped.control.key, let value = store.getValue(key: key) {
            let mirror = Mirror(reflecting: value)
            for child in mirror.children {
                guard let label = child.label, let val = child.value as? Store.ValueType else {
                    continue
                }
                dict[label] = val
            }
            
            if let jsonData = try? JSONSerialization.data(withJSONObject: dict, options: .prettyPrinted),
               let object = try? JSONDecoder().decode(SliderState.self, from: jsonData) {
                print(object)
            }
        }
        
        
        if case Control.flag = tapped.control, let key = tapped.control.key {
            let flag = store.getValue(key: key) as? Bool ?? false
            store.setValue(value: !flag, key: key)
        }
        tapped.control.closure?(self)
        
        last = p
        self.tapped = nil
    }
    
    func createSceneIn(_ bounds: CGRect) -> RAScene {
        let scene = RAScene()
        guard let pageID, let pageDelegate, let page = pageDelegate.pageFor(pageID) else {
            return scene
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
        let mutable = NSMutableAttributedString()
        var range = NSRange(location: 0, length: 0)
        
        for (i, control) in page.controls.enumerated() {
            let isActive = i == (tapped?.index ?? -1)
            let runs = runsFor(control: control, isActive: isActive)
            for run in runs {
                range.length = run.attributedString.length
                mutable.append(run.attributedString)
                if control.isTappable {
                    tappables.append(Tappable(index: i, range: range, control: control))
                }
                range.location += range.length
            }
            mutable.append(NSAttributedString(string: "\n"))
            range.location += 1
        }
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = page.alignment
        paragraphStyle.lineBreakMode = .byClipping
        mutable.addAttributes([.paragraphStyle: paragraphStyle], range: NSRange(location: 0, length: mutable.length))
        
        let gutter = bounds.height
        let frame = RAFrame(
            attributedString: mutable,
            in: CGRect(
                x: bounds.minX, y: bounds.minY - gutter,
                width: bounds.width, height: bounds.height + gutter)
        )
        tapMap.removeAll()
        frame.applyRuns({ range, bounds in
            self.tapMap[range] = bounds
        })
        if self.showTapMap {
            for b in tapMap.values {
                let path = RAPath(rect: b)
                path.close()
                scene.addStroke(path, ctm: .identity, color: RAPaint(), width: 1, capStyle: .capButt, joinStyle: .joinMiter)
            }
        }
        frame.applyLines({ line, origin in
            scene.add(line, ctm: CGAffineTransform(translationX: origin.x, y: origin.y), clip: .zero)
        })
        return scene
    }
    
    func runsFor(control: Control, isActive: Bool) -> [Run] {
        let red = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let blue = CGColor(red: 0, green: 0, blue: 1, alpha: 1)
        let black = CGColor(gray: 0, alpha: 1)
        let gray = CGColor(gray: 0.66, alpha: 1)
        
        switch control {
        case .button(let label, _):
            return [
                Run(string: label, font: font, color: isActive ? red : blue)
            ]
        case .flag(let key, _):
            let current = store.getValue(key: key) as? Bool ?? false
            return [
                Run(string: "\(key):", font: font, color: gray),
                Run(string: String(current ? 1 : 0), font: font, color: isActive ? red : black)
            ]
        case .label(let label):
            return [
                Run(string: label, font: font, color: black)
            ]
        case .object(let key, _):
            return [
                Run(string: "\(key):", font: font, color: gray)
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
