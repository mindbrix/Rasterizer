//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "RasterizerCoreGraphics.hpp"
#import "RasterizerSVG.hpp"

enum RasterizerType : int { kRasterizerMT = 0, kRasterizer, kCoreGraphics, kRasterizerCount };

@interface RasterizerView () <CALayerDelegate>

@property(nonatomic) std::vector<Rasterizer::Context> contexts;
@property(nonatomic) int rasterizerType;
@property(nonatomic) std::vector<CGPathRef> glyphCGPaths;
@property(nonatomic) std::vector<Rasterizer::Path> glyphPaths;
@property(nonatomic) std::vector<Rasterizer::Bounds> glyphBounds;
@property(nonatomic) Rasterizer::Scene scene;
@property(nonatomic) RasterizerCoreGraphics::CGScene cgscene;
@property(nonatomic) RasterizerCoreGraphics::BGRAColorConverter converter;
@property(nonatomic) CGFloat dimension;
@property(nonatomic) CGFloat phi;

@end


@implementation RasterizerView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self setWantsLayer:YES];
    [self setLayer:[CALayer layer]];
    self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
    self.layer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    self.layer.delegate = self;
    self.layer.bounds = self.bounds;
    self.layer.opaque = NO;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    _contexts.resize(4);
    self.dimension = 24;
    self.phi = (sqrt(5) - 1) / 2;
    [self writeGlyphGrid:@"AppleSymbols"];
    return self;
}

- (void)dealloc {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    NSFont *newFont = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"Times" size:14]];
    [self writeGlyphGrid:newFont.fontName];
    [self resetCTM:nil];
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
    [self.layer setNeedsDisplay];
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = _rasterizerType == kRasterizerMT ? @"Rasterizer (mt)" : _rasterizerType == kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

- (void)writeGlyphGrid:(NSString *)fontName {
    _scene.empty();
    _cgscene.empty();
    RasterizerCoreGraphics::writeGlyphGridToCGScene(fontName, _dimension, _phi, _cgscene);
    RasterizerCoreGraphics::writeCGSceneToScene(_cgscene, _scene);
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


#pragma mark - IBOutlet

- (IBAction)resetCTM:(id)sender {
    self.CTM = CGAffineTransformIdentity;
}

- (IBAction)toggleRasterizer:(id)sender {
    _rasterizerType = (++_rasterizerType) % kRasterizerCount;
    [self updateRasterizerLabel];
    [self redraw];
}


#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    size_t square = ceilf(sqrtf(float(_scene.paths.size())));
    CGContextConcatCTM(ctx, self.CTM);
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height, scale = (w < h ? w : h) / CGFloat(square * _dimension * _phi);
    CGContextConcatCTM(ctx, CGAffineTransformMake(scale, 0, 0, scale, 0, 0));
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    _contexts[0].setBitmap(bitmap);
    if (_rasterizerType == kCoreGraphics) {
        for (size_t i = 0; i < _cgscene.paths.size(); i++) {
            Rasterizer::Bounds bounds = RasterizerCoreGraphics::boundsFromCGRect(_cgscene.bounds[i]);
            Rasterizer::AffineTransform t = RasterizerCoreGraphics::transformFromCGAffineTransform(_cgscene.ctms[i]);
            Rasterizer::Bounds device = bounds.transform(ctm.concat(t)).integral();
            Rasterizer::Bounds clipped = device.intersected(_contexts[0].clip);
            if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
                CGContextSaveGState(ctx);
                CGContextSetFillColorWithColor(ctx, _cgscene.colors[i]);
                CGContextConcatCTM(ctx, _cgscene.ctms[i]);
                CGContextAddPath(ctx, _cgscene.paths[i]);
                CGContextFillPath(ctx);
                CGContextRestoreGState(ctx);
            }
        }
    } else {
        CGColorSpaceRef srcSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        uint32_t *bgras = (uint32_t *)alloca(_scene.paths.size() * sizeof(uint32_t));
        _converter.set(srcSpace, CGBitmapContextGetColorSpace(ctx));
        _converter.convert(& _scene.bgras[0], _scene.paths.size(), bgras);
        CGColorSpaceRelease(srcSpace);
        
        if (_rasterizerType == kRasterizerMT) {
            auto ctms = (Rasterizer::AffineTransform *)alloca(_contexts.size() * sizeof(Rasterizer::AffineTransform));
            size_t slice, ly, uy, count;
            slice = bitmap.height / _contexts.size(), slice = slice < 64 ? 64 : slice;
            for (count = ly = 0; ly < bitmap.height; ly = uy) {
                uy = ly + slice, uy = uy < bitmap.height ? uy : bitmap.height;
                ctms[count] = ctm;
                _contexts[count].setBitmap(bitmap);
                _contexts[count].clip.ly = ly;
                _contexts[count].clip.uy = uy;
                count++;
            }
            dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                for (size_t i = 0; i < self->_scene.paths.size(); i++)
                    Rasterizer::writePathToBitmap(self->_scene.paths[i], self->_scene.paths[i].bounds, ctms[idx].concat(self->_scene.ctms[i]), bgras[i], self->_contexts[idx]);
            });
        } else {
            for (size_t i = 0; i < _scene.paths.size(); i++)
                Rasterizer::writePathToBitmap(_scene.paths[i], _scene.paths[i].bounds, ctm.concat(_scene.ctms[i]), bgras[i], _contexts[0]);

        }
    }
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    if (_svgData) {
        _scene.empty();
        _cgscene.empty();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, _scene);
        RasterizerCoreGraphics::writeSceneToCGScene(_scene, _cgscene);
    }
    [self redraw];
}
@end
