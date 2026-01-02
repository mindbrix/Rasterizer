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
        let settings: Store.DictType = [
            Key.flag()   : false,
            Key.paused() : true,
            Key.slider() : 0.0,
            Key.rect(): false,
            Key.index()  : NSRange(location: 0, length: drawables.count)
        ]
        swiftApp.store.setValue(
            value: settings,
            key: Key.Settings())
        
        let debug: Store.DictType = [
            Key.outlines(): false,
            Key.opaques() : true,
            Key.clips()    : true,
            Key.curves()   : true
        ]
        swiftApp.store.setValue(
            value: debug,
            key: Key.debug())
        swiftApp.store.setValue(
            value: swiftApp.store.dict,
            key: Key.test())
        
        let testFlag = swiftApp.store.dict.getValue(keys: [Key.test(), Key.Settings(), Key.flag()])
        let a = 0
    }
    enum Event {
        case keyDown(character: Character, flags: NSEvent.ModifierFlags)
        case paste(attributed: NSAttributedString)
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
    
    var ctm = CGAffineTransform.identity
    var pageCtms: [String: CGAffineTransform] = [:]
    var appCtm = CGAffineTransform.identity
    var bounds = CGRect.zero
    var pastedScene: RAScene?
    var redraw = false
    
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
            swiftApp.mouseDown(bounds, p: p.applying(mouseCtmFor(swiftApp.pageName)))
        case .mouseMove(let p, _):
            swiftApp.mouseMoved(bounds, p: p.applying(mouseCtmFor(swiftApp.pageName)))
        case .mouseUp(let p, _):
            swiftApp.mouseUp(bounds, p: p.applying(mouseCtmFor(swiftApp.pageName)))
        case .magnify(let scale):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(scaleX: scale, y: scale), cx: bounds.midX, cy: bounds.midY)
        case .rotate(let angle):
            ctm = ctm.concatAroundCenter(t: CGAffineTransform(rotationAngle: CGFloat(angle)), cx: bounds.midX, cy: bounds.midY)
        case .translate(let tx, let ty):
            ctm.tx += tx
            ctm.ty += ty
        }
        redraw = true
        return true
    }
}

extension SwiftDemo: RASceneListDelegate {
    func shouldRedraw(atTime time: Double, width: Double, height: Double) -> Bool {
        bounds = CGRect(x: 0, y: 0, width: width, height: height)
        let should = redraw || !paused || swiftApp.shouldRedraw(swiftApp.pageName ?? "", in: bounds)
        redraw = false
        return should
    }
    func getListAtTime(_ time: Double, width: Double, height: Double) -> RASceneList {
        bounds = CGRect(x: 0, y: 0, width: width, height: height)
        let list = RASceneList()
        let t = paused ? slider : time - floor(time)
        list.add(drawables[index].getSceneAtTime(t, bounds: bounds, state: self))
        list.add(pastedScene ?? RAScene())
        
        appCtm = ctm.inverted()
        if let pageName = swiftApp.pageName {
            let pageCtm = CGAffineTransform.identity
            pageCtms[pageName] = pageCtm
            list.add(swiftApp.sceneFor(pageName, in: bounds), ctm: pageCtm.concatenating(appCtm), clip: .zero)
        }
        list.ctm = ctm
        list.useClips = useClips;
        list.useCurves = useCurves
        list.showOpaques = showOpaques
        list.showOutlines = showOutlines
        return list
    }
    
    func mouseCtmFor(_ page: String?) -> CGAffineTransform {
        ctm.concatenating(appCtm.concatenating(pageCtms[page ?? ""] ?? .identity)).inverted()
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
        case Settings
        case debug
        case test
        
        case flag
        case paused
        case slider
        case rect
        case index
        case clips
        case curves
        case opaques
        case outlines
        case reset = "reset ctm"
        case button
        
        func callAsFunction() -> String {
            rawValue
        }
    }
    
    var settings: Store.DictType? {
        swiftApp.store.getValue(key: Key.Settings()) as? Store.DictType
    }
    var debug: Store.DictType? {
        swiftApp.store.getValue(key: Key.debug()) as? Store.DictType
    }
    var index: Int {
        (settings?[Key.index()] as? NSRange)?.location ?? 0
    }
    var flag: Bool {
        settings?[Key.flag()] as? Bool ?? false
    }
    var paused: Bool {
        settings?[Key.paused()] as? Bool ?? false
    }
    var slider: Double {
        settings?[Key.slider()] as? Double ?? 0.0
    }
    var useRect: Bool {
        settings?[Key.rect()] as? Bool ?? false
    }
    var showOutlines: Bool {
        debug?[Key.outlines()] as? Bool ?? false
    }
    var showOpaques: Bool {
        debug?[Key.opaques()] as? Bool ?? false
    }
    var useClips: Bool {
        debug?[Key.clips()] as? Bool ?? false
    }
    var useCurves: Bool {
        debug?[Key.curves()] as? Bool ?? false
    }
    
    func controlsFor(_ pageName: String) -> [Control]? {
        switch PageID(rawValue: pageName) {
        case .LogIn: [
            Control(key: Key.Settings(), mode: .readonly, closure: nil),
            Control(key: Key.debug(), mode: .readonly, closure: nil),
            Control(key: Key.reset(), mode: .button, closure: { [weak self] _, _ in
                self?.ctm = .identity
                return nil
            }),
            Control(key: Key.button(), mode: .button, closure: { store, key in
                let tapcount = store.getValue(key: key) as? Int ?? 0
                print("\(tapcount)")
                store.setValue(value: tapcount + 1, key: key)
                return tapcount == 2 ? PageID.Home() : nil
            }),
            Control(key: Key.test(), mode: .mutable, closure: nil),
        ]
        case .Home: [
            Control(key: Key.button(), mode: .button, closure: { store, key in
                store.setValue(value: 0, key: key)
                return PageID.LogIn()
            })]
        default:
            nil
        }
    }
}
