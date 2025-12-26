//
//  SwiftDemo.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation
import RasterizerObjC


class SwiftDemo: NSObject {
    override init() {
        super.init()
        swiftApp.pageDelegate = self
        swiftApp.pageID = PageID.LogIn()
        dict = [
            Key.flag0()  : false,
            Key.slider0(): 0.0,
            Key.useRect(): false
        ]
    }
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case mouseDown(p: CGPoint, flags: NSEvent.ModifierFlags)
        case mouseMove(p: CGPoint, flags: NSEvent.ModifierFlags)
        case mouseUp(p: CGPoint, flags: NSEvent.ModifierFlags)
        case magnify(scale: Double)
        case rotate(angle: Float)
        case translate(tx: Double, ty: Double)
    }
    
    let drawables: [RADrawable] = [
        Test0(),
        TestQuadratics(),
        TestCubics(),
        TestGradients(),
        TestDasher(),
        TestImage()
    ]
    
    var useClips = true
    var index = 0
    var paused = false
    var useCurves = true
    var showOpaques = true
    var showOutlines = false
    var ctm = CGAffineTransform.identity
    var bounds = CGRect.zero
    var pastedScene: RAScene?
    
    var selectedFont: NSFont? {
        didSet {
            if let f = selectedFont {
                swiftApp.font = Font(name: f.fontName, size: f.pointSize)
            }
        }
    }
     
    let swiftApp = SwiftApp()

    func handleEvent(_ event: Event) -> Bool {
        switch event {
        case .keyDown(let character, let flags):
            switch character {
            case "1"..."9":
                guard drawables.count > 0 else {
                    break
                }
                let i = Int(character.asciiValue ?? 0) - Int(Character("1").asciiValue ?? 0)
                index = min(drawables.count - 1, i)
            case "a":
                flag.toggle()
            case "b":
                useClips.toggle()
            case "c":
                useCurves.toggle()
            case "f":
                ctm = .identity
            case "i":
                showOpaques.toggle()
            case "o":
                showOutlines.toggle()
            case "p":
                paused.toggle()
            case "r":
                useRect.toggle()
            case "v":
                if (flags.contains(.command)) {
                    let objects = NSPasteboard.general.readObjects(forClasses: [NSAttributedString.self])
                    if let attrString = objects?.first as? NSAttributedString {
                        let scene = RAScene()
                        scene.addText(attrString, in: bounds, ctm: .identity, clip: .zero)
                        pastedScene = scene
                    }
                }
            default:
                return false
            }
            break
        case .mouseDown(let p, _):
            swiftApp.mouseDown(bounds, p: p)
        case .mouseMove(let p, _):
            swiftApp.mouseMoved(bounds, p: p)
        case .mouseUp(let p, _):
            swiftApp.mouseUp(bounds, p: p)
        case .magnify(let scale):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(scaleX: scale, y: scale), cx: bounds.midX, cy: bounds.midY)
        case .rotate(let angle):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(rotationAngle: CGFloat(angle)), cx: bounds.midX, cy: bounds.midY)
        case .translate(let tx, let ty):
            ctm.tx += tx
            ctm.ty += ty
        }
        return true
    }
}

extension SwiftDemo: RASceneListDelegate {
    func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        bounds = CGRect(x: 0, y: 0, width: width, height: height)
        let t = paused ? slider0 : time - floor(time)
        let list = RASceneList()
        list.add(drawables[index].getSceneAtTime(t, bounds: bounds, state: self))
        list.add(pastedScene ?? RAScene())
        list.add(swiftApp.createSceneIn(bounds), ctm: ctm.inverted(), clip: .zero)
        list.ctm = ctm
        list.useClips = useClips;
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
}

extension SwiftDemo: SwiftApp.PageDelegate {
    var dict: Store.DictType? {
        get {
            swiftApp.store.getValue(key: Key.dict()) as? Store.DictType
        }
        set {
            swiftApp.store.setValue(value: newValue, key: Key.dict())
        }
    }
    
    enum PageID: String, CaseIterable {
        case LogIn
        case Home
        
        func callAsFunction() -> String {
            rawValue
        }
    }
    enum Key: String, CaseIterable {
        case dict
        case slider0
        case flag0
        case useRect
        case button
        
        func callAsFunction() -> String {
            rawValue
        }
    }
    
    var flag: Bool {
        get {
            dict?[Key.flag0()] as? Bool ?? false
        }
        set {
            dict?[Key.flag0()] = newValue
        }
    }
    var slider0: Double {
        dict?[Key.slider0()] as? Double ?? 0.0
    }
    var useRect: Bool {
        get {
            dict?[Key.useRect()] as? Bool ?? false
        }
        set {
            dict?[Key.useRect()] = newValue
        }
    }
    func pageFor(_ pageID: String) -> Page? {
        switch PageID(rawValue: pageID) {
        case .LogIn:
            Page(
                alignment: .left,
                controls: [
                    .object(key: Key.dict(), closure: nil),
                    .object(key: Key.button(), closure: { app in
                        let tapcount = app.store.getValue(key: Key.button()) as? Int ?? 0
                        print("\(tapcount)")
                        app.store.setValue(value: tapcount + 1, key: Key.button())
                        
                        if tapcount == 2 {
                            app.pageID = PageID.Home()
                        }
                    })
                ])
        case .Home:
            Page(alignment: .left,
                 controls: [
                    .object(key: Key.button(), closure: { app in
                        app.store.setValue(value: 0, key: Key.button())
                        app.pageID = PageID.LogIn()
                })
            ])
        default:
            nil
        }
    }
}
