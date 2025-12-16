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
    func stringValue(key: Key) -> String {
        dict[key] as? String ?? ""
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
    case text(label: Label, key: Store.Key)
}

struct Font {
    let name: String
    let size: Double
}

struct State {
    let font: Font
    let store: Store
}

struct Run {
    let string: String
    let font: Font
    let color: RAPaint
    
    var attributedString: NSAttributedString {
        RAText.createAttributedString(string, fontName: font.name, fontSize: font.size, color: color)
    }
    var bounds: CGRect {
        RAText.bounds(for: string, fontName: font.name, fontSize: font.size)
    }
    func draw(scene: RAScene, ctm: CGAffineTransform, clip: CGRect) {
        scene.addTextLine(self.attributedString, ctm: ctm, clip: clip)
    }
}

struct Line {
    enum Alignment: Double {
        case min = 0, mid = 0.5, max = 1
    }
    let alignx: Alignment
    let aligny: Alignment
    let runs: [Run]
    
    func drawIn(_ bounds: CGRect, scene: RAScene, clip: CGRect) {
        let width = runs.reduce(0.0) { result, run in
            result + run.bounds.width
        }
        let height = runs.reduce(0.0) { result, run in
            result + run.bounds.height
        }
        let tx = alignx.rawValue * (bounds.width - width)
        let ty = aligny.rawValue * (bounds.height - height)
        let ctm = CGAffineTransform(translationX: bounds.minX + tx, y: bounds.minY + ty)
        
        for run in runs {
            run.draw(scene: scene, ctm: ctm, clip: clip)
        }
    }
}

extension Control {
    func runsFor(_ state: State) -> [Run] {
        let font = state.font
        switch self {
        case .button(let label, _):
            return [Run(string: label.rawValue, font: font, color: RAPaint(red: 1, green: 0, blue: 0, alpha: 1))]
        case .label(let label):
            return [Run(string: label.rawValue, font: font, color: RAPaint())]
        case .text(let label, let key):
            let run0 = Run(string: label.rawValue, font: font, color: RAPaint())
            let run1 = Run(string: state.store.stringValue(key: key), font: font, color: RAPaint())
            return [run0, run1]
        }
    }
    
    func lineFor(_ state: State) -> Line {
        Line(alignx: .mid, aligny: .max, runs: runsFor(state))
    }
    
    func drawIn(_ bounds: CGRect, scene: RAScene, clip: CGRect, state: State) {
        lineFor(state).drawIn(bounds, scene: scene, clip: clip)
    }
    
    func mouseIn(mx: Double, my: Double, state: State) -> Bool {
        for run in runsFor(state) {
            if run.bounds.contains(CGPoint(x: mx, y: my)) {
                return true
            }
        }
        return false
    }
    
    func mouseDown() {
        switch self {
        case .button(_, let closure):
            closure()
        default:
            break
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
    func drawIn(_ bounds: CGRect, scene: RAScene, state: State) {
        for (i, control) in controls.enumerated() {
            let b = boundsForIndex(bounds, index: i)
            
            let path = RAPath(rect: b)
            path.close()
            scene.addStroke(path, ctm: .identity, color: RAPaint(), width: 1, capStyle: .capButt, joinStyle: .joinMiter)
            
            control.drawIn(b, scene: scene, clip: .zero, state: state)
        }
    }
    func mouseDownIn(_ bounds: CGRect, font: Font, mx: Double, my: Double, store: Store) {
        for (i, control) in controls.enumerated() {
            let b = boundsForIndex(bounds, index: i)
            if b.contains(CGPoint(x: mx, y: my)) {
//                control.mouseDownIn(b, font: font, mx: mx, my: my, store: store)
            }
        }
    }
}

struct SwiftApp {
    let store: Store
    let page: Page
}
