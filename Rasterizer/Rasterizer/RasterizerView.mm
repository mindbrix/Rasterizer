//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#define RASTERIZER_SIMD 1
#import "RasterizerView.h"
#import "RasterizerCoreGraphics.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerTrueType.hpp"
#import "MetalLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) RasterizerCoreGraphics::CGTestScene testScene;
@property(nonatomic) RasterizerCoreGraphics::Scene textScene;
@property(nonatomic) BOOL useClip;
@property(nonatomic) BOOL useMetal;

@end


@implementation RasterizerView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    [self initLayer:_useMetal];
    [self writeGlyphGrid:[NSFont fontWithName:@"AppleSymbols" size:14]];
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    [self writeGlyphGrid:[[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"Times" size:14]]];
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
    [self.layer setNeedsDisplay];
}

- (void)initLayer:(BOOL)useMetal {
    [self setWantsLayer:YES];
    if (useMetal)
        [self setLayer:[MetalLayer layer]];
    else
        [self setLayer:[CALayer layer]];
    
    self.layer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    if (useMetal) {
        MetalLayer *layer = (MetalLayer *)self.layer;
        layer.layerDelegate = self;
    } else {
        self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
        self.layer.delegate = self;
    }
}

- (void)updateRasterizerLabel {
    [self.rasterizerLabel setHidden:YES];
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizerMT ? @"Rasterizer (mt)" : _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

- (void)writeGlyphGrid:(NSFont *)nsFont {
	CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize ((__bridge CFStringRef)nsFont.fontName, 1);
	CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
	CFRelease(fontRef);
	NSData *data = [NSData dataWithContentsOfURL:(__bridge NSURL *)url];
	CFRelease(url);
	_testScene.scene.empty();
    _testScene.cgscene.empty();
	RasterizerTrueType::Font font;
	uint8_t bgra[4] = { 0, 0, 0, 255 };
	const char *text = "One of the obvious motivations has been Objective-C, and in particular the notion of Software-ICs, components that are connected via dynamic messages. This is actually an instance of the Scripted Components (pdf) pattern, with the interesting twist that unlike most of the instances of this pattern, there is only a single programming language for both the scripts and the components, that language being Objective-C.";
	
    const char *label = "Hello, world!\nABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz 0123456789\n!@$%^&*()-_=+[]{};:\'\"\\|~,.<>/?";
	if (font.set(data.bytes, nsFont.fontName.UTF8String) != 0) {
//        RasterizerTrueType::writeGlyphs(font, float(nsFont.pointSize), bgra, RasterizerCoreGraphics::boundsFromCGRect(self.bounds), text, _testScene.scene.bgras, _testScene.scene.ctms, _testScene.scene.paths);
        RasterizerTrueType::writeGlyphGrid(font, float(nsFont.pointSize), bgra,  _testScene.scene.bgras, _testScene.scene.ctms, _testScene.scene.paths);
	}
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    [self updateRasterizerLabel];
    [self redraw];
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    if (event.keyCode == 8) {
        _useClip = !_useClip;
        [self redraw];
    } else if (event.keyCode == 46) {
        _useMetal = !_useMetal;
        [self toggleTimer];
        [self initLayer:_useMetal];
        [self redraw];
    } else {
        [super keyDown:event];
    }
}

#pragma mark - IBOutlet

- (IBAction)toggleRasterizer:(id)sender {
    _testScene.rasterizerType = (++_testScene.rasterizerType) % RasterizerCoreGraphics::CGTestScene::kRasterizerCount;
    [self updateRasterizerLabel];
    [self redraw];
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(Rasterizer::Buffer *)buffer forLayer:(CALayer *)layer {
    if (_testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kCoreGraphics)
        return;
        
    float s = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
	Rasterizer::AffineTransform view(self.CTM.a, self.CTM.b, self.CTM.c, self.CTM.d, self.CTM.tx, self.CTM.ty);
    Rasterizer::AffineTransform contentsScale(s, 0.f, 0.f, s, 0.f, 0.f);
    Rasterizer::AffineTransform ctm = contentsScale.concat(view);
    Rasterizer::Bitmap bitmap(nullptr, ceilf(s * w), ceilf(h * s), 0, 0);
    Rasterizer::Path clipPath;
    clipPath.sequence->addBounds(Rasterizer::Bounds(100, 100, 200, 200));
    Rasterizer::Bounds clip(0.f, 0.f, bitmap.width, bitmap.height);
    uint8_t svg[4] = { 0xCC, 0xCC, 0xCC, 0xCC }, font[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    buffer->clearColor = Rasterizer::GPU::Paint(_svgData ? svg : font);
    RasterizerCoreGraphics::writeTestScene(_testScene, ctm, clip, _useClip ? &clipPath : nullptr, nullptr, self.window.colorSpace.CGColorSpace, bitmap, buffer);
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    CGRect clip = CGRectApplyAffineTransform(CGContextGetClipBoundingBox(ctx), CGContextGetCTM(ctx));
    CGContextConcatCTM(ctx, self.CTM);
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    bitmap.clear(_svgData ? 0xCCCCCCCC : 0xFFFFFFFF);
    Rasterizer::Path clipPath;
    clipPath.sequence->addBounds(Rasterizer::Bounds(100, 100, 200, 200));
    RasterizerCoreGraphics::writeTestScene(_testScene, ctm, RasterizerCoreGraphics::boundsFromCGRect(clip), _useClip ? &clipPath : nullptr, ctx, CGBitmapContextGetColorSpace(ctx), bitmap, nullptr);
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    if (_svgData) {
        _testScene.scene.empty();
        _testScene.cgscene.empty();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, _testScene.scene);
    }
    [self redraw];
}
@end
