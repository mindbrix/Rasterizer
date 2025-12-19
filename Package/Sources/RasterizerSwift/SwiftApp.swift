//
//  SwiftApp.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 14/12/2025.
//

import Foundation
import RasterizerObjC


enum PageID: String, CaseIterable {
    case Null
    case LogIn
}

class Store {
    struct Entry {
        let hash: Int
        let object: any Hashable
        let observers: Set<PageID>
    }
    enum Key: String, CaseIterable {
        case username
        case password
        case tapcount
    }
    func getValue(key: Key, pageID: PageID) -> Any? {
        guard let entry = dict[key] else {
            return nil
        }
        setValue(value: entry.object, key: key, pageID: pageID)
        return entry.object
    }
    func intValue(key: Key, pageID: PageID) -> Int {
        getValue(key: key, pageID: pageID) as? Int ?? 0
    }
    func stringValue(key: Key, pageID: PageID) -> String {
        getValue(key: key, pageID: pageID) as? String ?? "88"
    }
    func setValue(value: any Hashable, key: Key, pageID: PageID) {
        let observers = dict[key]?.observers ?? []
        let entry = Entry(hash: value.hashValue, object: value, observers: observers.union([pageID]))
        dict[key] = entry
    }
    var dict: [Key: Entry] = [:]
}

enum Label: String {
    case UserName = "User name"
    case Password
    case LogIn = "Log in"
    case Welcome
}

struct Context {
    let pageID: PageID
    let store: Store
    
    func getValue(key: Store.Key) -> Any? {
        store.getValue(key: key, pageID: pageID)
    }
    func setValue(value: any Hashable, key: Store.Key) {
        store.setValue(value: value, key: key, pageID: pageID)
    }
}

enum Control {
    typealias Closure = (Context) -> Void
    
    case button(label: Label, closure: Closure)
    case label(label: Label)
    case text(label: Label, key: Store.Key)
}

struct Page {
    let pageID: PageID
    let state: State
    let controls: [Control]
}

typealias PageMap = [PageID: Page]

struct Font {
    let name: String
    let size: Double
}

struct Run {
    let string: String
    let font: Font
    let color: RAPaint
    
    var attributedString: NSAttributedString {
        RAText.createAttributedString(string, fontName: font.name, fontSize: font.size, color: color)
    }
    var bounds: CGRect {
        RAText.bounds(for: attributedString)
    }
}

struct SetRun {
    let run: Run
    let origin: CGPoint
    let control: Control
}

struct State {
    let font: Font
    let store: Store
}

extension Control {
    enum Alignment: Double {
        case min = 0, mid = 0.5, max = 1
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
    
    var isTappable: Bool {
        closure != nil
    }
    var closure: Closure? {
        switch self {
        case .button(_, let closure):
            return closure
        default:
            break
        }
        return nil
    }
    
    func runsFor(_ state: State, pageID: PageID) -> [Run] {
        let font = state.font
        let red = RAPaint(red: 1, green: 0, blue: 0, alpha: 1)
        let black = RAPaint()
        let gray = RAPaint(gray: 0.66, alpha: 1)
        
        switch self {
        case .button(let label, _):
            return [
                Run(string: label.rawValue, font: font, color: red)
            ]
        case .label(let label):
            return [
                Run(string: label.rawValue, font: font, color: black)
            ]
        case .text(let label, let key):
            return [
                Run(string: label.rawValue, font: font, color: black),
                Run(string: state.store.stringValue(key: key, pageID: pageID), font: font, color: gray)
            ]
        }
    }
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
    func setRunsIn(_ bounds: CGRect) -> [SetRun] {
        controls.enumerated().flatMap({ i, control in
            let b = boundsForIndex(bounds, index: i)
            let runs = control.runsFor(state, pageID: pageID)
            let origin = control.originIn(b, runs: runs, alignx: .mid, aligny: .mid)

            var setruns: [SetRun] = []
            var tx = 0.0
            for run in runs {
                setruns.append(SetRun(run: run, origin: CGPoint(x: tx + origin.x, y: origin.y), control: control))
                tx += run.bounds.width
            }
            return setruns
        })
    }
    func drawIn(_ bounds: CGRect, scene: RAScene) {
//        drawGridIn(bounds, scene: scene)
        for setrun in setRunsIn(bounds) {
            let ctm = CGAffineTransform(translationX: setrun.origin.x, y: setrun.origin.y)
//            scene.addRect(run.run.bounds, ctm: ctm, width: 1, color: RAPaint())
            scene.addTextLine(setrun.run.attributedString, ctm: ctm, clip: .zero)
        }
    }
    func drawGridIn(_ bounds: CGRect, scene: RAScene) {
        for i in 0..<controls.count {
            scene.addRect(boundsForIndex(bounds, index: i), ctm: .identity, width: 1, color: RAPaint())
        }
    }
    func mouseDownIn(_ bounds: CGRect, mx: Double, my: Double) {
        for setrun in setRunsIn(bounds).filter({ $0.control.isTappable }).reversed() {
            if setrun.run.bounds.contains(CGPoint(x: mx - setrun.origin.x, y: my - setrun.origin.y)) {
                setrun.control.closure?(Context(pageID: pageID, store: state.store))
                break
            }
        }
    }
}

 
class SwiftApp {
    protocol Delegate: AnyObject {
        func pageFor(_ pageID: PageID) -> Page?
    }
    weak var delegate: Delegate?
    var pageID = PageID.Null
    let store = Store()
    
    func drawIn(_ bounds: CGRect, scene: RAScene) {
        guard pageID != .Null, let delegate, let page = delegate.pageFor(pageID) else {
            return
        }
        page.drawIn(bounds, scene: scene)
    }
    func mouseDownIn(_ bounds: CGRect, mx: Double, my: Double) {
        guard pageID != .Null, let delegate, let page = delegate.pageFor(pageID) else {
            return
        }
        page.mouseDownIn(bounds, mx: mx, my: my)
    }
}
