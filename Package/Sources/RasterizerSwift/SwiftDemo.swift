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
        swiftApp.pageName = PageID.LogIn()
        let dict: Store.DictType = [
            Key.flag()   : false,
            Key.paused() : false,
            Key.slider() : 0.0,
            Key.rect(): false,
            Key.index()  : NSRange(location: 0, length: drawables.count),
            Key.outlines(): false,
            Key.opaques() : true,
            Key.clips()    : true,
            Key.curves()   : true
        ]
        swiftApp.store.setValue(
            value: dict,
            key: Key.dict())
    }
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case paste(attributed: NSAttributedString)
        case mouseDown(p: CGPoint, flags: NSEvent.ModifierFlags)
        case mouseMove(p: CGPoint, flags: NSEvent.ModifierFlags)
        case mouseUp(p: CGPoint, flags: NSEvent.ModifierFlags)
        case reset
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
    
    var ctm = CGAffineTransform.identity
    var appCtm = CGAffineTransform.identity
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
        case .keyDown(_, _):
            return false
        case .paste(let attributed):
            let scene = RAScene()
            scene.addText(attributed, in: bounds, ctm: .identity, clip: .zero)
            pastedScene = scene
        case .mouseDown(let p, _):
            swiftApp.mouseDown(bounds, p: p.applying(appCtm))
        case .mouseMove(let p, _):
            swiftApp.mouseMoved(bounds, p: p.applying(appCtm))
        case .mouseUp(let p, _):
            swiftApp.mouseUp(bounds, p: p.applying(appCtm))
        case .reset:
            ctm = .identity
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
        let t = paused ? slider : time - floor(time)
        let list = RASceneList()
        list.add(drawables[index].getSceneAtTime(t, bounds: bounds, state: self))
        list.add(pastedScene ?? RAScene())
        appCtm = ctm.inverted()
        list.add(swiftApp.createSceneIn(bounds), ctm: appCtm, clip: .zero)
        appCtm = ctm.concatenating(appCtm).inverted()
        list.ctm = ctm
        list.useClips = useClips;
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
}

extension SwiftDemo: SwiftApp.PageDelegate {
    enum PageID: String, CaseIterable {
        case LogIn
        case Home
        
        func callAsFunction() -> String {
            rawValue
        }
    }
    enum Key: String, CaseIterable {
        case dict
        case flag
        case paused
        case slider
        case rect
        case index
        case clips
        case curves
        case opaques
        case outlines
        case button = "اللغة البشتوية"
        
        func callAsFunction() -> String {
            rawValue
        }
    }
    
    var dict: Store.DictType? {
        swiftApp.store.getValue(key: Key.dict()) as? Store.DictType
    }
    var index: Int {
        (dict?[Key.index()] as? NSRange)?.location ?? 0
    }
    var flag: Bool {
        dict?[Key.flag()] as? Bool ?? false
    }
    var paused: Bool {
        dict?[Key.paused()] as? Bool ?? false
    }
    var slider: Double {
        dict?[Key.slider()] as? Double ?? 0.0
    }
    var useRect: Bool {
        dict?[Key.rect()] as? Bool ?? false
    }
    var showOutlines: Bool {
        dict?[Key.outlines()] as? Bool ?? false
    }
    var showOpaques: Bool {
        dict?[Key.opaques()] as? Bool ?? false
    }
    var useClips: Bool {
        dict?[Key.clips()] as? Bool ?? false
    }
    var useCurves: Bool {
        dict?[Key.curves()] as? Bool ?? false
    }
    
    func controlsFor(_ pageName: String) -> [Control]? {
        switch PageID(rawValue: pageName) {
        case .LogIn: [
            Control(key: Key.dict(), closure: nil),
            Control(key: Key.button(), closure: { store, key in
                let tapcount = store.getValue(key: key) as? Int ?? 0
                print("\(tapcount)")
                store.setValue(value: tapcount + 1, key: key)
                return tapcount == 2 ? PageID.Home() : nil
            })]
        case .Home: [
            Control(key: Key.button(), closure: { store, key in
                store.setValue(value: 0, key: key)
                return PageID.LogIn()
            })]
        default:
            nil
        }
    }
}
