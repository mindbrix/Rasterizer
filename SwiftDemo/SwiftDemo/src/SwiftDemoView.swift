//
//  SwiftDemoView.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation
import RasterizerObjC


public class SwiftDemoView: RasterizerView, NSFontChanging {
    let demo = SwiftDemo()
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        listDelegate = demo
    }
    
    override public var acceptsFirstResponder: Bool {
        true
    }
    public func changeFont(_ sender: NSFontManager?) {
        demo.selectedFont = NSFontManager.shared.convert(NSFont(name: "HelveticaNeue-Medium", size: 14)!)
    }
    override public func keyDown(with event: NSEvent) {
        guard let characters = event.characters?.first?.lowercased(), let character = characters.first else {
            return
        }
        switch character {
        case "v":
            if (event.modifierFlags.contains(.command)) {
                let objects = NSPasteboard.general.readObjects(forClasses: [NSAttributedString.self])
                if let attrString = objects?.first as? NSAttributedString {
                    _ = demo.handleEvent(.paste(attributed: attrString))
                }
            }
        case "0":
            useCG.toggle()
        default:
            super.keyDown(with: event)
        }
    }
    override public func mouseMoved(with event: NSEvent) {
        guard let point = mousePoint(for: event) else {
            return
        }
        _ = demo.handleEvent(.mouseMove(p: point))
    }
    override public func mouseDown(with event: NSEvent) {
        guard let point = mousePoint(for: event) else {
            return
        }
        _ = demo.handleEvent(.mouseDown(p: point))
    }
    override public func mouseDragged(with event: NSEvent) {
        guard let point = mousePoint(for: event) else {
            return
        }
        _ = demo.handleEvent(.mouseMove(p: point))
    }
    override public func mouseUp(with event: NSEvent) {
        guard let point = mousePoint(for: event) else {
            return
        }
        _ = demo.handleEvent(.mouseUp(p: point))
    }
    override public func magnify(with event: NSEvent) {
        _ = demo.handleEvent(.magnify(scale: 1.0 + event.magnification))
    }
    override public func rotate(with event: NSEvent) {
        _ = demo.handleEvent(.rotate(angle: 0.1 * event.rotation))
    }
    override public func scrollWheel(with event: NSEvent) {
        let inversion = event.isDirectionInvertedFromDevice ? -1.0 : 1.0
        _ = demo.handleEvent(.translate(tx: event.deltaX, ty: inversion * event.deltaY))
    }
    
    func mousePoint(for event: NSEvent) -> CGPoint? {
        let point = CGPoint(x: event.locationInWindow.x, y: event.locationInWindow.y)
        guard point.x >= 0 && point.x < bounds.size.width,
              point.y >= 0 && point.y < bounds.size.height else {
            return nil
        }
        return point
    }
}
