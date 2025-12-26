//
//  SwiftApp.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 14/12/2025.
//

import Foundation
import CoreText
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

struct Control {
    typealias Closure = (SwiftApp) -> Void
    let key: Store.keyType
    let closure: Closure?
}

struct Font {
    let name: String
    let size: Double
}

struct Tappable {
    let range: NSRange
    let control: Control
    let key: Store.keyType
}

class SwiftApp {
    protocol PageDelegate: AnyObject {
        func controlsFor(_ pageID: String) -> [Control]?
    }
    weak var pageDelegate: PageDelegate?
    var alignment: NSTextAlignment = .left
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
        tapped.control.closure?(self)
        
        last = p
        self.tapped = nil
    }
    
    func createSceneIn(_ bounds: CGRect) -> RAScene {
        let scene = RAScene()
        guard let pageID, let pageDelegate, let controls = pageDelegate.controlsFor(pageID) else {
            return scene
        }
        for key in observers.keys {
            observers[key]?.remove(pageID)
        }
        for control in controls {
            let key = control.key
            let entry = observers[key] ?? []
            observers[key] = entry.union([pageID])
        }
        
        let frame = RAFrame(
            attributedString: stringForControls(controls),
            in: bounds.withGutter()
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
    
    func stringForControls(_ controls: [Control]) -> NSAttributedString {
        let red = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
        let blue = CGColor(red: 0, green: 0, blue: 1, alpha: 1)
        let black = CGColor(gray: 0, alpha: 1)
        let gray = CGColor(gray: 0.66, alpha: 1)

        tappables.removeAll()
        let mutable = NSMutableAttributedString()
        
        for control in controls {
            guard let dict = store.getValue(key: control.key) as? Store.DictType else {
                let range = mutable.appendString("\(control.key)\n")
                let isActive = (tapped?.range ?? NSRange()) == range
                mutable.addAttribute(.foregroundColor, value: isActive ? red : blue, range: range)
                tappables.append(Tappable(range: range, control: control, key: control.key))
                continue
            }
            let range = mutable.appendString("\(control.key):\n")
            mutable.addAttribute(.foregroundColor, value: gray, range: range)
            
            for key in dict.keys.sorted() {
                let range = mutable.appendString("\t\(key):")
                let isActive = (tapped?.range ?? NSRange()) == range
                mutable.addAttribute(.foregroundColor, value: isActive ? red : gray, range: range)
                tappables.append(Tappable(range: range, control: control, key: key))
                
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
                let range0 = mutable.appendString(string)
                mutable.addAttribute(.foregroundColor, value: black, range: range0)
            }
            mutable.append(NSAttributedString(string: "\n"))
        }
        let ctFont = CTFontCreateWithName(font.name as CFString, font.size, nil)
        let range = NSRange(location: 0, length: mutable.length)
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = alignment
        paragraphStyle.lineBreakMode = .byClipping
        mutable.addAttribute(.paragraphStyle, value: paragraphStyle, range: range)
        mutable.addAttribute(.font, value: ctFont, range: range)
        return mutable
    }
}
