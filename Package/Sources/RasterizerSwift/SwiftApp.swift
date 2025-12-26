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
    typealias ValueType = Any
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
    
    case label(label: String)
    case object(key: Store.keyType, closure: Closure?)
    
    var closure: Closure? {
        switch self {
        case .object(_, let closure):
            closure
        default:
            nil
        }
    }
    var key: Store.keyType? {
        switch self {
        case .object(let key, _):
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

struct SliderState: Codable, Hashable {
    let min, max, current: Double
}

struct Tappable {
    let index: Int
    let range: NSRange
    let control: Control
    let key: Store.keyType
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
        if let key = tapped.control.key {
            if var dict = store.getValue(key: key) as? Store.DictType,
               let value = dict[tapped.key],
               let slider = value as? Double {
                let dt = (p.x - last.x) / b.width
                dict[tapped.key] = max(0.0, min(1.0, slider + dt))
                store.setValue(value: dict, key: key)
            }
        }
        last = p
    }
    func mouseUp(_ bounds: CGRect, p: CGPoint) {
        guard let tapped else {
            return
        }
        if let key = tapped.control.key,
            var dict = store.getValue(key: key) as? Store.DictType,
           let value = dict[tapped.key] {
            if let flag = value as? Bool {
                dict[tapped.key] = !flag
            }
            store.setValue(value: dict, key: key)
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
        
        let gutter = bounds.height
        let frame = RAFrame(
            attributedString: stringForPage(page),
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
    
    func stringForPage(_ page: Page) -> NSAttributedString {
        let red = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let blue = CGColor(red: 0, green: 0, blue: 1, alpha: 1)
        let black = CGColor(gray: 0, alpha: 1)
        let gray = CGColor(gray: 0.66, alpha: 1)
        
        tappables.removeAll()
        let mutable = NSMutableAttributedString()
        
        for (i, control) in page.controls.enumerated() {
            let isActive = i == (tapped?.index ?? -1)
            switch control {
            case .label(let label):
                mutable.append(RAText.createAttributedString(label, fontName: font.name, fontSize: font.size, color: black))
            case .object(let key, _):
                guard let dict = store.getValue(key: key) as? Store.DictType else {
                    let range = mutable.appendString(
                        RAText.createAttributedString("\(key)\n", fontName: font.name, fontSize: font.size, color: blue))
                    tappables.append(Tappable(index: i, range: range, control: control, key: key))
                    continue
                    
                }
                mutable.append(
                    RAText.createAttributedString("\(key):\n", fontName: font.name, fontSize: font.size, color: gray))
                
                for key in dict.keys.sorted() {
                    let range = mutable.appendString(
                        RAText.createAttributedString("\t\(key):", fontName: font.name, fontSize: font.size, color: gray))
                    tappables.append(Tappable(index: i, range: range, control: control, key: key))
                    
                    let string = {
                        if let flag = dict[key] as? Bool {
                            return String(flag ? 1 : 0) + "\n"
                        } else if let slider = dict[key] as? Double {
                            return String(format: "%.2f\n", slider)
                        } else if let string = dict[key] as? String {
                            return "\(string)\n"
                        }
                        return ""
                    }()
                    
                    mutable.append(
                        RAText.createAttributedString(string, fontName: font.name, fontSize: font.size, color: black))
                }
            }
            mutable.append(NSAttributedString(string: "\n"))
        }
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = page.alignment
        paragraphStyle.lineBreakMode = .byClipping
        mutable.addAttributes([.paragraphStyle: paragraphStyle], range: NSRange(location: 0, length: mutable.length))
        return mutable
    }
}
