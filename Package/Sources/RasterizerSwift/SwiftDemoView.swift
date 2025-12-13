//
//  SwiftDemoView.swift
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

import Foundation
import RasterizerObjC


public class SwiftDemoView: RasterizerView {
    let demo = SwiftDemo()
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        listDelegate = demo
    }
    
    override public var acceptsFirstResponder: Bool {
        true
    }
    override public func becomeFirstResponder() -> Bool {
        self.window?.acceptsMouseMovedEvents = true
        return true
    }
    override public func keyDown(with event: NSEvent) {
        guard let characters = event.characters?.first?.lowercased(), let character = characters.first else {
            return
        }
        demo.bounds = self.bounds
        if !demo.handleEvent(.keyDown(character: character, flags: event.modifierFlags)) {
            if character == "0" {
                useCG.toggle()
            } else {
                super.keyDown(with: event)
            }
        }
    }
    override public func mouseMoved(with event: NSEvent) {
        guard let point = mousePoint(for: event) else {
            return
        }
        _ = demo.handleEvent(.mouseMove(x: point.x, y: point.y, flags: event.modifierFlags))
    }
    override public func magnify(with event: NSEvent) {
        _ = demo.handleEvent(.magnify(scale: 1.0 + event.magnification))
    }
    override public func mouseDragged(with event: NSEvent) {
        _ = demo.handleEvent(.translate(tx: event.deltaX, ty: -event.deltaY))
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
