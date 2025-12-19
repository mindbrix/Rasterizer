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
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case magnify(scale: Double)
        case mouseDown(x: Double, y: Double, flags: NSEvent.ModifierFlags)
        case mouseMove(x: Double, y: Double, flags: NSEvent.ModifierFlags)
        case mouseUp(x: Double, y: Double, flags: NSEvent.ModifierFlags)
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
    
    var selectedFont: NSFont?
    var font: Font {
        let f = selectedFont ?? NSFont(name: "HelveticaNeue-Medium", size: 72)!
        return Font(name: f.fontName, size: f.pointSize)
    }
    
    let app = SwiftApp()
    
    let store = Store()
    var state: State {
        State(font: font, store: store)
    }
    var loginPage: Page {
        Page(pageID: .LogIn,
            state: state,
            controls: [
                .label(label: .Welcome),
                .text(label: .UserName, key: .username),
                .text(label: .Password, key: .password),
                .button(label: .LogIn, closure: { context in
                    let tapcount = context.getValue(key: .tapcount) as? Int ?? 0
                    print("\(tapcount)")
                    context.setValue(value: tapcount + 1, key: .tapcount)
                })
            ])
    }
    var page: Page {
        switch pageID {
        case .Null:
            Page(pageID: .Null, state: state, controls: [])
        case .LogIn:
            loginPage
        }
    }
    var pageID = PageID.LogIn
    
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
        case .mouseDown(let x, let y, _):
            let inv = ctm.inverted()
            let mx = x * inv.a + y * inv.c + inv.tx
            let my = x * inv.b + y * inv.d + inv.ty
            app.pageID = .LogIn
            app.delegate = self
            app.mouseDownIn(bounds, mx: mx, my: my)
        case .mouseMove(let x, let y, let flags):
            if (flags.contains(.shift)) {
                let inv = ctm.inverted()
                slider = max(0.0, min(1.0, (x * inv.a + y * inv.c + inv.tx) / bounds.width))
            }
        case .mouseUp(_, _, _):
            break
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
        let scene = drawables[index].getSceneAtTime(t, bounds: bounds, state: self)
        list.add(scene, ctm: .identity, clip: .zero)
        if let pasted = pastedScene {
            list.add(pasted,
                ctm: .identity,
                clip: .zero
            )
        }
        let pg = RAScene()
        app.pageID = .LogIn
        app.delegate = self
        app.drawIn(bounds, scene: pg)
        list.add(pg, ctm: .identity, clip: .zero)
        
        list.ctm = ctm
        list.useClips = useClips;
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
}

extension SwiftDemo: SwiftApp.Delegate {
    func pageFor(_ pageID: PageID) -> Page? {
        switch pageID {
        case .Null:
            nil
        case .LogIn:
            loginPage
        }
    }
}
