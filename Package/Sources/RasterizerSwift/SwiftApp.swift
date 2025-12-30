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
    typealias KeyType = String
    typealias ValueType = Any
    typealias DictType = OrderedDictionary<KeyType, ValueType>
    
    func getValue(key: KeyType) -> ValueType? {
        dict[key]
    }
    func setValue(value: ValueType?, key: KeyType) {
        dict[key] = value
    }
    var dict: DictType = [:]
}

struct Control {
    enum Mode {
        case button, mutable, readonly
    }
    typealias Closure = (Store, Store.KeyType) -> String?
    let key: Store.KeyType
    let mode: Mode
    let closure: Closure?
}

extension Control: Hashable {
    static func == (lhs: Control, rhs: Control) -> Bool {
        lhs.hashValue == rhs.hashValue
    }
    func hash(into hasher: inout Hasher) {
        hasher.combine(key)
        hasher.combine(mode)
    }
}

struct Font {
    let name: String
    let size: Double
}

struct Tappable {
    let control: Control
    let subKey: Store.KeyType
    let range: NSRange
    let exclude: Bool
    var bounds = CGRect.zero
}

struct Colors {
    static let red = CGColor(red: 1, green: 0, blue: 0, alpha: 1)
    static let blue = CGColor(red: 0, green: 0, blue: 1, alpha: 1)
    static let black = CGColor(gray: 0, alpha: 1)
    static let gray = CGColor(gray: 0.66, alpha: 1)
}

struct PageEntry {
    let hash: Int
    let scene: RAScene
    let tappables: [Tappable]
    let tapMap: OrderedDictionary<NSRange, CGRect>
}

class SwiftApp {
    protocol PageDelegate: AnyObject {
        func controlsFor(_ pageName: String) -> [Control]?
    }
    weak var pageDelegate: PageDelegate?
    var font = Font(name: "HelveticaNeue-Medium", size: 16)
    let store = Store()
    
    var pageName: String?
    var observed: [String: Set<Store.KeyType>] = [:]
    var pageMap: [String: PageEntry] = [:]
    
    var tapped: Tappable?
    var down: CGPoint = .zero
    var last: CGPoint = .zero
    var showTapMap = false
    
    func mouseDown(_ bounds: CGRect, p: CGPoint) {
        guard let pageName, let entry = pageMap[pageName],
              let element = entry.tapMap.enumerated().reversed().filter({ $0.element.value.contains(p) }).first?.element,
              let tappable = entry.tappables.filter({ $0.range.contains(element.key.location) }).first
        else {
            return
        }
        down = p
        last = p
        tapped = tappable
        tapped?.bounds = element.value
    }
    func mouseMoved(_ bounds: CGRect, p: CGPoint) {
        guard let tapped else {
            return
        }
        let key = tapped.control.key
        if var dict = store.getValue(key: key) as? Store.DictType,
           let value = dict[tapped.subKey],
           let slider = value as? Double {
            let dt = (p.x - last.x) / tapped.bounds.width
            dict[tapped.subKey] = max(0.0, min(1.0, slider + dt))
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
           let value = dict[tapped.subKey] {
            if let flag = value as? Bool {
                dict[tapped.subKey] = !flag
            } else if let range = value as? NSRange {
                let delta = down.x < tapped.bounds.midX ? -1 : 1
                let location = max(0, min(range.length - 1, range.location + delta))
                dict[tapped.subKey] = NSRange(location: location, length: range.length)
            }
            store.setValue(value: dict, key: key)
        }
        if let name = tapped.control.closure?(self.store, key) {
            pageName = name
        }
        self.tapped = nil
    }
    
    func shouldRedraw(_ pageName: String, in bounds: CGRect) -> Bool {
        guard let pageHash = pageMap[pageName]?.hash else {
            return true
        }
        return pageHash != hashFor(pageName, in: bounds)
    }
    
    func hashFor(_ pageName: String, in bounds: CGRect) -> Int {
        guard let pageDelegate, let controls = pageDelegate.controlsFor(pageName) else {
            return 0
        }
        var hasher = Hasher()
        hasher.combine(controls)
        hasher.combine(bounds)
        hasher.combine(font.name)
        hasher.combine(font.size)
        hasher.combine(pageName)
        hasher.combine(tapped?.range ?? NSRange())
        for key in (observed[pageName] ?? []) {
            hasher.combine(key)
            if let object = store.getValue(key: key) as? CustomStringConvertible {
                hasher.combine(String(describing: object))
            }
        }
        return hasher.finalize()
    }
    func sceneFor(_ pageName: String, in bounds: CGRect) -> RAScene {
        let scene = RAScene()
        guard let pageDelegate, let controls = pageDelegate.controlsFor(pageName) else {
            return scene
        }
        let hash = hashFor(pageName, in: bounds)
        if let entry = pageMap[pageName], hash == entry.hash {
            return entry.scene
        }
        observed[pageName] = .init(controls.map{ $0.key })
        
        let tuple = tappablesAndStringForControls(controls)
        let tappables = tuple.0
        let frame = RAFrame(
            attributedString: tuple.1,
            in: bounds.withGutter()
        )
        var tapMap: OrderedDictionary<NSRange, CGRect> = [:]
        frame.applyRuns({ range, bounds in
            tapMap[range] = bounds
        })
        let excludes = tappables.filter{ $0.exclude }
        let rect = scene.add(frame, excludes: excludes.map{ NSValue(range: $0.range) }, ctm: .identity, clip: .zero)
        for exclude in excludes {
            if let b = tapMap[exclude.range], let dict = store.getValue(key: exclude.control.key) as? Store.DictType {
                let isActive = (tapped?.range ?? NSRange()) == exclude.range
                
                if let flag = dict[exclude.subKey] as? Bool {
                    scene.addFlag(flag, in: b, paint: RAPaint(cgColor: isActive ? Colors.red : Colors.blue), fontSize: font.size)
                } else if let slider = dict[exclude.subKey] as? Double {
                    scene.addSlider(slider, in: b, paint: RAPaint(cgColor: isActive ? Colors.red : Colors.blue), fontSize: font.size)
                } else if let _ = dict[exclude.subKey] as? NSRange {
                    scene.addStepper(in: b, paint: RAPaint(cgColor: isActive ? Colors.red : Colors.blue), fontSize: font.size)
                } else {
                    scene.fillRect(b, paint: RAPaint(cgColor: Colors.red))
                }
            }
        }
        if self.showTapMap {
            scene.strokeRect(bounds, width: -1, paint: RAPaint())
            scene.strokeRect(rect, width: -1, paint: RAPaint())
            for b in tapMap.values {
                scene.strokeRect(b, width: -1, paint: RAPaint())
            }
        }
        pageMap[pageName] = PageEntry(hash: hash, scene: scene, tappables: tappables, tapMap: tapMap)
        return scene
    }
    
    func tappablesAndStringForControls(_ controls: [Control]) -> ([Tappable], NSAttributedString) {
        var tappables: [Tappable] = []
        let mutable = NSMutableAttributedString()
        
        for control in controls {
            switch control.mode {
            case .button:
                let range = mutable.appendString("\t\t\(control.key)\n")
                let isActive = (tapped?.range ?? NSRange()) == range
                mutable.addAttribute(.foregroundColor, value: isActive ? Colors.red : Colors.blue, range: range)
                tappables.append(Tappable(control: control, subKey: control.key, range: range, exclude: false))
                continue
            case .mutable:
                guard let dict = store.getValue(key: control.key) as? Store.DictType else {
                    continue
                }
                var range = mutable.appendString("\(control.key):\n")
                mutable.addAttribute(.foregroundColor, value: Colors.gray, range: range)
                
                for (subKey, value) in dict {
                    range = mutable.appendString("\t\(subKey):")
                    mutable.addAttribute(.foregroundColor, value: Colors.black, range: range)
                    var placeholder = ""
                    if let _ = value as? Bool {
                        placeholder = "flag"
                    } else if let _ = value as? Double {
                        placeholder = "sligeeer"
                    } else if let _ = value as? NSRange {
                        placeholder = "Rang"
                    } else if let value = value as? CustomStringConvertible {
                        placeholder = String(describing: value)
                    }
                    placeholder = "\t" + placeholder + "\n"
                    range = mutable.appendString(placeholder)
                    tappables.append(Tappable(control: control, subKey: subKey, range: range, exclude: true))
                    mutable.addAttribute(.foregroundColor, value: Colors.gray, range: range)
                }
            case .readonly:
                guard let dict = store.getValue(key: control.key) as? Store.DictType else {
                    continue
                }
                var range = mutable.appendString("\(control.key):\n")
                mutable.addAttribute(.foregroundColor, value: Colors.gray, range: range)
                
                for (subKey, value) in dict {
                    range = mutable.appendString("\t\(subKey):")
                    mutable.addAttribute(.foregroundColor, value: Colors.gray, range: range)
                    
                    var placeholder = ""
                     if let slider = value as? Double {
                         placeholder = String(format: "%.2f", slider)
                    } else if let range = value as? NSRange {
                        placeholder = String(range.location)
                    } else if let value = value as? CustomStringConvertible {
                        placeholder = String(describing: value)
                    }
                    placeholder = "\t" + placeholder + "\n"
                    range = mutable.appendString(placeholder)
                    mutable.addAttribute(.foregroundColor, value: Colors.black, range: range)
                }
            }
            
        }
        let ctFont = CTFontCreateWithName(font.name as CFString, font.size, nil)
        let range = NSRange(location: 0, length: mutable.length)
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = .left
        paragraphStyle.lineBreakMode = .byWordWrapping
        paragraphStyle.tabStops = [
            NSTextTab(type: .leftTabStopType, location: font.size),
            NSTextTab(type: .rightTabStopType, location: 8 * font.size)
        ]
        mutable.addAttribute(.paragraphStyle, value: paragraphStyle, range: range)
        mutable.addAttribute(.font, value: ctFont, range: range)
        return (tappables, mutable)
    }
}
