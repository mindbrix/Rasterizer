//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import "DemoView.h"
#import "RasterizerCG.hpp"
#import "RasterizerCoreText.hpp"
#import "RasterizerDemo.hpp"
#import "../private/RasterizerAPI+Internal.h"

@interface DemoView () <NSFontChanging, RASceneListDelegate>

@property(nonatomic) RasterizerDemo demo;
@property(nonatomic) NSFont *font;

@end


@implementation DemoView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    self.listDelegate = self;
    self.font = nil;
    _demo.setUseGPU(!self.useCG);
    return self;
}


#pragma mark - SceneListDelegate

- (BOOL)shouldRedrawAtTime:(double)time scale:(double)scale width:(double)width height:(double)height {
    return _demo.getShouldRedraw(time, width, height);
}

- (RASceneList *)getListAtTime:(double)time scale:(double)scale width:(double)width height:(double)height {
    RASceneList *list = [RASceneList new];
    list.list = _demo.getDrawList(time, width, height);
    return list;
}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    self.font = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"HelveticaNeue-Medium" size:14]];
}


#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    self.window.acceptsMouseMovedEvents = YES;
    return YES;
}

- (void)flagsChanged:(NSEvent *)event {
    _demo.onFlags(event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask);
}
- (void)keyDown:(NSEvent *)event {
//    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (_demo.onKeyDown(event.keyCode, event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask))
        ;
    else if (keyCode == 29) {  // 0
        self.useCG = !self.useCG;
        _demo.setUseGPU(!self.useCG);
    } else if (keyCode == 15) {  // R
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self.layer setNeedsDisplay];
    } else
        [super keyDown:event];
    
}
- (void)keyUp:(NSEvent *)event {
    _demo.onKeyUp(event.keyCode);
}

- (void)paste:(id)sender {
    NSString *pasted = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    _demo.onPaste(pasted.UTF8String, RaCG::BoundsFromCGRect(self.bounds));
}

- (void)magnifyWithEvent:(NSEvent *)event {
    _demo.onMagnify(float(1 + event.magnification), (event.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) != 0);
}
- (void)rotateWithEvent:(NSEvent *)event {
    _demo.onRotate(float(event.rotation / 10), (event.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) != 0);
}
- (void)scrollWheel:(NSEvent *)event {
    CGFloat inversion = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]) ? 1.0f : -1.0f;
    CGFloat dx = event.hasPreciseScrollingDeltas ? event.scrollingDeltaX : event.deltaX, dy = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY : event.deltaY;
    _demo.onTranslate(float(dx * inversion), float(-dy * inversion));
}
- (void)mouseDown:(NSEvent *)event {
    _demo.onMouseDown(float(event.locationInWindow.x), float(event.locationInWindow.y));
}
- (void)mouseDragged:(NSEvent *)event {
    _demo.onDrag(float(event.deltaX), float(-event.deltaY));
}
- (void)mouseMoved:(NSEvent *)event {
    CGSize size = self.window.frame.size;
    CGFloat x = event.locationInWindow.x, y = event.locationInWindow.y;
    if (x >= 0 && x <= size.width && y >= 0 && y <= size.height)
        _demo.onMouseMove(float(x), float(y));
}
- (void)mouseUp:(NSEvent *)event {
    _demo.onMouseUp(float(event.locationInWindow.x), float(event.locationInWindow.y));
}

#pragma mark - Properies

- (void)setFont:(NSFont *)font {
    _font = font ?: [NSFont fontWithName:@"HelveticaNeue-Medium" size:14];

    NSURL *url = RaCT::fontURL(_font.fontName);
    _demo.setFont(url.path.UTF8String, _font.fontName.UTF8String, _font.pointSize);
}

- (void)setPdfUrl:(NSURL *)pdfUrl {
    if (pdfUrl && pdfUrl.fileURL)
        _demo.setPdfUrl(pdfUrl.path.UTF8String);
}

- (void)setSvgUrl:(NSURL *)svgUrl {
    if (svgUrl && svgUrl.fileURL)
        _demo.setSvgUrl(svgUrl.path.UTF8String);
}
@end
