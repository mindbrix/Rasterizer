//
//  SwiftApp.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 14/12/2025.
//

import Foundation
import OrderedCollections
import CoreText
import RasterizerObjC


class Store {
    typealias keyType = String
    typealias ValueType = Any?
    typealias DictType = OrderedDictionary<keyType, ValueType>
    
    func getValue(key: keyType) -> ValueType? {
        dict[key]
    }
    func setValue(value: ValueType?, key: keyType) {
        dict[key] = value
    }
    var dict: DictType = [:]
}

struct Control {
    typealias Closure = (Store, Store.keyType, Store.ValueType?) -> String?
    let key: Store.keyType
    let closure: Closure?
}

struct Font {
    let name: String
    let size: Double
}

struct Tappable {
    let control: Control
    let key: Store.keyType
    let range: NSRange
}

struct Colors {
    static let red = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
    static let blue = CGColor(red: 0, green: 0, blue: 1, alpha: 1)
    static let black = CGColor(gray: 0, alpha: 1)
    static let gray = CGColor(gray: 0.66, alpha: 1)
}

class SwiftApp {
    protocol PageDelegate: AnyObject {
        func controlsFor(_ pageName: String) -> [Control]?
    }
    weak var pageDelegate: PageDelegate?
    var pageName: String?
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
        let key = tapped.control.key
        if var dict = store.getValue(key: key) as? Store.DictType,
           let value = dict[tapped.key],
           let slider = value as? Double {
            let dt = (p.x - last.x) / b.width
            dict[tapped.key] = max(0.0, min(1.0, slider + dt))
            store.setValue(value: dict, key: key)
        }
        last = p
    }
    func mouseUp(_ bounds: CGRect, p: CGPoint) {
        guard let tapped else {
            return
        }
        let key = tapped.control.key
        if var dict = store.getValue(key: key) as? Store.DictType,
           let value = dict[tapped.key] {
            if let flag = value as? Bool {
                dict[tapped.key] = !flag
            }
            store.setValue(value: dict, key: key)
        }
        if let name = tapped.control.closure?(self.store, key, store.getValue(key: key)) {
            pageName = name
        }
        last = p
        self.tapped = nil
    }
    
    func createSceneIn(_ bounds: CGRect) -> RAScene {
        let scene = RAScene()
        guard let pageName, let pageDelegate, let controls = pageDelegate.controlsFor(pageName) else {
            return scene
        }
        for key in observers.keys {
            observers[key]?.remove(pageName)
        }
        for control in controls {
            let key = control.key
            let entry = observers[key] ?? []
            observers[key] = entry.union([pageName])
        }
        
        let tuple = tappablesAndStringForControls(controls)
        tappables = tuple.0
        let frame = RAFrame(
            attributedString: tuple.1,
            in: bounds.withGutter()
        )
        tapMap.removeAll()
        frame.applyRuns({ range, bounds in
            self.tapMap[range] = bounds
        })
        if self.showTapMap {
            for b in tapMap.values {
                scene.strokeRect(b, width: 1, paint: RAPaint())
            }
        }
        scene.add(frame, ctm: .identity, clip: .zero)
        return scene
    }
    
    func tappablesAndStringForControls(_ controls: [Control]) -> ([Tappable], NSAttributedString) {
        var tappables: [Tappable] = []
        let mutable = NSMutableAttributedString()
        
        for control in controls {
            guard let dict = store.getValue(key: control.key) as? Store.DictType else {
                let range = mutable.appendString("\(control.key)\n")
                let isActive = (tapped?.range ?? NSRange()) == range
                mutable.addAttribute(.foregroundColor, value: isActive ? Colors.red : Colors.blue, range: range)
                tappables.append(Tappable(control: control, key: control.key, range: range))
                continue
            }
            var range = mutable.appendString("\(control.key):\n")
            mutable.addAttribute(.foregroundColor, value: Colors.gray, range: range)
            
            for key in dict.keys {
                range = mutable.appendString("\t\(key):")
                let isActive = (tapped?.range ?? NSRange()) == range
                mutable.addAttribute(.foregroundColor, value: isActive ? Colors.red : Colors.gray, range: range)
                tappables.append(Tappable(control: control, key: key, range: range))
                
                let string = {
                    if let flag = dict[key] as? Bool {
                        return String(flag ? 1 : 0)
                    } else if let slider = dict[key] as? Double {
                        return String(format: "%.2f", slider)
                    } else if let value = dict[key], let value {
                        return String(describing: value)
                    }
                    return "nil"
                }() + "\n"
                range = mutable.appendString(string)
                mutable.addAttribute(.foregroundColor, value: Colors.black, range: range)
            }
        }
        let ctFont = CTFontCreateWithName(font.name as CFString, font.size, nil)
        let range = NSRange(location: 0, length: mutable.length)
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = .left
        paragraphStyle.lineBreakMode = .byWordWrapping
        paragraphStyle.tabStops = [NSTextTab(type: .leftTabStopType, location: font.size)]
        mutable.addAttribute(.paragraphStyle, value: paragraphStyle, range: range)
        mutable.addAttribute(.font, value: ctFont, range: range)
        return (tappables, mutable)
    }
}
