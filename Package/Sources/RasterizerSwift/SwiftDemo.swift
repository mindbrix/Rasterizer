//
//  SwiftDemo.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation
import RasterizerObjC


class SwiftDemo: NSObject, RASceneListDelegate {
    override init() {
        super.init()
        swiftApp.pageDelegate = self
        swiftApp.pageID = .LogIn
    }
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case magnify(scale: Double)
        case mouseDown(p: CGPoint, flags: NSEvent.ModifierFlags)
        case mouseMove(p: CGPoint, flags: NSEvent.ModifierFlags)
        case mouseUp(p: CGPoint, flags: NSEvent.ModifierFlags)
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
    var flag = false
    var paused = false
    var useCurves = true
    var showOpaques = true
    var showOutlines = false
    var useRect = false
    var slider = 0.0
    var t = 0.0
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
        case .magnify(let scale):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(scaleX: scale, y: scale), cx: bounds.midX, cy: bounds.midY)
        case .mouseDown(let p, _):
            swiftApp.mouseDownIn(bounds, p: p.applying(ctm.inverted()))
        case .mouseMove(let p, let flags):
            if (flags.contains(.shift)) {
                let inv = ctm.inverted()
                slider = max(0.0, min(1.0, (p.x * inv.a + p.y * inv.c + inv.tx) / bounds.width))
            }
            swiftApp.mouseMovedIn(bounds, p: p.applying(ctm.inverted()))
        case .mouseUp(let p, _):
            swiftApp.mouseUpIn(bounds, p: p.applying(ctm.inverted()))
        case .rotate(let angle):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(rotationAngle: CGFloat(angle)), cx: bounds.midX, cy: bounds.midY)
        case .translate(let tx, let ty):
            ctm.tx += tx
            ctm.ty += ty
        }
        return true
    }
    
    func shouldRedraw(atTime time: Double) -> Bool {
        true
    }
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        bounds = CGRect(x: 0, y: 0, width: width, height: height)
        t = paused ? slider : time - floor(time)
        let list = RASceneList()
        list.add(drawables[index].getSceneAtTime(t, bounds: bounds, state: self))
        list.add(pastedScene ?? RAScene())
        list.add(swiftApp.drawIn(bounds))
        list.ctm = ctm
        list.useClips = useClips;
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
}

extension SwiftDemo: SwiftApp.PageDelegate {
    func controlsFor(_ pageID: PageID) -> [Control]? {
        switch pageID {
        case .LogIn: [
            .label(label: .Welcome),
            .text(label: .UserName, key: .username),
            .text(label: .Password, key: .password),
            .button(label: .LogIn, closure: { app in
                let tapcount = app.store.getValue(key: .tapcount) as? Int ?? 0
                print("\(tapcount)")
                app.store.setValue(value: tapcount + 1, key: .tapcount)
            })
        ]
        default:
            nil
        }
    }
}
