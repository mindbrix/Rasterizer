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
        case tapcount
    }
    func getValue(key: Key) -> Any? {
        dict[key]
    }
    func intValue(key: Key) -> Int {
        dict[key] as? Int ?? 0
    }
    func setValue(value: any Hashable, key: Key) {
        dict[key] = value
    }
    var dict: [Key: any Hashable] = [:]
}

enum Label: String {
    case UserName
    case Password
    case LogIn
}

enum Control {
    typealias Closure = () -> Void
    
    case button(label: Label, closure: Closure)
    case label(label: Label)
    case text(key: Store.Key)
}

struct Font {
    let name: String
    let size: Double
}

extension Control {
    func drawAt(_ origin: CGPoint, font: Font, scene: RAScene, store: Store, clip: CGRect) {
        let ctm = CGAffineTransform(translationX: origin.x, y: origin.y)
        switch self {
        case .button(let label, _):
            let string = label.rawValue
            scene.addText(string, fontName: font.name, fontSize: font.size, ctm: ctm, color: RAPaint(red: 1, green: 0, blue: 0, alpha: 1), clip: clip)
        case .label(let label):
            let string = label.rawValue
            scene.addText(string, fontName: font.name, fontSize: font.size, ctm: ctm, color: RAPaint(), clip: clip)
        case .text(let key):
            let string = store.getValue(key: key) as? String ?? ""
            scene.addText(string, fontName: font.name, fontSize: font.size, ctm: ctm, color: RAPaint(), clip: clip)
        }
    }
}

struct Page {
    let controls: [Control]
}

extension Page {
    func boundsForIndex(_ bounds: CGRect, index: Int) -> CGRect {
        let count = Double(controls.count)
        let dy = bounds.height / Double(controls.count)
        return CGRect(x: bounds.origin.x,
                      y: bounds.origin.y + (count - 1 - Double(index)) * dy,
                      width: bounds.width,
                      height: dy)
    }
    func drawIn(_ bounds: CGRect, font: Font, scene: RAScene, store: Store) {
        for (i, control) in controls.enumerated() {
            let b = boundsForIndex(bounds, index: i)
            control.drawAt(b.origin, font: font, scene: scene, store: store, clip: .zero)
        }
    }
    func mouseDownIn(_ bounds: CGRect, font: Font, mx: Double, my: Double, store: Store) {
        for (i, control) in controls.enumerated() {
            let b = boundsForIndex(bounds, index: i)
            if b.contains(CGPoint(x: mx, y: my)) {
                switch control {
                case .button(_, let closure):
                    closure()
                default:
                    break
                }
            }
        }
    }
}

struct SwiftApp {
    let store: Store
    let page: Page
}
