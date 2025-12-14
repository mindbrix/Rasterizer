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
    func drawIn(_ bounds: CGRect, font: Font, scene: RAScene, store: Store) {
        switch self {
        case .button(let label, _):
            let string = label.rawValue
            scene.addText(string, fontName: font.name, fontSize: font.size, ctm: .identity, color: RAPaint(), clip: .zero)
        case .label(let label):
            let string = label.rawValue
            scene.addText(string, fontName: font.name, fontSize: font.size, ctm: .identity, color: RAPaint(), clip: .zero)
        case .text(let key):
            let string = store.getValue(key: key) as? String ?? ""
            scene.addText(string, fontName: font.name, fontSize: font.size, ctm: .identity, color: RAPaint(), clip: .zero)
        }
    }
}

struct Page {
    let controls: [Control]
}

extension Page {
    func drawIn(_ bounds: CGRect, font: Font, scene: RAScene, store: Store) {
        for control in controls {
            control.drawIn(bounds, font: font, scene: scene, store: store)
        }
    }
}

struct SwiftApp {
    let store: Store
    let page: Page
}
