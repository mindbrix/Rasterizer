#!/bin/zsh
#
#   Copyright 2026 Nigel Timothy Barber - nigel@mindbrix.co.uk
#   SPDX-License-Identifier: MIT
#
# bench.sh: rebuilds & runs the Rasterizer vs Vello vs Skia vs Rive benchmark over TestFiles from scratch.
#
#   Benchmarks/bench.sh [--rounds N] [--size WxH] [--frames N] [--configs a,b,..] [--rev <git rev>] [--metallib path] [--contexts N]
#                       [--build-only]
#   Benchmarks/bench.sh --viewer [file] [--build-only]
#
# Everything is generated under Benchmarks/build/, which ignores itself:
#   src/        the harness sources, written from this script each run, so this script is their only copy
#   vendor/     vello_encoding 0.11.0 from crates.io, checksummed & patched (see below), & rive-runtime & ply from GitHub
#   target/     cargo builds, rive/ the Rive renderer's library, bin/ the tools, dumps/<rev>/ the exported TestFiles, metallib/<rev>/
#   results/<stamp>-<rev>-<WxH>/  info.txt (machine, versions), runs.jsonl, quality.jsonl, summary.md, summary.csv, png/
#
# Needs: Xcode command line tools (clang), python3, curl, & Rust (rustup installs the pinned toolchain). The first Skia build
# downloads prebuilt Skia from github.com/rust-skia/skia-binaries. The Rasterizer shaders are compiled from the checkout when the
# Metal toolchain is installed (xcodebuild -downloadComponent MetalToolchain); otherwise pass --metallib, or the newest Release
# build in DerivedData is used, which may not match the source. The first Rive build needs git & the Metal toolchain: it
# sparse-clones rive-runtime's renderer & core sources at RIVE_TAG (~10 MB), & ply (the lexer its shader minifier uses), checking
# both commits, then builds Rive's shaders with its own Makefile & its sources with clang.
#
# --rev benchmarks another commit: its tree is exported with git archive, & its TestFiles, parsers & shaders are used.
# --contexts N caps Rasterizer's contexts at N, e.g. 1 for a baseline without any slicing.
# --viewer builds every renderer & raviewer, an interactive viewer that switches between them on the same pan, zoom & rotation
#   (see raviewer.mm), & opens file in it: an RSD dump, or a TestFiles SVG or PDF (default tiger.svg).
#
# How it works:
# - rabench loads each TestFile exactly as RasterizerDemo does (RaSVG / RaPDF page 0), exports the parsed SceneList as an RSD dump,
#   and benchmarks Rasterizer offscreen with a copy of RasterizerLayer's encode loop: keep that in sync with RasterizerLayer -draw.
# - vello_bench (Vello 0.11), skia_bench (skia-safe 0.153, Metal, Ganesh) & rivebench (the Rive renderer, Metal) replay the
#   dumps, so every library draws identical geometry. rsd is their shared reader, view formula, culling, clip runs, stats & PNG
#   I/O, & rsdcompare diffs PNGs. rivebench is C++, so it mirrors rsd's reader, view, culling, clip runs & stats.
# - Frames are serialised (build, submit, wait): CPU is to submit, submit-to-done to GPU completion, wall the whole frame, &
#   Rasterizer also reports Metal GPU time. 3 warm-up then N timed frames per run; fit (static) & sweep (1x to 8x zoom) modes;
#   rounds interleave libraries; summary.md takes the best median across rounds, & compares each fit frame with Rasterizer's.
# - Vello's dynamic GPU buffers are fixed sizes picked for Paris-30K & overflow silently (blank frames on the big PDFs), so
#   vello_encoding is patched to scale them 16x. Ganesh without MSAA falls back to CPU masks for large AA paths; Chrome renders
#   with MSAA. Skia's Graphite backend isn't benchmarked, as its internal 4x MSAA & depth-tested draws aren't comparable.
set -euo pipefail

HERE=${0:a:h}; REPO=${HERE:h}; W=$HERE/build
RUST=1.99.0
VE_VERSION=0.11.0
VE_SHA256=b1fb9548d2b46b0f79137b5b1d208ecbd76d432f74e7a2ba838615d117f11532
RIVE_TAG=runtime-v0.1.698
RIVE_SHA=03cd2cdf6322859f62e9edfcfc8040ce80b37ccc
PLY_TAG=3.11
PLY_SHA=0f398b72618c1564d71f7dc0558e6722b241875a
ALL_CONFIGS=rasterizer,vello,vello_retained,skia_ganesh,rive
ROUNDS=3; SIZE=2048x2048; FRAMES=30; CONFIGS=$ALL_CONFIGS; REV=; ML=${METALLIB:-}; CONTEXTS=; BUILD_ONLY=0; VIEWER=0; VFILE=
while (( $# )); do
  case $1 in
    --rounds) ROUNDS=$2; shift ;;
    --size) SIZE=$2; shift ;;
    --frames) FRAMES=$2; shift ;;
    --configs) CONFIGS=$2; shift ;;
    --rev) REV=$2; shift ;;
    --metallib) ML=${2:a}; shift ;;
    --contexts) CONTEXTS=$2; shift ;;
    --build-only) BUILD_ONLY=1 ;;
    --viewer) VIEWER=1; if (( $# > 1 )) && [[ $2 != --* ]]; then VFILE=${2:a}; shift; fi ;;
    *) sed -n '2,10p' $0; exit 2 ;;
  esac
  shift
done
(( VIEWER )) && CONFIGS=$ALL_CONFIGS    # The viewer has every renderer
[[ ,$CONFIGS, == *,rasterizer,* ]] || CONFIGS=rasterizer,$CONFIGS
die() { print -u2 "bench.sh: $*"; exit 1 }
step() { print -P "%B==> $*%b" }
mkdir -p $W; print '*' > $W/.gitignore

# Rasterizer source: this checkout, or --rev exported
if [[ -n $REV ]]; then
  SHA=$(git -C $REPO rev-parse --short $REV) || die "unknown rev $REV"
  RA=$W/rasterizer-$SHA
  if [[ ! -d $RA ]]; then
    step "Exporting Rasterizer $SHA"
    mkdir -p $RA.tmp && git -C $REPO archive $SHA | tar -x -C $RA.tmp && mv $RA.tmp $RA
  fi
else
  RA=$REPO
  SHA=$(git -C $REPO rev-parse --short HEAD)
  git -C $REPO diff --quiet HEAD -- Package Rasterizer/Demo TestFiles || SHA=$SHA-dirty
fi
INC=$RA/Package/Sources/RasterizerCpp/include
PDFIUM=$RA/Rasterizer/Frameworks/pdfium.xcframework/macos-arm64_x86_64    # Linked by revs whose RasterizerPDF.hpp used pdfium
[[ -d $PDFIUM ]] || PDFIUM=
[[ -f $INC/Rasterizer.hpp ]] || die "no Rasterizer sources at $RA"

step "Writing sources"
S=$W/src
w() { mkdir -p ${1:h}; cat > $1.new; cmp -s $1.new $1 && rm $1.new || mv $1.new $1 }    # Keeps mtimes, so builds stay incremental

w $S/rust-toolchain.toml <<'__RUST_TOOLCHAIN_TOML__'
[toolchain]
channel = "1.99.0"
profile = "minimal"
__RUST_TOOLCHAIN_TOML__

w $S/ra/rabench.mm <<'__RA_RABENCH_MM__'
//  rabench: loads a TestFiles SVG/PDF exactly as the Rasterizer demo does, then either
//    export <in> <out.rsd>                          writes the parsed SceneList as a neutral binary dump (see RSD format below)
//    bench  <in> <metallib> [opts]                  renders it offscreen with the Metal renderer & times each frame
//  bench opts: --size WxH  --frames N  --zoom Z  --png out.png  --pngframe k  --contexts N
//
//  RSD v1 (little endian). Coordinates are Rasterizer's: y up, device = list ctm * scene ctm * draw ctm.
//    "RSD1" u32 version  f32 bounds[4] (SceneList::bounds())
//    u32 nscenes  { f32 ctm[6]  f32 clip[4] }
//    u32 npaths   { u32 n  u8 types[n] (Ra::Geometry::Type, one per point)  pad to 4  f32 pts[2n] }
//    u32 ngrads   { u32 type (1 linear, 2 radial)  f32 ctm[6] (Bitmap::ctm)  u32 n  f32 locs[n]  u8 bgra[4n] }
//    u32 nimages  { u32 w  u32 h  u8 bgra[4wh] }
//    u32 ndraws   { DumpDraw }   visible draws only, in draw order

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <ImageIO/ImageIO.h>
#import <CoreServices/CoreServices.h>
#import "Rasterizer.hpp"
#import "RasterizerRenderer.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerPDF.hpp"
#import <unordered_map>
#import <string>
#import <algorithm>
#import <mach/mach_time.h>

static double now() {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0)
        mach_timebase_info(& tb);
    return double(mach_absolute_time()) * tb.numer / tb.denom * 1e-9;
}

static bool hasSuffix(const std::string& s, const char *suffix) {
    size_t n = strlen(suffix);
    if (s.size() < n)
        return false;
    std::string tail = s.substr(s.size() - n);
    std::transform(tail.begin(), tail.end(), tail.begin(), ::tolower);
    return tail == suffix;
}

// Builds the list as RasterizerDemo::getDrawList() does for a document: one scene, the loader's ctm, page 0
static bool loadList(const char *path, Ra::SceneList& list) {
    Ra::SceneRef scene;
    Ra::Transform m;
    std::string s(path);
    if (hasSuffix(s, ".svg"))
        m = RaSVG::addSvgToScene(path, scene);
    else if (hasSuffix(s, ".pdf"))
        m = RaPDF::addPdfPageToScene(path, 0, scene);
    else
        return false;
    list.addScene(scene, m);
    return list.pathsCount() > 0;
}

#pragma mark - Export

#pragma pack(push, 1)
struct DumpDraw {
    uint32_t scene, path;  float ctm[6];
    uint8_t paintType, flags;  uint16_t pad;  uint8_t bgra[4];  uint32_t paintIndex;
    float width;  float clip[4];  uint32_t clipPath;
};
#pragma pack(pop)
static_assert(sizeof(DumpDraw) == 68, "DumpDraw layout");

struct Writer {
    FILE *f;
    void u32(uint32_t v) { fwrite(& v, 4, 1, f); }
    void f32s(const float *v, size_t n) { fwrite(v, 4, n, f); }
    void bytes(const void *v, size_t n) { fwrite(v, 1, n, f); }
    void pad4(size_t n) { static const uint8_t z[4] = { 0 }; if (n & 3) fwrite(z, 1, 4 - (n & 3), f); }
};

static int exportList(const Ra::SceneList& list, const char *out) {
    std::unordered_map<Ra::Geometry *, uint32_t> paths;
    std::unordered_map<Ra::Bitmap *, uint32_t> grads, images;
    std::vector<Ra::Geometry *> pathList;  std::vector<Ra::Bitmap *> gradList, imageList;  std::vector<bool> gradRadial;
    std::vector<DumpDraw> draws;
    size_t fills = 0, strokes = 0, hairlines = 0, clipRects = 0, clipPathDraws = 0, nGrad = 0, nImage = 0, points = 0;
    auto pathIndex = [&](Ra::Geometry *g) {
        auto it = paths.find(g);
        if (it != paths.end())
            return it->second;
        uint32_t idx = uint32_t(pathList.size());
        paths.emplace(g, idx), pathList.push_back(g), points += g->types.end;
        return idx;
    };
    auto bitmapIndex = [](Ra::Bitmap *b, std::unordered_map<Ra::Bitmap *, uint32_t>& map, std::vector<Ra::Bitmap *>& vec) {
        auto it = map.find(b);
        if (it != map.end())
            return it->second;
        uint32_t idx = uint32_t(vec.size());
        map.emplace(b, idx), vec.push_back(b);
        return idx;
    };
    for (size_t si = 0; si < list.scenes.size(); si++) {
        const Ra::Scene& scene = *list.scenes[si].ptr;
        for (size_t i = 0; i < scene.count(); i++) {
            const Ra::Draw& draw = scene.draws[i];
            if (draw.flags & Ra::Draw::kInvisible)
                continue;
            DumpDraw d;  memset(& d, 0, sizeof(d));
            d.scene = uint32_t(si), d.path = pathIndex(draw.path.ptr);
            memcpy(d.ctm, & draw.ctm, sizeof(d.ctm));
            d.paintType = uint8_t(draw.paint.type), d.flags = draw.flags;
#if !RA_CLIP_RULE
            d.flags |= draw.clipPath.ptr ? 1 << 5 : 0;     // Revisions without Draw::kClipEvenOdd clip even-odd
#endif
            d.bgra[0] = draw.paint.color.b, d.bgra[1] = draw.paint.color.g, d.bgra[2] = draw.paint.color.r, d.bgra[3] = draw.paint.color.a;
            d.paintIndex = ~0u;
            if (draw.paint.isGradient())
                d.paintIndex = bitmapIndex(draw.paint.bitmap.ptr, grads, gradList), nGrad++, gradRadial.resize(gradList.size()), gradRadial[d.paintIndex] = draw.paint.type == Ra::Paint::kRadial;
            else if (draw.paint.isImage())
                d.paintIndex = bitmapIndex(draw.paint.bitmap.ptr, images, imageList), nImage++;
            d.width = draw.width;
            memcpy(d.clip, & draw.clip, sizeof(d.clip));
            d.clipPath = draw.clipPath.ptr ? pathIndex(draw.clipPath.ptr) : ~0u;
            draws.push_back(d);
            draw.width == 0.f ? fills++ : draw.width < 0.f ? hairlines++ : strokes++;
            clipRects += !draw.clip.isHuge(), clipPathDraws += draw.clipPath.ptr != nullptr;
        }
    }
    Writer w { fopen(out, "wb") };
    if (!w.f)
        return fprintf(stderr, "cannot write %s\n", out), 1;
    w.bytes("RSD1", 4), w.u32(1);
    Ra::Bounds b = list.bounds();
    w.f32s(& b.lx, 4);
    w.u32(uint32_t(list.scenes.size()));
    for (size_t si = 0; si < list.scenes.size(); si++)
        w.f32s(& list.ctms[si].a, 6), w.f32s(& list.clips[si].lx, 4);
    w.u32(uint32_t(pathList.size()));
    for (Ra::Geometry *g : pathList) {
        g->validate();
        w.u32(uint32_t(g->types.end)), w.bytes(g->types.base, g->types.end), w.pad4(g->types.end);
        w.f32s(g->points.base, g->points.end);
    }
    w.u32(uint32_t(gradList.size()));
    for (size_t gi = 0; gi < gradList.size(); gi++) {
        Ra::Bitmap *g = gradList[gi];
        size_t n = g->locs.end();
        w.u32(gradRadial[gi] ? 2 : 1), w.f32s(& g->ctm.a, 6), w.u32(uint32_t(n));
        w.f32s(& g->locs[0], n), w.bytes(& g->colors[0], 4 * n);
    }
    w.u32(uint32_t(imageList.size()));
    for (Ra::Bitmap *im : imageList)
        w.u32(uint32_t(im->w)), w.u32(uint32_t(im->h)), w.bytes(& im->colors[0], 4 * im->w * im->h);
    w.u32(uint32_t(draws.size()));
    w.bytes(draws.data(), draws.size() * sizeof(DumpDraw));
    fclose(w.f);
    printf("{\"draws\":%zu,\"fills\":%zu,\"strokes\":%zu,\"hairlines\":%zu,\"uniquePaths\":%zu,\"points\":%zu,"
           "\"gradientDraws\":%zu,\"imageDraws\":%zu,\"clipRectDraws\":%zu,\"clipPathDraws\":%zu}\n",
           draws.size(), fills, strokes, hairlines, pathList.size(), points, nGrad, nImage, clipRects, clipPathDraws);
    return 0;
}

#pragma mark - Offscreen Metal renderer: RasterizerLayer's pipelines & encode loop, drawing to a texture

// As RasterizerLayer's MetalCache, whose entries expire after 10 s unused, flushed once per frame
struct TextureCache {
    struct Entry { id<MTLTexture> texture;  double timestamp; };
    std::map<size_t, Entry> map;
    id<MTLTexture> entryFor(const Ra::Paint& image, id<MTLDevice> device) {
        auto it = map.find(image.hash());
        if (it != map.end())
            return it->second.timestamp = CACurrentMediaTime(), it->second.texture;
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:image.bitmap->w height:image.bitmap->h mipmapped:NO];
        desc.storageMode = MTLStorageModeShared, desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        [texture replaceRegion:MTLRegionMake2D(0, 0, image.bitmap->w, image.bitmap->h) mipmapLevel:0 withBytes:& image.bitmap->colors[0] bytesPerRow:image.bitmap->w * sizeof(Ra::Color)];
        map.emplace(image.hash(), Entry{ texture, CACurrentMediaTime() });
        return texture;
    }
    void flush() {
        double t = CACurrentMediaTime();
        std::vector<size_t> expired;
        for (const auto& entry : map)
            if (t - entry.second.timestamp > 10)
                expired.emplace_back(entry.first);
        for (auto key : expired)
            map.erase(key);
    }
};


// RA_CLIP_MASK, set by bench.sh when Rasterizer.hpp has Buffer::kClipMask, selects RasterizerLayer's anti-aliased clip mask & lazy
// render passes; otherwise it's the stencil clipping & eager passes of earlier revisions, so --rev can benchmark either.
// RA_CLIP_CLEAR_CELLS, set when Rasterizer.hpp has Instance::kClipClear, zeroes each mask with a cell among its instances; earlier
// clip mask revisions zero it with a scissored clear pass. RA_CLIP_IN_PASS, set when Shaders.metal reads the mask with framebuffer
// fetch, renders each mask in the drawable pass as its color attachment 1, so a frame with clip masks has no mask passes.
// RA_BLEND, set when Rasterizer.h has blend modes, draws a frame with them using the instance pipelines that blend in the shader.
// RA_IMAGES, set when Shaders.metal samples images from an argument buffer, binds the frame's images once per drawable pass.
// RA_EDGES_HEIGHT, set when Rasterizer.hpp's allocator passes have heights, limits each edges pass to the accumulation rows it uses.
// RA_CONTEXT_LIMIT, set when RasterizerRenderer.hpp keeps its contexts in a vector, lets --contexts N cap how many a frame uses
#if RA_CLIP_MASK
static const MTLPixelFormat kDepthFormat = MTLPixelFormatDepth32Float;
#else
static const MTLPixelFormat kDepthFormat = MTLPixelFormatDepth32Float_Stencil8;
#endif

struct Offscreen {
    id<MTLDevice> device;  id<MTLCommandQueue> queue;  id<MTLLibrary> library;
    id<MTLRenderPipelineState> quadEdges, fastEdges, fastMolecules, quadMolecules, opaques, instances;
    id<MTLDepthStencilState> instancesDepthState, opaquesDepthState;
#if RA_CLIP_IN_PASS
    id<MTLRenderPipelineState> opaquesMasked, instancesMasked;  id<MTLDepthStencilState> clipMaskDepthState;
#endif
#if RA_BLEND
    id<MTLRenderPipelineState> instancesBlend, instancesBlendMasked, instancesClipBlend;
#endif
#if RA_IMAGES
    id<MTLArgumentEncoder> imageEncoder;
    id<MTLBuffer> noImagesBuffer;    // Bound for frames without images, as the image functions need a buffer
    // The argument buffer of the last frame's images, kept while they're the same, with their hashes, the first draw of each
    // distinct one, & their textures
    id<MTLBuffer> imagesBuffer;  std::vector<size_t> imageHashes, imageFirsts;  NSArray<id<MTLTexture>> *imageTextures = @[];
#endif
    id<MTLTexture> target, depthTexture, accumulationTexture;
#if RA_CLIP_MASK
    id<MTLRenderPipelineState> instancesClip, clipMask;  id<MTLTexture> clipMaskTexture;
#if !RA_CLIP_CLEAR_CELLS
    id<MTLRenderPipelineState> clipClear;
#endif
#else
    id<MTLRenderPipelineState> stencil;  id<MTLDepthStencilState> stencilDepthState, instancesClipDepthState, opaquesClipDepthState;
#endif
    TextureCache textureCache;
    MTLPixelFormat pixelFormat = MTLPixelFormatBGRA8Unorm;
#if RA_QUAD_INDICES
    // Quads draw as 2 triangles, (0, 1, 2) & (2, 1, 3), of quad q's vertices 4q to 4q + 3, in chunks of kQuadChunk with a base vertex
    static constexpr NSUInteger kQuadChunk = 1 << 18;
    id<MTLBuffer> quadIndices;
#endif

    bool init(const char *metallib, size_t w, size_t h) {
        device = MTLCreateSystemDefaultDevice(), queue = [device newCommandQueue];
        NSError *error = nil;
        library = [device newLibraryWithURL:[NSURL fileURLWithPath:@(metallib)] error:& error];
        if (!library)
            return fprintf(stderr, "metallib: %s\n", error.localizedDescription.UTF8String), false;
#if RA_QUAD_INDICES
        std::vector<uint32_t> indices(kQuadChunk * 6);
        for (uint32_t q = 0, *dst = indices.data(); q < kQuadChunk; q++, dst += 6)
            dst[0] = 4 * q, dst[1] = 4 * q + 1, dst[2] = 4 * q + 2, dst[3] = 4 * q + 2, dst[4] = 4 * q + 1, dst[5] = 4 * q + 3;
        quadIndices = [device newBufferWithBytes:indices.data() length:indices.size() * sizeof(uint32_t) options:MTLResourceStorageModeShared];
#endif
#if RA_IMAGES
        // Images need tier 2 argument buffers, else the fragment functions are made without image sampling
        bool hasImages = device.argumentBuffersSupport == MTLArgumentBuffersTier2;
        MTLFunctionConstantValues *constants = [MTLFunctionConstantValues new];
        [constants setConstantValue:& hasImages type:MTLDataTypeBool atIndex:0];
        auto fn = [&](const char *name) { return name ? [library newFunctionWithName:@(name) constantValues:constants error:nil] : nil; };
#else
        auto fn = [&](const char *name) { return name ? [library newFunctionWithName:@(name)] : nil; };
#endif

        MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
        d.depthWriteEnabled = YES, d.depthCompareFunction = MTLCompareFunctionGreater;
        opaquesDepthState = [device newDepthStencilStateWithDescriptor:d];
        d.depthWriteEnabled = NO;
        instancesDepthState = [device newDepthStencilStateWithDescriptor:d];
#if RA_CLIP_IN_PASS
        d.depthCompareFunction = MTLCompareFunctionAlways;    // Clip mask cells are always written, & never write depth
        clipMaskDepthState = [device newDepthStencilStateWithDescriptor:d];
        d.depthCompareFunction = MTLCompareFunctionGreater;
#endif
#if !RA_CLIP_MASK
        d.frontFaceStencil.stencilCompareFunction = MTLCompareFunctionNotEqual, d.frontFaceStencil.depthStencilPassOperation = MTLStencilOperationKeep, d.frontFaceStencil.readMask = 0x01;
        d.backFaceStencil = d.frontFaceStencil;
        instancesClipDepthState = [device newDepthStencilStateWithDescriptor:d];
        d.depthWriteEnabled = YES;
        opaquesClipDepthState = [device newDepthStencilStateWithDescriptor:d];
        MTLDepthStencilDescriptor *s = [MTLDepthStencilDescriptor new];
        s.depthWriteEnabled = NO, s.depthCompareFunction = MTLCompareFunctionAlways;
        s.frontFaceStencil.stencilCompareFunction = MTLCompareFunctionAlways, s.frontFaceStencil.depthStencilPassOperation = MTLStencilOperationIncrementWrap;
        s.backFaceStencil.stencilCompareFunction = MTLCompareFunctionAlways, s.backFaceStencil.depthStencilPassOperation = MTLStencilOperationDecrementWrap;
        stencilDepthState = [device newDepthStencilStateWithDescriptor:s];
        MTLRenderPipelineDescriptor *sd = [MTLRenderPipelineDescriptor new];
        sd.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid, sd.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
        sd.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8, sd.vertexFunction = fn("stencil_vertex_main");
        stencil = [device newRenderPipelineStateWithDescriptor:sd error:nil];
#endif

        MTLRenderPipelineDescriptor *p = [MTLRenderPipelineDescriptor new];
        p.colorAttachments[0].pixelFormat = pixelFormat;
        p.depthAttachmentPixelFormat = kDepthFormat;
#if !RA_CLIP_MASK
        p.stencilAttachmentPixelFormat = kDepthFormat;
#endif
        p.colorAttachments[0].blendingEnabled = NO;
        p.vertexFunction = fn("opaques_vertex_main"), p.fragmentFunction = fn("opaques_fragment_main");
        opaques = [device newRenderPipelineStateWithDescriptor:p error:nil];
        p.colorAttachments[0].blendingEnabled = YES;
        p.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd, p.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        p.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne, p.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        p.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha, p.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        p.vertexFunction = fn("instances_vertex_main"), p.fragmentFunction = fn("instances_fragment_main");
        instances = [device newRenderPipelineStateWithDescriptor:p error:nil];
#if RA_IMAGES
        if (hasImages) {
            imageEncoder = [p.fragmentFunction newArgumentEncoderWithBufferIndex:2];
            noImagesBuffer = [device newBufferWithLength:imageEncoder.encodedLength options:MTLResourceStorageModeShared];
        }
#endif
#if RA_BLEND
        // Frames with blend modes blend in the shader, reading the target with framebuffer fetch
        auto blendPipeline = [&](const char *name) {
            p.colorAttachments[0].blendingEnabled = NO, p.fragmentFunction = fn(name);
            id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:p error:nil];
            p.colorAttachments[0].blendingEnabled = YES, p.fragmentFunction = fn("instances_fragment_main");
            return state;
        };
        instancesBlend = blendPipeline("instances_blend_fragment_main");
#endif
#if RA_CLIP_IN_PASS
        // Frames with clip masks render them into the drawable pass's color attachment 1, which only clip mask instances write
        p.colorAttachments[1].pixelFormat = MTLPixelFormatR8Unorm, p.colorAttachments[1].writeMask = MTLColorWriteMaskNone;
        instancesMasked = [device newRenderPipelineStateWithDescriptor:p error:nil];
#if RA_BLEND
        instancesBlendMasked = blendPipeline("instances_blend_fragment_main");
        instancesClipBlend = blendPipeline("instances_clip_blend_fragment_main");
#endif
        p.colorAttachments[0].blendingEnabled = NO, p.vertexFunction = fn("opaques_vertex_main"), p.fragmentFunction = fn("opaques_fragment_main");
        opaquesMasked = [device newRenderPipelineStateWithDescriptor:p error:nil];
        p.colorAttachments[0].blendingEnabled = YES, p.vertexFunction = fn("instances_vertex_main");
#endif
#if RA_CLIP_MASK
        p.fragmentFunction = fn("instances_clip_fragment_main");
        instancesClip = [device newRenderPipelineStateWithDescriptor:p error:nil];
#endif
#if RA_CLIP_IN_PASS
        p.colorAttachments[1].pixelFormat = MTLPixelFormatInvalid, p.colorAttachments[1].writeMask = MTLColorWriteMaskAll;
#endif
        p.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
        p.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne, p.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        p.depthAttachmentPixelFormat = MTLPixelFormatInvalid, p.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;
        p.vertexFunction = fn("edges_vertex_main"), p.fragmentFunction = fn("quad_edges_fragment_main");
        quadEdges = [device newRenderPipelineStateWithDescriptor:p error:nil];
        p.fragmentFunction = fn("fast_edges_fragment_main");
        fastEdges = [device newRenderPipelineStateWithDescriptor:p error:nil];
        p.vertexFunction = fn("fast_molecules_vertex_main"), p.fragmentFunction = fn("fast_molecules_fragment_main");
        fastMolecules = [device newRenderPipelineStateWithDescriptor:p error:nil];
        p.vertexFunction = fn("quad_molecules_vertex_main"), p.fragmentFunction = fn("quad_molecules_fragment_main");
        quadMolecules = [device newRenderPipelineStateWithDescriptor:p error:nil];
#if RA_CLIP_MASK
        MTLRenderPipelineDescriptor *m = [MTLRenderPipelineDescriptor new];
        m.colorAttachments[0].pixelFormat = MTLPixelFormatR8Unorm;
#if RA_CLIP_CLEAR_CELLS
        bool clipClear = true;
#else
        m.vertexFunction = fn("clip_clear_vertex_main"), m.fragmentFunction = fn("clip_clear_fragment_main");
        clipClear = [device newRenderPipelineStateWithDescriptor:m error:nil];
#endif
#if RA_CLIP_IN_PASS
        m.colorAttachments[0].pixelFormat = pixelFormat, m.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
        m.colorAttachments[1].pixelFormat = MTLPixelFormatR8Unorm, m.depthAttachmentPixelFormat = kDepthFormat;
#endif
        m.vertexFunction = fn("instances_vertex_main"), m.fragmentFunction = fn("clip_mask_fragment_main");
        clipMask = [device newRenderPipelineStateWithDescriptor:m error:nil];
        bool clipping = instancesClip && clipMask && clipClear;
#else
        bool clipping = stencil;
#endif
        if (!(clipping && opaques && instances && quadEdges && fastEdges && fastMolecules && quadMolecules))
            return fprintf(stderr, "pipeline creation failed: does the metallib match the Rasterizer sources?\n"), false;

        MTLTextureDescriptor *t = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat width:w height:h mipmapped:NO];
        t.storageMode = MTLStorageModePrivate, t.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        target = [device newTextureWithDescriptor:t];
        t.pixelFormat = kDepthFormat, t.usage = MTLTextureUsageRenderTarget;
        depthTexture = [device newTextureWithDescriptor:t];
        t.pixelFormat = MTLPixelFormatR32Float, t.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        accumulationTexture = [device newTextureWithDescriptor:t];
#if RA_CLIP_MASK
        t.pixelFormat = MTLPixelFormatR8Unorm;
        clipMaskTexture = [device newTextureWithDescriptor:t];
#endif
        return true;
    }

    // RasterizerLayer -draw, with drawable.texture replaced by the offscreen target & no present
    id<MTLCommandBuffer> encode(RenderBuffer *renderBuffer) {
        Ra::Buffer *buffer = & renderBuffer->buffer;
        id<MTLBuffer> mtlBuffer = renderBuffer->mtlBuffer;
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
        desc.storageMode = MTLStorageModeShared, desc.usage = MTLTextureUsageShaderRead;
        size_t w = kColorTextureWidth, h = (buffer->pathsCount + w - 0) / w, th = buffer->texCount;
        desc.width = w, desc.height = h + th;
        id<MTLTexture> colorTexture = [device newTextureWithDescriptor:desc];
        if (buffer->base) {
            [colorTexture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:buffer->base + buffer->colors bytesPerRow:w * sizeof(Ra::Color)];
            if (buffer->texCount)
                [colorTexture replaceRegion:MTLRegionMake2D(0, h, w, th) mipmapLevel:0 withBytes:buffer->base + buffer->texStrips bytesPerRow:w * sizeof(Ra::Color)];
        }
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];

        MTLRenderPassDescriptor *drawableDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        drawableDescriptor.colorAttachments[0].texture = target;
        drawableDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        drawableDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(buffer->params.clearColor.r / 255.0, buffer->params.clearColor.g / 255.0, buffer->params.clearColor.b / 255.0, buffer->params.clearColor.a / 255.0);
        drawableDescriptor.depthAttachment.texture = depthTexture;
        drawableDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
        drawableDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
        drawableDescriptor.depthAttachment.clearDepth = 0;
#if RA_CLIP_IN_PASS
        // Frames without clip masks don't attach the mask
        bool hasMasks = false;
        for (size_t k = 0; k < buffer->entries.end && !hasMasks; k++)
            hasMasks = buffer->entries.base[k].type == Ra::Buffer::kClipMask;
        if (hasMasks) {
            drawableDescriptor.colorAttachments[1].texture = clipMaskTexture;
            drawableDescriptor.colorAttachments[1].loadAction = MTLLoadActionDontCare;
            drawableDescriptor.colorAttachments[1].storeAction = MTLStoreActionStore;
        }
        id<MTLRenderPipelineState> opaquesState = hasMasks ? opaquesMasked : opaques, instancesState = hasMasks ? instancesMasked : instances;
        id<MTLRenderPipelineState> instancesClipState = instancesClip;
#if RA_BLEND_BATCHES
        // Batches of draws with blend modes, kBlendInstances, use the pipelines that blend in the shader
        bool canBlend = instancesBlend && instancesClipBlend;
        id<MTLRenderPipelineState> instancesBlendState = hasMasks ? instancesBlendMasked : instancesBlend;
#elif RA_BLEND
        bool hasBlends = buffer->hasBlends && instancesBlend && instancesClipBlend;
        if (hasBlends)
            instancesState = hasMasks ? instancesBlendMasked : instancesBlend, instancesClipState = instancesClipBlend;
#endif
#else
        id<MTLRenderPipelineState> opaquesState = opaques, instancesState = instances;
#endif
#if RA_CLIP_MASK
        // The clip mask holds the current clip's coverage within its bounds, which each clip zeroes first; clipped instances ignore it outside them
        MTLRenderPassDescriptor *maskDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        maskDescriptor.colorAttachments[0].texture = clipMaskTexture;
        maskDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        MTLScissorRect maskBounds = { 0, 0, 0, 0 };
        size_t maskID = 0;    // The identity of the mask the clip mask holds, as contexts beginning with the last one's clip repeat it
        const bool lazyPasses = true;
#else
        drawableDescriptor.stencilAttachment.texture = depthTexture;
        drawableDescriptor.stencilAttachment.loadAction = MTLLoadActionLoad;
        drawableDescriptor.stencilAttachment.storeAction = MTLStoreActionStore;
        MTLRenderPassDescriptor *clipDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        clipDescriptor.stencilAttachment.texture = depthTexture;
        clipDescriptor.stencilAttachment.loadAction = MTLLoadActionClear;
        clipDescriptor.stencilAttachment.storeAction = MTLStoreActionStore;
        clipDescriptor.stencilAttachment.clearStencil = 0;
        MTLRenderPassDescriptor *maskDescriptor = nil;
        const bool lazyPasses = false;
#endif
        id<MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
#if RA_IMAGES
        // The frame's image textures, in an argument buffer bound once per drawable pass
        size_t imageCount = imageEncoder ? buffer->images.end() : 0;
        if (imageCount) {
            textureCache.flush();
            std::vector<size_t> hashes(imageCount);
            for (size_t k = 0; k < imageCount; k++)
                hashes[k] = buffer->images[k].hash();
            if (hashes != imageHashes) {     // Each distinct image is looked up once, & made resident once per pass
                NSUInteger stride = imageEncoder.encodedLength;
                imagesBuffer = [device newBufferWithLength:imageCount * stride options:MTLResourceStorageModeShared];
                NSMutableArray<id<MTLTexture>> *textures = [NSMutableArray array];
                std::map<size_t, id<MTLTexture>> distinct;
                imageFirsts.resize(0);
                for (size_t k = 0; k < imageCount; k++) {
                    auto it = distinct.find(hashes[k]);
                    if (it == distinct.end()) {
                        it = distinct.emplace(hashes[k], textureCache.entryFor(buffer->images[k], device)).first;
                        [textures addObject:it->second], imageFirsts.emplace_back(k);
                    }
                    [imageEncoder setArgumentBuffer:imagesBuffer offset:k * stride];
                    [imageEncoder setTexture:it->second atIndex:0];
                }
                imageHashes.swap(hashes), imageTextures = textures;
            } else
                for (size_t k : imageFirsts)     // So they don't expire
                    textureCache.entryFor(buffer->images[k], device);
        }
        NSArray<id<MTLTexture>> *images = imageCount ? imageTextures : @[];
        id<MTLBuffer> frameImages = imageCount ? imagesBuffer : nil;
        auto bindImages = [&]() {
            if (imageEncoder) {
                for (id<MTLTexture> texture in images)
                    [commandEncoder useResource:texture usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
                [commandEncoder setFragmentBuffer:frameImages ?: noImagesBuffer offset:0 atIndex:2];
            }
        };
        bindImages();
#endif
        drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
        drawableDescriptor.depthAttachment.loadAction = MTLLoadActionLoad;
#if RA_CLIP_IN_PASS
        drawableDescriptor.colorAttachments[1].loadAction = MTLLoadActionLoad;
#endif

        MTLRenderPassDescriptor *edgesDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        edgesDescriptor.colorAttachments[0].texture = accumulationTexture;
        edgesDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        edgesDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        edgesDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
#if RA_EDGES_HEIGHT
        edgesDescriptor.renderTargetWidth = accumulationTexture.width;
        NSUInteger edgesHeight = accumulationTexture.height;
#endif
        float passHeight = accumulationTexture.height;    // The edges pass's viewport height, which its edges map their rows to

        auto drawQuads = [&](NSUInteger count) {
#if RA_QUAD_INDICES
            for (NSUInteger q = 0; q < count; q += kQuadChunk)
                [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:MIN(kQuadChunk, count - q) * 6 indexType:MTLIndexTypeUInt32 indexBuffer:quadIndices indexBufferOffset:0 instanceCount:1 baseVertex:NSInteger(q * 4) baseInstance:0];
#else
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:count baseInstance:0];
#endif
        };
        bool useClip = false, useImage = false, inMask = false, skipMask = false;
        bool edgesRead = true;    // Edges begin a pass only if instances have read the accumulation texture since the last one
        enum PassKind { kDrawablePass, kEdgesPass, kMaskPass } passKind = kDrawablePass;
        auto beginPass = [&](PassKind kind) {
            if (lazyPasses && passKind == kind && kind != kEdgesPass)
                return;
            [commandEncoder endEncoding];
#if RA_EDGES_HEIGHT
            if (kind == kEdgesPass)
                edgesDescriptor.renderTargetHeight = MAX(NSUInteger(1), MIN(edgesHeight, accumulationTexture.height)), passHeight = edgesDescriptor.renderTargetHeight;
#endif
            commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:kind == kMaskPass ? maskDescriptor : kind == kEdgesPass ? edgesDescriptor : drawableDescriptor];
            passKind = kind;
#if RA_IMAGES
            if (kind == kDrawablePass)
                bindImages();
#endif
        };
        uint32_t reverse, pathsCount = uint32_t(buffer->pathsCount), texCount = uint32_t(th);
        float width = target.width, height = target.height;
        NSUInteger imgIndex = 0;
        id<MTLTexture> imageTexture = nil;

        for (size_t segbase = 0, instbase = 0, i = 0; i < buffer->entries.end; i++) {
            Ra::Buffer::Entry& entry = buffer->entries.base[i];
            switch (entry.type) {
                case Ra::Buffer::kSegmentsBase:  segbase = entry.begin;  break;
#if RA_EDGES_HEIGHT
                case Ra::Buffer::kInstancesBase:  instbase = entry.begin, edgesHeight = entry.end;  break;
#else
                case Ra::Buffer::kInstancesBase:  instbase = entry.begin;  break;
#endif
                case Ra::Buffer::kDisableClip:
                case Ra::Buffer::kEnableClip:
                    inMask = skipMask = false, useClip = entry.type == Ra::Buffer::kEnableClip;
                    break;
#if !RA_IMAGES
                case Ra::Buffer::kDisableImage:  useImage = false;  break;
                case Ra::Buffer::kNextImage:
                    imageTexture = textureCache.entryFor(buffer->images[imgIndex++], device);
                    useImage = true;
                    break;
#endif
#if RA_CLIP_MASK
                case Ra::Buffer::kClipMask: {
                    inMask = true;
                    if ((skipMask = entry.end == maskID))
                        break;
                    maskID = entry.end;
                    NSUInteger lx = entry.begin & 0xFFFF, ly = (entry.begin >> 16) & 0xFFFF, ux = (entry.begin >> 32) & 0xFFFF, uy = (entry.begin >> 48) & 0xFFFF;
                    ux = MIN(ux, NSUInteger(width)), uy = MIN(uy, NSUInteger(height)), lx = MIN(lx, ux), ly = MIN(ly, uy);
                    maskBounds = { lx, NSUInteger(height) - uy, ux - lx, uy - ly };
#if !RA_CLIP_IN_PASS
                    maskDescriptor.colorAttachments[0].loadAction = MTLLoadActionDontCare;
                    beginPass(kMaskPass);
                    maskDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
#endif
#if !RA_CLIP_CLEAR_CELLS
                    if (maskBounds.width && maskBounds.height) {
                        [commandEncoder setScissorRect:maskBounds];
                        [commandEncoder setRenderPipelineState:clipClear];
                        [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                        [commandEncoder setScissorRect:(MTLScissorRect){ 0, 0, NSUInteger(width), NSUInteger(height) }];
                    }
#endif
                    break;
                }
#else
                case Ra::Buffer::kStencils:
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:clipDescriptor];
                    [commandEncoder setDepthStencilState:stencilDepthState];
                    [commandEncoder setRenderPipelineState:stencil];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3 instanceCount:(entry.end - entry.begin) / sizeof(Ra::Opaque) baseInstance:0];
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                    break;
#endif
                case Ra::Buffer::kOpaques:
                    if (lazyPasses)
                        beginPass(kDrawablePass);
#if RA_CLIP_MASK
                    [commandEncoder setDepthStencilState:opaquesDepthState];
#else
                    [commandEncoder setDepthStencilState:useClip ? opaquesClipDepthState : opaquesDepthState];
                    [commandEncoder setStencilReferenceValue:0];
#endif
                    [commandEncoder setRenderPipelineState:opaquesState];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
#if RA_PATH_DATA
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->paths atIndex:4];
#else
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texCtms atIndex:8];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texIdxs atIndex:9];
#endif
                    reverse = uint32_t((entry.end - entry.begin) / sizeof(Ra::Opaque));
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder setVertexBytes:& reverse length:sizeof(reverse) atIndex:12];
                    [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                    [commandEncoder setVertexBytes:& texCount length:sizeof(texCount) atIndex:14];
                    [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:15];
                    [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
                    drawQuads(reverse);
                    break;
                case Ra::Buffer::kQuadEdges:
                case Ra::Buffer::kFastEdges:
                case Ra::Buffer::kFastMolecules:
                case Ra::Buffer::kQuadMolecules:
                    if (entry.type == Ra::Buffer::kQuadEdges) {
                        if (passKind != kEdgesPass || edgesRead)
                            beginPass(kEdgesPass), edgesRead = false;
                        [commandEncoder setRenderPipelineState:quadEdges];
                    } else if (entry.type == Ra::Buffer::kFastEdges)
                        [commandEncoder setRenderPipelineState:fastEdges];
                    else if (entry.type == Ra::Buffer::kFastMolecules)
                        [commandEncoder setRenderPipelineState:fastMolecules];
                    else
                        [commandEncoder setRenderPipelineState:quadMolecules];
                    if (entry.end - entry.begin) {
                        [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:segbase atIndex:2];
#if RA_PATH_DATA
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->paths atIndex:4];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:instbase atIndex:5];
#else
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:instbase atIndex:5];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
#endif
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->p16s atIndex:8];
                        [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                        [commandEncoder setVertexBytes:& passHeight length:sizeof(passHeight) atIndex:11];
                        [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:14];
                        [commandEncoder setFragmentBuffer:mtlBuffer offset:segbase atIndex:2];
                        [commandEncoder setFragmentBytes:& buffer->params length:sizeof(Ra::Params) atIndex:14];
                        drawQuads((entry.end - entry.begin) / sizeof(Ra::Edge));
                    }
                    break;
                case Ra::Buffer::kInstances:
#if RA_BLEND_BATCHES
                case Ra::Buffer::kBlendInstances:
#endif
                    edgesRead = true;
                    if (skipMask)
                        break;
#if RA_CLIP_IN_PASS
                    beginPass(kDrawablePass);
                    if (inMask) {
                        [commandEncoder setDepthStencilState:clipMaskDepthState];
                        [commandEncoder setRenderPipelineState:clipMask];
                    } else {
                        [commandEncoder setDepthStencilState:instancesDepthState];
#if RA_BLEND_BATCHES
                        bool hasBlends = canBlend && entry.type == Ra::Buffer::kBlendInstances;
                        [commandEncoder setRenderPipelineState:useClip ? (hasBlends ? instancesClipBlend : instancesClipState) : (hasBlends ? instancesBlendState : instancesState)];
#else
                        [commandEncoder setRenderPipelineState:useClip ? instancesClipState : instancesState];
#endif
                        uint32_t bounds[4] = { uint32_t(maskBounds.x), uint32_t(maskBounds.y), uint32_t(maskBounds.width), uint32_t(maskBounds.height) };
                        [commandEncoder setFragmentBytes:bounds length:sizeof(bounds) atIndex:0];
#if RA_BLEND
                        if (hasBlends)
#if RA_PATH_DATA
                            [commandEncoder setFragmentBuffer:mtlBuffer offset:buffer->paths atIndex:1];
#else
                            [commandEncoder setFragmentBuffer:mtlBuffer offset:buffer->modes atIndex:1];
#endif
#endif
                    }
#elif RA_CLIP_MASK
                    beginPass(inMask ? kMaskPass : kDrawablePass);
                    if (inMask)
                        [commandEncoder setRenderPipelineState:clipMask];
                    else {
                        [commandEncoder setDepthStencilState:instancesDepthState];
                        [commandEncoder setRenderPipelineState:useClip ? instancesClip : instancesState];
                        [commandEncoder setFragmentTexture:clipMaskTexture atIndex:2];
                        uint32_t bounds[4] = { uint32_t(maskBounds.x), uint32_t(maskBounds.y), uint32_t(maskBounds.width), uint32_t(maskBounds.height) };
                        [commandEncoder setFragmentBytes:bounds length:sizeof(bounds) atIndex:0];
                    }
#else
                    beginPass(kDrawablePass);
                    [commandEncoder setDepthStencilState:useClip ? instancesClipDepthState : instancesDepthState];
                    [commandEncoder setStencilReferenceValue:0];
                    [commandEncoder setRenderPipelineState:instancesState];
#endif
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
#if RA_PATH_DATA
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->paths atIndex:4];
#else
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->clips atIndex:5];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texCtms atIndex:8];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texIdxs atIndex:9];
#endif
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                    [commandEncoder setVertexBytes:& texCount length:sizeof(texCount) atIndex:14];
                    [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:15];
                    [commandEncoder setFragmentTexture:accumulationTexture atIndex:0];
                    [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
                    drawQuads((entry.end - entry.begin) / sizeof(Ra::Instance));
                    break;
                default:
                    break;
            }
        }
        [commandEncoder endEncoding];
        return commandBuffer;
    }

    bool writePNG(const char *path) {
        size_t w = target.width, h = target.height, bpr = w * 4;
        id<MTLBuffer> readback = [device newBufferWithLength:bpr * h options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                     toBuffer:readback destinationOffset:0 destinationBytesPerRow:bpr destinationBytesPerImage:bpr * h];
        [blit endEncoding], [cb commit], [cb waitUntilCompleted];
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(readback.contents, w, h, 8, bpr, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGImageRef image = CGBitmapContextCreateImage(ctx);
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:@(path)], kUTTypePNG, 1, nullptr);
        CGImageDestinationAddImage(dst, image, nullptr);
        bool ok = CGImageDestinationFinalize(dst);
        CFRelease(dst), CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(cs);
        return ok;
    }
};

#pragma mark - Bench

// The view for frame k of n: fit the list to WxH, then zoom by zmax^(k/(n-1)) around the centre. vello_bench uses the same formula
static Ra::Transform frameView(Ra::Bounds bounds, float W, float H, size_t k, size_t n, float zmax) {
    Ra::Transform fit = Ra::Bounds(0.f, 0.f, W, H).fitTransform(bounds);
    double z = n > 1 ? pow(double(zmax), double(k) / double(n - 1)) : 1.0;
    return fit.concatAroundCenter(Ra::Transform(float(z), 0.f, 0.f, float(z), 0.f, 0.f), 0.5f * W, 0.5f * H);
}

static double median(std::vector<double> v) {
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    return v.size() & 1 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}

static int bench(Ra::SceneList& list, const char *name, const char *metallib, size_t W, size_t H, size_t frames, float zmax, const char *png, long pngFrame, size_t contexts) {
    Offscreen gpu;
    if (!gpu.init(metallib, W, H))
        return 1;
    RenderBuffer buffers[2];
    buffers[0].device = buffers[1].device = gpu.device;
    RasterizerRenderer renderer;
    if (contexts) {     // A frame uses at most contexts.size()
#if RA_CONTEXT_LIMIT
        renderer.contexts.resize(std::min(contexts, renderer.contexts.size()));
#else
        return fprintf(stderr, "--contexts needs a RasterizerRenderer with a contexts vector\n"), 2;
#endif
    }
    Ra::Bounds bounds = list.bounds();

    double t0 = now();
    list.prepare();
    double prepareTime = now() - t0;

    std::vector<double> cpu, gpuTime, wall, submitToDone;
    double firstCpu = 0, firstWall = 0;
    const size_t warmup = 3;
    for (size_t f = 0; f < warmup + frames; f++) {
        @autoreleasepool {
            size_t k = f < warmup ? 0 : f - warmup;
            list.ctm = frameView(bounds, W, H, k, frames, zmax);
            RenderBuffer *rb = & buffers[f & 1];
            double a = now();
            renderer.renderList(list, 1.f, W, H, rb);
            id<MTLCommandBuffer> cb = gpu.encode(rb);
            [cb commit];
            double b = now();
            [cb waitUntilCompleted];
            double c = now();
            if (cb.status == MTLCommandBufferStatusError)
                fprintf(stderr, "frame %zu: %s\n", f, cb.error.localizedDescription.UTF8String);
            if (f == 0)
                firstCpu = b - a, firstWall = c - a;
            if (f >= warmup)
                cpu.push_back(b - a), wall.push_back(c - a), submitToDone.push_back(c - b), gpuTime.push_back(cb.GPUEndTime - cb.GPUStartTime);
            if (png && f >= warmup && long(k) == pngFrame)
                gpu.writePNG(png);
        }
    }
    auto ms = [](double s) { return s * 1e3; };
    printf("{\"lib\":\"rasterizer\",\"file\":\"%s\",\"w\":%zu,\"h\":%zu,\"frames\":%zu,\"zoom\":%g,\"draws\":%zu,"
           "\"setup_ms\":%.3f,\"first_cpu_ms\":%.3f,\"first_wall_ms\":%.3f,"
           "\"cpu_min\":%.3f,\"cpu_med\":%.3f,\"gpu_min\":%.3f,\"gpu_med\":%.3f,\"submit_to_done_min\":%.3f,\"submit_to_done_med\":%.3f,\"wall_min\":%.3f,\"wall_med\":%.3f}\n",
           name, W, H, frames, zmax, list.pathsCount(), ms(prepareTime), ms(firstCpu), ms(firstWall),
           ms(*std::min_element(cpu.begin(), cpu.end())), ms(median(cpu)),
           ms(*std::min_element(gpuTime.begin(), gpuTime.end())), ms(median(gpuTime)),
           ms(*std::min_element(submitToDone.begin(), submitToDone.end())), ms(median(submitToDone)),
           ms(*std::min_element(wall.begin(), wall.end())), ms(median(wall)));
    return 0;
}

#if !RABENCH_NO_MAIN     // raviewer includes this file for its loaders & Offscreen renderer
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 4)
            return fprintf(stderr, "usage: rabench export <in> <out.rsd> | bench <in> <metallib> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--contexts N]\n"), 2;
        std::string cmd = argv[1];
        Ra::SceneList list;
        double t0 = now();
        if (!loadList(argv[2], list))
            return fprintf(stderr, "failed to load %s\n", argv[2]), 1;
        double loadTime = now() - t0;
        const char *name = strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 : argv[2];
        if (cmd == "export") {
            fprintf(stderr, "%s: loaded in %.1f ms\n", name, loadTime * 1e3);
            return exportList(list, argv[3]);
        }
        size_t W = 2048, H = 2048, frames = 60, contexts = 0;  float zoom = 8.f;  const char *png = nullptr;  long pngFrame = 0;
        for (int i = 4; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--size" && i + 1 < argc)
                sscanf(argv[++i], "%zux%zu", & W, & H);
            else if (a == "--frames" && i + 1 < argc)
                frames = strtoul(argv[++i], nullptr, 10);
            else if (a == "--zoom" && i + 1 < argc)
                zoom = strtof(argv[++i], nullptr);
            else if (a == "--png" && i + 1 < argc)
                png = argv[++i];
            else if (a == "--pngframe" && i + 1 < argc)
                pngFrame = strtol(argv[++i], nullptr, 10);
            else if (a == "--contexts" && i + 1 < argc)
                contexts = strtoul(argv[++i], nullptr, 10);
        }
        if (frames == 0)
            frames = 1;
        return bench(list, name, argv[3], W, H, frames, zoom, png, pngFrame, contexts);
    }
}
#endif
__RA_RABENCH_MM__

w $S/rsd/Cargo.toml <<'__RSD_CARGO_TOML__'
[package]
name = "rsd"
version = "0.1.0"
edition = "2024"

[dependencies]
png = "0.18"

[[bin]]
name = "rsdcompare"
path = "src/bin/compare.rs"
__RSD_CARGO_TOML__

w $S/rsd/src/lib.rs <<'__RSD_SRC_LIB_RS__'
//! Shared by the replay benches: the RSD dump written by `rabench export`, the frame views, culling, clip runs,
//! per-frame stats & PNG I/O. Coordinates are Rasterizer's, y up: device = view * scene ctm * draw ctm.

use std::{fs, time::Instant};

pub const K_MOVE: u8 = 0;
pub const K_LINE: u8 = 1;
pub const K_QUAD: u8 = 2;
pub const K_CUBIC: u8 = 3;
pub const K_CLOSE: u8 = 4;
pub const F_EVEN_ODD: u8 = 1 << 1;
pub const F_ROUND_CAP: u8 = 1 << 2;
pub const F_SQUARE_CAP: u8 = 1 << 3;
pub const F_ROUND_JOIN: u8 = 1 << 4;
pub const F_CLIP_EVEN_ODD: u8 = 1 << 5;
pub const PAINT_COLOR: u8 = 0;
pub const PAINT_LINEAR: u8 = 1;
pub const PAINT_RADIAL: u8 = 2;
pub const PAINT_IMAGE: u8 = 3;
pub const NONE: u32 = u32::MAX;
/// Rasterizer caps both segments at a turn of more than 150 degrees, i.e. a miter ratio of 1 / sin(15 degrees)
pub const MITER_LIMIT: f64 = 3.864;
const HUGE: f32 = 5e11;

/// x' = a x + c y + e, y' = b x + d y + f, as Ra::Transform, kurbo::Affine & skia's affine
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Affine(pub [f64; 6]);

impl Affine {
    pub const IDENTITY: Affine = Affine([1.0, 0.0, 0.0, 1.0, 0.0, 0.0]);
    pub fn translate(x: f64, y: f64) -> Self {
        Affine([1.0, 0.0, 0.0, 1.0, x, y])
    }
    pub fn scale(s: f64) -> Self {
        Affine([s, 0.0, 0.0, s, 0.0, 0.0])
    }
    /// Rasterizer's y-up device space to a y-down one of height h
    pub fn flip(h: f64) -> Self {
        Affine([1.0, 0.0, 0.0, -1.0, 0.0, h])
    }
    pub fn det(&self) -> f64 {
        self.0[0] * self.0[3] - self.0[1] * self.0[2]
    }
    pub fn apply(&self, x: f64, y: f64) -> (f64, f64) {
        let m = &self.0;
        (m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5])
    }
    fn from_f32(m: [f32; 6]) -> Self {
        Affine(m.map(f64::from))
    }
}

/// self * rhs applies rhs first
impl std::ops::Mul for Affine {
    type Output = Affine;
    fn mul(self, r: Affine) -> Affine {
        let (a, b) = (self.0, r.0);
        Affine([
            a[0] * b[0] + a[2] * b[1],
            a[1] * b[0] + a[3] * b[1],
            a[0] * b[2] + a[2] * b[3],
            a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4],
            a[1] * b[4] + a[3] * b[5] + a[5],
        ])
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Bounds {
    pub lx: f64,
    pub ly: f64,
    pub ux: f64,
    pub uy: f64,
}

impl Bounds {
    pub fn width(&self) -> f64 {
        self.ux - self.lx
    }
    pub fn height(&self) -> f64 {
        self.uy - self.ly
    }
    /// The device bounds of these local bounds under m
    pub fn transformed(&self, m: Affine) -> Bounds {
        let mut b = Bounds { lx: f64::MAX, ly: f64::MAX, ux: f64::MIN, uy: f64::MIN };
        for (x, y) in [(self.lx, self.ly), (self.ux, self.ly), (self.lx, self.uy), (self.ux, self.uy)] {
            let (px, py) = m.apply(x, y);
            b.lx = b.lx.min(px);
            b.ly = b.ly.min(py);
            b.ux = b.ux.max(px);
            b.uy = b.uy.max(py);
        }
        b
    }
}

pub enum Seg {
    Move(f32, f32),
    Line(f32, f32),
    Quad(f32, f32, f32, f32),
    Cubic(f32, f32, f32, f32, f32, f32),
    Close,
}

/// A Ra::Geometry: one type per point (quads take 2, cubics 3), & its bounds, which include control points as Rasterizer's do
pub struct Path {
    pub types: Vec<u8>,
    pub pts: Vec<[f32; 2]>,
    pub bounds: Bounds,
}

impl Path {
    pub fn for_each(&self, mut f: impl FnMut(Seg)) {
        let (t, p) = (&self.types, &self.pts);
        let mut i = 0;
        while i < t.len() {
            match t[i] {
                K_MOVE => { f(Seg::Move(p[i][0], p[i][1])); i += 1; }
                K_LINE => { f(Seg::Line(p[i][0], p[i][1])); i += 1; }
                K_QUAD => { f(Seg::Quad(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1])); i += 2; }
                K_CUBIC => { f(Seg::Cubic(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1], p[i + 2][0], p[i + 2][1])); i += 3; }
                K_CLOSE => { f(Seg::Close); i += 1; }
                t => panic!("bad path type {t}"),
            }
        }
    }
}

pub struct Draw {
    pub scene: u32,
    pub path: u32,
    pub ctm: Affine,
    pub paint_type: u8,
    pub flags: u8,
    pub rgba: [u8; 4],
    pub paint_index: u32,
    pub width: f32,
    pub clip: [f32; 4],
    pub clip_path: u32,
}

/// A gradient in brush space (linear t = y from 0 to 1, radial t = |p|, pad extend), mapped to the path's local space by ctm
pub struct Gradient {
    pub radial: bool,
    pub ctm: Affine,
    pub locs: Vec<f32>,
    pub rgba: Vec<[u8; 4]>,
}

/// Straight from Ra::Bitmap: BGRA rows, row 0 at the top of the path bounds, which the Metal renderer treats as premultiplied
pub struct Image {
    pub w: u32,
    pub h: u32,
    pub bgra: Vec<u8>,
}

impl Image {
    /// Image space (pixels, y down from row 0) to the path's local space: the image covers the path bounds, row 0 at uy
    pub fn to_local(&self, b: &Bounds) -> Affine {
        Affine([b.width() / self.w as f64, 0.0, 0.0, -b.height() / self.h as f64, b.lx, b.uy])
    }
}

pub struct Dump {
    pub bounds: [f32; 4],
    pub scene_ctms: Vec<Affine>,
    pub scene_clips: Vec<[f32; 4]>,
    pub paths: Vec<Path>,
    pub grads: Vec<Gradient>,
    pub images: Vec<Image>,
    pub draws: Vec<Draw>,
}

struct Reader<'a> {
    b: &'a [u8],
    i: usize,
}
impl<'a> Reader<'a> {
    fn bytes(&mut self, n: usize) -> &'a [u8] {
        let s = &self.b[self.i..self.i + n];
        self.i += n;
        s
    }
    fn u32(&mut self) -> u32 {
        u32::from_le_bytes(self.bytes(4).try_into().unwrap())
    }
    fn f32(&mut self) -> f32 {
        f32::from_le_bytes(self.bytes(4).try_into().unwrap())
    }
    fn f32s<const N: usize>(&mut self) -> [f32; N] {
        std::array::from_fn(|_| self.f32())
    }
}

pub fn load(file: &str) -> Dump {
    let data = fs::read(file).unwrap_or_else(|e| panic!("{file}: {e}"));
    let mut r = Reader { b: &data, i: 0 };
    assert_eq!(r.bytes(4), b"RSD1", "not an RSD dump");
    assert_eq!(r.u32(), 1, "unknown RSD version");
    let bounds = r.f32s::<4>();
    let (mut scene_ctms, mut scene_clips) = (vec![], vec![]);
    for _ in 0..r.u32() {
        scene_ctms.push(Affine::from_f32(r.f32s::<6>()));
        scene_clips.push(r.f32s::<4>());
    }
    let npaths = r.u32() as usize;
    let mut paths = Vec::with_capacity(npaths);
    for _ in 0..npaths {
        let n = r.u32() as usize;
        let types = r.bytes(n).to_vec();
        r.i += (4 - (n & 3)) & 3;
        let pts: Vec<[f32; 2]> = (0..n).map(|_| [r.f32(), r.f32()]).collect();
        let mut b = Bounds { lx: f64::MAX, ly: f64::MAX, ux: f64::MIN, uy: f64::MIN };
        for p in &pts {
            b.lx = b.lx.min(p[0] as f64);
            b.ly = b.ly.min(p[1] as f64);
            b.ux = b.ux.max(p[0] as f64);
            b.uy = b.uy.max(p[1] as f64);
        }
        paths.push(Path { types, pts, bounds: b });
    }
    let ngrads = r.u32() as usize;
    let mut grads = Vec::with_capacity(ngrads);
    for _ in 0..ngrads {
        let radial = r.u32() == 2;
        let ctm = Affine::from_f32(r.f32s::<6>());
        let n = r.u32() as usize;
        let locs = (0..n).map(|_| r.f32()).collect();
        let rgba = r.bytes(4 * n).chunks(4).map(|c| [c[2], c[1], c[0], c[3]]).collect();
        grads.push(Gradient { radial, ctm, locs, rgba });
    }
    let nimages = r.u32() as usize;
    let mut images = Vec::with_capacity(nimages);
    for _ in 0..nimages {
        let (w, h) = (r.u32(), r.u32());
        images.push(Image { w, h, bgra: r.bytes(4 * (w * h) as usize).to_vec() });
    }
    let ndraws = r.u32() as usize;
    let mut draws = Vec::with_capacity(ndraws);
    for _ in 0..ndraws {
        let (scene, path) = (r.u32(), r.u32());
        let ctm = Affine::from_f32(r.f32s::<6>());
        let pt = r.bytes(4);
        let bgra = r.bytes(4);
        let paint_index = r.u32();
        let width = r.f32();
        let clip = r.f32s::<4>();
        let clip_path = r.u32();
        draws.push(Draw {
            scene, path, ctm, paint_type: pt[0], flags: pt[1],
            rgba: [bgra[2], bgra[1], bgra[0], bgra[3]], paint_index, width, clip, clip_path,
        });
    }
    Dump { bounds, scene_ctms, scene_clips, paths, grads, images, draws }
}

/// Bounds::fitTransform(bounds) into WxH, then a zoom by zmax^(k/(n-1)) around the centre, as rabench's frameView
pub fn frame_view(b: [f32; 4], w: f64, h: f64, k: usize, n: usize, zmax: f64) -> Affine {
    let (bw, bh) = ((b[2] - b[0]) as f64, (b[3] - b[1]) as f64);
    let s = (w / bw).min(h / bh);
    let fit = Affine([s, 0.0, 0.0, s, 0.5 * (w - s * bw) - s * b[0] as f64, 0.5 * (h - s * bh) - s * b[1] as f64]);
    let z = if n > 1 { zmax.powf(k as f64 / (n - 1) as f64) } else { 1.0 };
    let (cx, cy) = (0.5 * w, 0.5 * h);
    Affine::translate(cx, cy) * Affine::scale(z) * Affine::translate(-cx, -cy) * fit
}

/// The clip a run of draws shares: the scene clip & draw clip rect (scene space), or None for a huge rect, & clip path & its rule
#[derive(Clone, Copy, PartialEq)]
pub struct ClipKey {
    pub scene: u32,
    pub rect: Option<[u32; 4]>,
    pub path: u32,
    pub even_odd: bool,
}

impl ClipKey {
    pub fn new(d: &Dump, draw: &Draw) -> Self {
        let s = d.scene_clips[draw.scene as usize];
        let c = draw.clip;
        let r = [s[0].max(c[0]), s[1].max(c[1]), s[2].min(c[2]), s[3].min(c[3])];
        let huge = r[0] <= -HUGE && r[1] <= -HUGE && r[2] >= HUGE && r[3] >= HUGE;
        let even_odd = draw.clip_path != NONE && draw.flags & F_CLIP_EVEN_ODD != 0;
        ClipKey { scene: draw.scene, rect: if huge { None } else { Some(r.map(f32::to_bits)) }, path: draw.clip_path, even_odd }
    }
    pub fn rect(&self) -> Option<Bounds> {
        self.rect.map(|r| {
            let r = r.map(f32::from_bits);
            Bounds { lx: r[0] as f64, ly: r[1] as f64, ux: r[2] as f64, uy: r[3] as f64 }
        })
    }
}

/// A draw's device transform & stroke width for a frame. device is the y-down device transform the draw is encoded with;
/// hair is the one hairline widths are computed for (the same, except for a retained scene)
pub struct Placed {
    pub scene_m: Affine,
    pub m: Affine,
    /// The stroke width in local space, 0 for fills
    pub local_width: f64,
    /// True for Rasterizer's -1 width, a 1 device pixel hairline
    pub is_unit_hairline: bool,
}

/// Places a draw, or returns None if it's culled (offscreen, as drawList does, when cull) or degenerate
pub fn place(d: &Dump, draw: &Draw, device: Affine, hair: Affine, w: f64, h: f64, cull: bool) -> Option<Placed> {
    let scene_m = device * d.scene_ctms[draw.scene as usize];
    let m = scene_m * draw.ctm;
    let det = m.det().abs();
    let hair_det = (hair * d.scene_ctms[draw.scene as usize] * draw.ctm).det().abs();
    if hair_det == 0.0 && draw.width >= 0.0 {
        return None;
    }
    let dev_width = if draw.width > 0.0 { draw.width as f64 * det.sqrt() } else { -draw.width as f64 };
    if cull {
        let b = d.paths[draw.path as usize].bounds.transformed(m);
        if b.ux + dev_width < 0.0 || b.uy + dev_width < 0.0 || b.lx - dev_width > w || b.ly - dev_width > h {
            return None;
        }
    }
    let local_width = if draw.width >= 0.0 { draw.width as f64 } else { -draw.width as f64 / hair_det.sqrt() };
    Some(Placed { scene_m, m, local_width, is_unit_hairline: draw.width == -1.0 })
}

#[derive(Default)]
pub struct Opts {
    pub file: String,
    pub w: u32,
    pub h: u32,
    pub frames: usize,
    pub zoom: f64,
    pub png: Option<String>,
    pub png_frame: usize,
    pub flags: Vec<String>,
    pub values: Vec<(String, String)>,
}

impl Opts {
    /// <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k], plus the bench's own --flags & --key value
    pub fn parse(valued: &[&str]) -> Opts {
        let args: Vec<String> = std::env::args().collect();
        let mut o = Opts { file: args.get(1).cloned().unwrap_or_default(), w: 2048, h: 2048, frames: 60, zoom: 8.0, ..Default::default() };
        let mut i = 2;
        while i < args.len() {
            let a = args[i].as_str();
            let next = || args.get(i + 1).cloned().unwrap_or_else(|| panic!("{a} needs a value"));
            match a {
                "--size" => {
                    let s: Vec<u32> = next().split('x').map(|v| v.parse().unwrap()).collect();
                    (o.w, o.h) = (s[0], s[1]);
                    i += 1;
                }
                "--frames" => { o.frames = next().parse().unwrap(); i += 1; }
                "--zoom" => { o.zoom = next().parse().unwrap(); i += 1; }
                "--png" => { o.png = Some(next()); i += 1; }
                "--pngframe" => { o.png_frame = next().parse().unwrap(); i += 1; }
                _ if valued.contains(&a) => { o.values.push((a.to_string(), next())); i += 1; }
                _ => o.flags.push(a.to_string()),
            }
            i += 1;
        }
        o.frames = o.frames.max(1);
        o
    }
    pub fn flag(&self, f: &str) -> bool {
        self.flags.iter().any(|x| x == f)
    }
    pub fn value(&self, k: &str) -> Option<&str> {
        self.values.iter().find(|(key, _)| key == k).map(|(_, v)| v.as_str())
    }
    pub fn stem(&self) -> String {
        std::path::Path::new(&self.file).file_stem().unwrap().to_string_lossy().to_string()
    }
}

/// Per-frame timings: start, CPU done (submitted), GPU done
#[derive(Default)]
pub struct Stats {
    pub encode: Vec<f64>,
    pub cpu: Vec<f64>,
    pub submit_to_done: Vec<f64>,
    pub wall: Vec<f64>,
    pub first_cpu: f64,
    pub first_wall: f64,
}

impl Stats {
    pub fn record(&mut self, frame: usize, warmup: usize, a: Instant, encoded: Instant, submitted: Instant, done: Instant) {
        let ms = |x: Instant, y: Instant| (y - x).as_secs_f64() * 1e3;
        if frame == 0 {
            self.first_cpu = ms(a, submitted);
            self.first_wall = ms(a, done);
        }
        if frame >= warmup {
            self.encode.push(ms(a, encoded));
            self.cpu.push(ms(a, submitted));
            self.submit_to_done.push(ms(submitted, done));
            self.wall.push(ms(a, done));
        }
    }
    /// One JSON line, with the bench's own fields first
    pub fn print(&self, lib: &str, o: &Opts, draws: usize, extra: &str) {
        let f = |v: &Vec<f64>| format!("{:.3},{:.3}", min(v), median(v));
        let (e, c, s, w) = (f(&self.encode), f(&self.cpu), f(&self.submit_to_done), f(&self.wall));
        let pair = |name: &str, v: &str| {
            let (lo, med) = v.split_once(',').unwrap();
            format!("\"{name}_min\":{lo},\"{name}_med\":{med}")
        };
        println!(
            "{{\"lib\":\"{lib}\",\"file\":\"{}\",\"w\":{},\"h\":{},\"frames\":{},\"zoom\":{},\"draws\":{draws},{extra}\"first_cpu_ms\":{:.3},\"first_wall_ms\":{:.3},{},{},{},{}}}",
            o.stem(), o.w, o.h, o.frames, o.zoom, self.first_cpu, self.first_wall,
            pair("encode", &e), pair("cpu", &c), pair("submit_to_done", &s), pair("wall", &w)
        );
    }
}

pub fn median(v: &[f64]) -> f64 {
    if v.is_empty() {
        return 0.0;
    }
    let mut v = v.to_vec();
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let n = v.len();
    if n & 1 == 1 { v[n / 2] } else { 0.5 * (v[n / 2 - 1] + v[n / 2]) }
}

pub fn min(v: &[f64]) -> f64 {
    if v.is_empty() { 0.0 } else { v.iter().cloned().fold(f64::MAX, f64::min) }
}

pub fn read_png(path: &str) -> (u32, u32, Vec<u8>) {
    let dec = png::Decoder::new(std::io::BufReader::new(fs::File::open(path).unwrap_or_else(|e| panic!("{path}: {e}"))));
    let mut rd = dec.read_info().unwrap();
    let mut buf = vec![0; rd.output_buffer_size().unwrap()];
    let info = rd.next_frame(&mut buf).unwrap();
    let ch = info.color_type.samples();
    let mut rgba = Vec::with_capacity((info.width * info.height * 4) as usize);
    for px in buf[..info.buffer_size()].chunks(ch) {
        rgba.extend_from_slice(&[px[0], px[1 % ch], px[2 % ch], if ch == 4 { px[3] } else { 255 }]);
    }
    (info.width, info.height, rgba)
}

pub fn write_png(path: &str, w: u32, h: u32, rgba: &[u8]) {
    let mut enc = png::Encoder::new(std::io::BufWriter::new(fs::File::create(path).unwrap()), w, h);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    enc.write_header().unwrap().write_image_data(rgba).unwrap();
}

/// Prints the mean abs diff, PSNR & % of pixels off by >8 / >32 as JSON, & optionally writes a diff image
pub fn compare(a: &str, b: &str, diff: Option<&str>) {
    let (w, h, pa) = read_png(a);
    let (w2, h2, pb) = read_png(b);
    assert!(w == w2 && h == h2, "size mismatch");
    let (mut sum, mut sq, mut over8, mut over32) = (0f64, 0f64, 0usize, 0usize);
    let mut out = vec![0u8; pa.len()];
    for i in 0..(w * h) as usize {
        let mut m = 0i32;
        for c in 0..3 {
            let dd = pa[4 * i + c] as i32 - pb[4 * i + c] as i32;
            sum += dd.abs() as f64;
            sq += (dd * dd) as f64;
            m = m.max(dd.abs());
        }
        over8 += (m > 8) as usize;
        over32 += (m > 32) as usize;
        let v = (255 - (m * 4).min(255)) as u8;
        out[4 * i..4 * i + 4].copy_from_slice(&[255, v, v, 255]);
    }
    let (n, px) = ((w * h * 3) as f64, (w * h) as f64);
    let mse = sq / n;
    let psnr = if mse == 0.0 { 99.0 } else { 10.0 * (255.0f64 * 255.0 / mse).log10() };
    println!(
        "{{\"mean_abs\":{:.3},\"psnr_db\":{:.2},\"pct_px_gt8\":{:.3},\"pct_px_gt32\":{:.3}}}",
        sum / n, psnr, 100.0 * over8 as f64 / px, 100.0 * over32 as f64 / px
    );
    if let Some(d) = diff {
        write_png(d, w, h, &out);
    }
}
__RSD_SRC_LIB_RS__

w $S/rsd/src/bin/compare.rs <<'__RSD_SRC_BIN_COMPARE_RS__'
//! rsdcompare <a.png> <b.png> [diff.png]: mean abs diff, PSNR & % of pixels off by >8 / >32, as JSON
fn main() {
    let a: Vec<String> = std::env::args().collect();
    if a.len() < 3 {
        eprintln!("usage: rsdcompare <a.png> <b.png> [diff.png]");
        std::process::exit(2);
    }
    rsd::compare(&a[1], &a[2], a.get(3).map(String::as_str));
}
__RSD_SRC_BIN_COMPARE_RS__

w $S/vello/Cargo.toml <<'__VELLO_CARGO_TOML__'
[package]
name = "vello_bench"
version = "0.1.0"
edition = "2024"

[dependencies]
vello = "=0.11.0"
pollster = "0.4"
rsd = { path = "../rsd" }

[profile.release]
debug = 1

# Vello's dynamic buffers are fixed sizes picked for Paris-30K: bench.sh patches this copy to scale them (VELLO_BUFFER_SHIFT)
[patch.crates-io]
vello_encoding = { path = "../../vendor/vello_encoding" }
__VELLO_CARGO_TOML__

w $S/vello/src/lib.rs <<'__VELLO_SRC_LIB_RS__'
//! The replay of an RSD dump into a Vello scene, shared by vello_bench & raviewer.
//!
//! Mapping from Rasterizer semantics (see Rasterizer.hpp Context::drawList & rsd):
//! - device = flip(H) * view * scene ctm * draw ctm, flip because Rasterizer is y-up & Vello y-down
//! - width > 0: local stroke width (Rasterizer scales by sqrt|det|, Vello by the full affine: same for uniform scales)
//! - width < 0: |width| device pixels, so local width = |width| / sqrt|det|, recomputed every frame
//! - joins: round, else miter limited at Rasterizer's 150 degree turn. Caps: butt, round, square
//! - clip rect (scene & draw clip, scene space) & clip path (scene space, even-odd like the stencil) become clip layers,
//!   pushed once per run of draws with the same clip
//! - gradients: brush space -> local is Bitmap::ctm, linear t = y, radial t = |p|, pad extend
//! - images: the image covers the path bounds, row 0 at the top (uy)
//! - offscreen draws are culled on the CPU by device bounds, as drawList does (--nocull to disable)

use rsd::{Affine as RAffine, ClipKey, Dump, Seg};
use std::sync::Arc;
use vello::{
    Scene,
    kurbo::{Affine, BezPath, Cap, Join, Rect, Stroke},
    peniko::{Blob, BrushRef, Color, Fill, Gradient, ImageAlphaType, ImageBrush, ImageData, ImageFormat},
};

pub struct Assets {
    pub paths: Vec<BezPath>,
    pub grads: Vec<Gradient>,
    pub images: Vec<ImageBrush>,
}

pub fn kurbo(m: RAffine) -> Affine {
    Affine::new(m.0)
}

pub fn assets(d: &Dump) -> Assets {
    let paths = d.paths.iter().map(|p| {
        let mut b = BezPath::new();
        let pt = |x: f32, y: f32| (x as f64, y as f64);
        p.for_each(|s| match s {
            Seg::Move(x, y) => b.move_to(pt(x, y)),
            Seg::Line(x, y) => b.line_to(pt(x, y)),
            Seg::Quad(x1, y1, x, y) => b.quad_to(pt(x1, y1), pt(x, y)),
            Seg::Cubic(x1, y1, x2, y2, x, y) => b.curve_to(pt(x1, y1), pt(x2, y2), pt(x, y)),
            Seg::Close => b.close_path(),
        });
        b
    }).collect();
    let grads = d.grads.iter().map(|g| {
        let stops: Vec<(f32, Color)> = g.locs.iter().zip(&g.rgba).map(|(l, c)| (*l, Color::from_rgba8(c[0], c[1], c[2], c[3]))).collect();
        let base = if g.radial { Gradient::new_radial((0.0, 0.0), 1.0) } else { Gradient::new_linear((0.0, 0.0), (0.0, 1.0)) };
        base.with_stops(stops.as_slice())
    }).collect();
    let images = d.images.iter().map(|im| ImageBrush::new(ImageData {
        data: Blob::new(Arc::new(im.bgra.clone())),
        format: ImageFormat::Bgra8,
        alpha_type: ImageAlphaType::AlphaPremultiplied,
        width: im.w,
        height: im.h,
    })).collect();
    Assets { paths, grads, images }
}

/// Encodes the draws with y-down device transform device. Hairline widths use hair (device, except for a retained scene)
pub fn build_scene(scene: &mut Scene, d: &Dump, a: &Assets, device: RAffine, hair: RAffine, w: f64, h: f64, cull: bool) -> usize {
    scene.reset();
    let (mut current, mut layers, mut encoded) = (None::<ClipKey>, 0, 0);
    for draw in &d.draws {
        let Some(p) = rsd::place(d, draw, device, hair, w, h, cull) else { continue };
        let key = ClipKey::new(d, draw);
        if current != Some(key) {
            for _ in 0..layers {
                scene.pop_layer();
            }
            layers = 0;
            if let Some(r) = key.rect() {
                scene.push_clip_layer(Fill::NonZero, kurbo(p.scene_m), &Rect::new(r.lx, r.ly, r.ux, r.uy));
                layers += 1;
            }
            if key.path != rsd::NONE {
                scene.push_clip_layer(if key.even_odd { Fill::EvenOdd } else { Fill::NonZero }, kurbo(p.scene_m), &a.paths[key.path as usize]);
                layers += 1;
            }
            current = Some(key);
        }
        let solid;
        let (brush, brush_xf): (BrushRef, Option<Affine>) = match draw.paint_type {
            rsd::PAINT_LINEAR | rsd::PAINT_RADIAL => ((&a.grads[draw.paint_index as usize]).into(), Some(kurbo(d.grads[draw.paint_index as usize].ctm))),
            rsd::PAINT_IMAGE => {
                let im = &d.images[draw.paint_index as usize];
                ((&a.images[draw.paint_index as usize]).into(), Some(kurbo(im.to_local(&d.paths[draw.path as usize].bounds))))
            }
            _ => {
                solid = Color::from_rgba8(draw.rgba[0], draw.rgba[1], draw.rgba[2], draw.rgba[3]);
                (solid.into(), None)
            }
        };
        let path = &a.paths[draw.path as usize];
        if draw.width == 0.0 {
            let fill = if draw.flags & rsd::F_EVEN_ODD != 0 { Fill::EvenOdd } else { Fill::NonZero };
            scene.fill(fill, kurbo(p.m), brush, brush_xf, path);
        } else {
            let cap = if draw.flags & rsd::F_ROUND_CAP != 0 { Cap::Round } else if draw.flags & rsd::F_SQUARE_CAP != 0 { Cap::Square } else { Cap::Butt };
            let join = if draw.flags & rsd::F_ROUND_JOIN != 0 { Join::Round } else { Join::Miter };
            let stroke = Stroke::new(p.local_width).with_caps(cap).with_join(join).with_miter_limit(rsd::MITER_LIMIT);
            scene.stroke(&stroke, kurbo(p.m), brush, brush_xf, path);
        }
        encoded += 1;
    }
    for _ in 0..layers {
        scene.pop_layer();
    }
    encoded
}
__VELLO_SRC_LIB_RS__

w $S/vello/src/main.rs <<'__VELLO_SRC_MAIN_RS__'
//! vello_bench: replays an RSD dump written by `rabench export` with Vello, & times each frame.
//!   vello_bench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--aa area|msaa8|msaa16] [--nocull] [--retained]
//!
//! See lib.rs for the mapping from Rasterizer semantics.
//! - --retained encodes once & appends per frame: no culling, & hairlines keep their fit-view width as the view zooms

use rsd::{Affine as RAffine, Opts, Stats};
use std::{num::NonZeroUsize, time::Instant};
use vello::{
    AaConfig, AaSupport, RenderParams, Renderer, RendererOptions, Scene,
    peniko::Color,
    wgpu,
};
use vello_bench::{assets, build_scene, kurbo};

fn main() {
    let o = Opts::parse(&["--aa"]);
    if o.file.is_empty() || o.file.starts_with("--") {
        eprintln!("usage: vello_bench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--aa area|msaa8|msaa16] [--nocull] [--retained]");
        std::process::exit(2);
    }
    let aa = match o.value("--aa") { Some("msaa8") => AaConfig::Msaa8, Some("msaa16") => AaConfig::Msaa16, _ => AaConfig::Area };
    let (cull, retained) = (!o.flag("--nocull"), o.flag("--retained"));
    let (w, h) = (o.w, o.h);
    let t = Instant::now();
    let dump = rsd::load(&o.file);
    let a = assets(&dump);
    let load_ms = t.elapsed().as_secs_f64() * 1e3;

    // Device: high-performance adapter, with its own limits as big scenes need big buffers
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle_from_env());
    let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::HighPerformance,
        ..Default::default()
    }))
    .expect("adapter");
    let (device, queue) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
        label: None,
        required_features: adapter.features() & (wgpu::Features::CLEAR_TEXTURE | wgpu::Features::PIPELINE_CACHE),
        required_limits: adapter.limits(),
        ..Default::default()
    }))
    .expect("device");
    let mut renderer = Renderer::new(&device, RendererOptions {
        use_cpu: false,
        antialiasing_support: AaSupport::all(),
        num_init_threads: NonZeroUsize::new(1),
        pipeline_cache: None,
    })
    .expect("renderer");
    let target = device.create_texture(&wgpu::TextureDescriptor {
        label: None,
        size: wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::COPY_SRC | wgpu::TextureUsages::TEXTURE_BINDING,
        view_formats: &[],
    });
    let view = target.create_view(&wgpu::TextureViewDescriptor::default());
    let params = RenderParams { base_color: Color::WHITE, width: w, height: h, antialiasing_method: aa };

    // --retained: encode once in list space (no culling, hairlines at the fit view), then append with each frame's view
    let flip = RAffine::flip(h as f64);
    let (mut base, mut retained_ms) = (Scene::new(), 0.0);
    if retained {
        let t = Instant::now();
        let v0 = rsd::frame_view(dump.bounds, w as f64, h as f64, 0, o.frames, o.zoom);
        build_scene(&mut base, &dump, &a, RAffine::IDENTITY, flip * v0, w as f64, h as f64, false);
        retained_ms = t.elapsed().as_secs_f64() * 1e3;
    }
    let mut scene = Scene::new();
    let (mut stats, mut encoded_fit, warmup) = (Stats::default(), 0, 3);
    for f in 0..warmup + o.frames {
        let k = f.saturating_sub(warmup);
        let v = flip * rsd::frame_view(dump.bounds, w as f64, h as f64, k, o.frames, o.zoom);
        let t0 = Instant::now();
        let encoded = if retained {
            scene.reset();
            scene.append(&base, Some(kurbo(v)));
            dump.draws.len()
        } else {
            build_scene(&mut scene, &dump, &a, v, v, w as f64, h as f64, cull)
        };
        let t1 = Instant::now();
        renderer.render_to_texture(&device, &queue, &scene, &view, &params).expect("render");
        let t2 = Instant::now();
        device.poll(wgpu::PollType::wait_indefinitely()).expect("poll");
        let t3 = Instant::now();
        stats.record(f, warmup, t0, t1, t2, t3);
        if f == 0 {
            encoded_fit = encoded;
        }
        if f >= warmup && k == o.png_frame && let Some(png) = &o.png {
            readback(&device, &queue, &target, w, h, png);
        }
    }
    let lib = if retained { "vello_retained" } else { "vello" };
    stats.print(lib, &o, dump.draws.len(), &format!("\"encoded_fit\":{encoded_fit},\"load_ms\":{load_ms:.1},\"retained_build_ms\":{retained_ms:.1},"));
}

fn readback(device: &wgpu::Device, queue: &wgpu::Queue, target: &wgpu::Texture, w: u32, h: u32, path: &str) {
    let bpr = (w * 4).div_ceil(256) * 256;
    let buf = device.create_buffer(&wgpu::BufferDescriptor {
        label: None,
        size: (bpr * h) as u64,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut enc = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
    enc.copy_texture_to_buffer(
        target.as_image_copy(),
        wgpu::TexelCopyBufferInfo {
            buffer: &buf,
            layout: wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(bpr), rows_per_image: None },
        },
        wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
    );
    queue.submit([enc.finish()]);
    let slice = buf.slice(..);
    slice.map_async(wgpu::MapMode::Read, |_| {});
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    let data = slice.get_mapped_range().expect("map");
    let mut rgba = Vec::with_capacity((w * h * 4) as usize);
    for row in 0..h {
        let s = (row * bpr) as usize;
        rgba.extend_from_slice(&data[s..s + (w * 4) as usize]);
    }
    drop(data);
    buf.unmap();
    rsd::write_png(path, w, h, &rgba);
}
__VELLO_SRC_MAIN_RS__

w $S/skia/Cargo.toml <<'__SKIA_CARGO_TOML__'
[package]
name = "skia_bench"
version = "0.1.0"
edition = "2024"

[dependencies]
skia-safe = { version = "=0.153.3", features = ["metal", "ganesh"] }
objc2 = "0.6.3"
objc2-metal = { version = "0.3.2", features = ["MTLDevice", "MTLCommandQueue"] }
rsd = { path = "../rsd" }

[profile.release]
debug = 1
__SKIA_CARGO_TOML__

w $S/skia/src/lib.rs <<'__SKIA_SRC_LIB_RS__'
//! The replay of an RSD dump onto a Skia canvas, shared by skia_bench & raviewer.
//!
//! Mapping from Rasterizer semantics (as vello_bench, see rsd):
//! - device = flip(H) * view * scene ctm * draw ctm, set as each draw's canvas matrix
//! - width -1 is a Skia hairline (width 0), other negative widths |width| / sqrt|det| local, recomputed every frame
//! - clip rect & clip path (even-odd, anti-aliased) are pushed once per run of draws with the same clip
//! - gradients: shaders in brush space with Bitmap::ctm as their local matrix. Images: shaders over the path bounds
//! - offscreen draws are culled on the CPU, as drawList does (skia_bench --nocull to disable)

use rsd::{Affine, ClipKey, Dump, Seg};
use skia_safe::{
    AlphaType, ClipOp, Color, ColorType, Data, FilterMode, ImageInfo, Matrix, MipmapMode, Paint, PaintCap, PaintJoin,
    PaintStyle, Path, PathBuilder, PathFillType, Rect, SamplingOptions, Shader, TileMode,
    images,
    gradient::{Colors, Gradient, Interpolation, shaders as gradients},
};

pub struct Assets {
    pub paths: Vec<Path>,
    pub even_odd: Vec<Option<Path>>,
    pub grads: Vec<Shader>,
    pub images: Vec<skia_safe::Image>,
}

pub fn matrix(m: Affine) -> Matrix {
    Matrix::from_affine(&m.0.map(|v| v as f32))
}

pub fn assets(d: &Dump) -> Assets {
    let paths: Vec<Path> = d.paths.iter().map(|p| {
        let mut b = PathBuilder::new();
        p.for_each(|s| {
            match s {
                Seg::Move(x, y) => b.move_to((x, y)),
                Seg::Line(x, y) => b.line_to((x, y)),
                Seg::Quad(x1, y1, x, y) => b.quad_to((x1, y1), (x, y)),
                Seg::Cubic(x1, y1, x2, y2, x, y) => b.cubic_to((x1, y1), (x2, y2), (x, y)),
                Seg::Close => b.close(),
            };
        });
        b.detach()
    }).collect();
    // Even-odd copies (which share their points) for even-odd fills & clip paths
    let mut even_odd: Vec<Option<Path>> = vec![None; paths.len()];
    for draw in &d.draws {
        for (i, needed) in [(draw.path, draw.width == 0.0 && draw.flags & rsd::F_EVEN_ODD != 0), (draw.clip_path, draw.clip_path != rsd::NONE && draw.flags & rsd::F_CLIP_EVEN_ODD != 0)] {
            if needed && even_odd[i as usize].is_none() {
                even_odd[i as usize] = Some(paths[i as usize].with_fill_type(PathFillType::EvenOdd));
            }
        }
    }
    let grads = d.grads.iter().map(|g| {
        let colors: Vec<skia_safe::Color4f> = g.rgba.iter().map(|c| Color::from_argb(c[3], c[0], c[1], c[2]).into()).collect();
        let gradient = Gradient::new(Colors::new(&colors, Some(g.locs.as_slice()), TileMode::Clamp, None), Interpolation::default());
        let local = matrix(g.ctm);
        if g.radial {
            gradients::radial_gradient(((0.0, 0.0), 1.0), &gradient, &local)
        } else {
            gradients::linear_gradient(((0.0, 0.0), (0.0, 1.0)), &gradient, &local)
        }
        .expect("gradient shader")
    }).collect();
    let images = d.images.iter().map(|im| {
        let info = ImageInfo::new((im.w as i32, im.h as i32), ColorType::BGRA8888, AlphaType::Premul, None);
        images::raster_from_data(&info, Data::new_copy(&im.bgra), im.w as usize * 4).expect("image")
    }).collect();
    Assets { paths, even_odd, grads, images }
}

/// Draws the dump into canvas with y-down device transform device. Hairline widths use hair (device, except for a picture)
pub fn draw_dump(canvas: &skia_safe::Canvas, d: &Dump, a: &Assets, device: Affine, hair: Affine, w: f64, h: f64, cull: bool) -> usize {
    let base = canvas.save();
    let (mut current, mut encoded) = (None::<ClipKey>, 0);
    let mut paint = Paint::default();
    paint.set_anti_alias(true);
    for draw in &d.draws {
        let Some(p) = rsd::place(d, draw, device, hair, w, h, cull) else { continue };
        let key = ClipKey::new(d, draw);
        if current != Some(key) {
            canvas.restore_to_count(base);
            canvas.save();
            if key.rect.is_some() || key.path != rsd::NONE {
                canvas.reset_matrix();
                canvas.concat(&matrix(p.scene_m));
                if let Some(r) = key.rect() {
                    canvas.clip_rect(Rect::new(r.lx as f32, r.ly as f32, r.ux as f32, r.uy as f32), ClipOp::Intersect, true);
                }
                if key.path != rsd::NONE {
                    let clip = if key.even_odd { a.even_odd[key.path as usize].as_ref().unwrap() } else { &a.paths[key.path as usize] };
                    canvas.clip_path(clip, ClipOp::Intersect, true);
                }
            }
            current = Some(key);
        }
        match draw.paint_type {
            rsd::PAINT_LINEAR | rsd::PAINT_RADIAL => {
                paint.set_color(Color::BLACK);
                paint.set_shader(a.grads[draw.paint_index as usize].clone());
            }
            rsd::PAINT_IMAGE => {
                let im = &d.images[draw.paint_index as usize];
                let local = matrix(im.to_local(&d.paths[draw.path as usize].bounds));
                let sampling = SamplingOptions::new(FilterMode::Linear, MipmapMode::None);
                paint.set_color(Color::BLACK);
                paint.set_shader(a.images[draw.paint_index as usize].to_shader((TileMode::Clamp, TileMode::Clamp), sampling, &local));
            }
            _ => {
                paint.set_shader(None);
                paint.set_color(Color::from_argb(draw.rgba[3], draw.rgba[0], draw.rgba[1], draw.rgba[2]));
            }
        }
        canvas.reset_matrix();
        canvas.concat(&matrix(p.m));
        let path = &a.paths[draw.path as usize];
        if draw.width == 0.0 {
            paint.set_style(PaintStyle::Fill);
            let path = if draw.flags & rsd::F_EVEN_ODD != 0 { a.even_odd[draw.path as usize].as_ref().unwrap() } else { path };
            canvas.draw_path(path, &paint);
        } else {
            let cap = if draw.flags & rsd::F_ROUND_CAP != 0 { PaintCap::Round } else if draw.flags & rsd::F_SQUARE_CAP != 0 { PaintCap::Square } else { PaintCap::Butt };
            let join = if draw.flags & rsd::F_ROUND_JOIN != 0 { PaintJoin::Round } else { PaintJoin::Miter };
            paint.set_style(PaintStyle::Stroke);
            paint.set_stroke_width(if p.is_unit_hairline { 0.0 } else { p.local_width as f32 });
            paint.set_stroke_cap(cap).set_stroke_join(join).set_stroke_miter(rsd::MITER_LIMIT as f32);
            canvas.draw_path(path, &paint);
        }
        encoded += 1;
    }
    canvas.restore_to_count(base);
    encoded
}
__SKIA_SRC_LIB_RS__

w $S/skia/src/main.rs <<'__SKIA_SRC_MAIN_RS__'
//! skia_bench: replays an RSD dump written by `rabench export` with Skia's Ganesh backend on Metal, & times each frame.
//!   skia_bench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--nocull] [--picture] [--msaa N]
//!
//! See lib.rs for the mapping from Rasterizer semantics.
//! - --picture records an SkPicture once (with an R-tree) & draws it with each frame's view: no CPU culling of
//!   our own, & widths other than -1 keep their fit-view width as the view zooms
//! - --msaa N: Ganesh render target sample count (default 1, Skia's analytic AA)

use rsd::{Affine, Dump, Opts, Stats};
use skia_bench::{Assets, assets, draw_dump, matrix};
use skia_safe::{Color, Picture, PictureRecorder, Rect};
use std::time::Instant;

/// --picture: records the dump once in list space, with Rasterizer's y flip & the fit view's hairline widths
fn record_picture(d: &Dump, a: &Assets, o: &Opts) -> Picture {
    let (w, h) = (o.w as f64, o.h as f64);
    let v0 = Affine::flip(h) * rsd::frame_view(d.bounds, w, h, 0, o.frames, o.zoom);
    let b = d.bounds;
    let pad = 0.5 * ((b[2] - b[0]).max(b[3] - b[1]));
    let mut rec = PictureRecorder::new();
    let canvas = rec.begin_recording(Rect::new(b[0] - pad, b[1] - pad, b[2] + pad, b[3] + pad), true);
    draw_dump(canvas, d, a, Affine::IDENTITY, v0, w, h, false);
    rec.finish_recording_as_picture(None).expect("picture")
}

mod backend {
    use skia_safe::{AlphaType, ColorType, ImageInfo, Surface, gpu};
    pub const NAME: &str = "skia_ganesh";
    pub struct Gpu {
        pub context: gpu::DirectContext,
        pub surface: Surface,
        _device: objc2::rc::Retained<objc2::runtime::ProtocolObject<dyn objc2_metal::MTLDevice>>,
        _queue: objc2::rc::Retained<objc2::runtime::ProtocolObject<dyn objc2_metal::MTLCommandQueue>>,
    }
    impl Gpu {
        pub fn new(w: u32, h: u32, msaa: usize) -> Self {
            use objc2::rc::Retained;
            use objc2_metal::{MTLCreateSystemDefaultDevice, MTLDevice};
            let device = MTLCreateSystemDefaultDevice().expect("Metal device");
            let queue = device.newCommandQueue().expect("command queue");
            let backend = unsafe {
                gpu::mtl::BackendContext::new(Retained::as_ptr(&device) as gpu::mtl::Handle, Retained::as_ptr(&queue) as gpu::mtl::Handle)
            };
            let mut context = gpu::direct_contexts::make_metal(&backend, None).expect("Ganesh Metal context");
            let info = ImageInfo::new((w as i32, h as i32), ColorType::RGBA8888, AlphaType::Premul, None);
            let surface = gpu::surfaces::render_target(&mut context, gpu::Budgeted::Yes, &info, msaa, gpu::SurfaceOrigin::TopLeft, None, false, None)
                .expect("render target");
            Gpu { context, surface, _device: device, _queue: queue }
        }
        pub fn prepare_image(&mut self, image: skia_safe::Image) -> skia_safe::Image {
            image    // Ganesh uploads raster images on first use & caches the texture
        }
        /// Flushes the canvas' work & commits it
        pub fn submit(&mut self) {
            self.context.flush_and_submit();
        }
        pub fn wait(&mut self) {
            self.context.submit(gpu::SyncCpu::Yes);
        }
        pub fn read_rgba(&mut self, w: u32, h: u32) -> Vec<u8> {
            let info = ImageInfo::new((w as i32, h as i32), ColorType::RGBA8888, AlphaType::Premul, None);
            let mut px = vec![0u8; (w * h * 4) as usize];
            assert!(self.surface.read_pixels(&info, &mut px, w as usize * 4, (0, 0)), "read_pixels");
            px
        }
    }
}

fn main() {
    let o = Opts::parse(&["--msaa"]);
    if o.file.is_empty() || o.file.starts_with("--") {
        eprintln!("usage: skia_bench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--nocull] [--picture] [--msaa N]");
        std::process::exit(2);
    }
    let (cull, use_picture) = (!o.flag("--nocull"), o.flag("--picture"));
    let msaa: usize = o.value("--msaa").map(|v| v.parse().unwrap()).unwrap_or(1);
    let (w, h) = (o.w, o.h);
    let t = Instant::now();
    let dump = rsd::load(&o.file);
    let mut a = assets(&dump);
    let load_ms = t.elapsed().as_secs_f64() * 1e3;

    let mut gpu = backend::Gpu::new(w, h, msaa);
    a.images = a.images.drain(..).map(|im| gpu.prepare_image(im)).collect();
    let t = Instant::now();
    let picture = use_picture.then(|| record_picture(&dump, &a, &o));
    let picture_ms = t.elapsed().as_secs_f64() * 1e3;

    let flip = Affine::flip(h as f64);
    let (mut stats, mut encoded_fit, warmup) = (Stats::default(), 0, 3);
    for f in 0..warmup + o.frames {
        let k = f.saturating_sub(warmup);
        let v = flip * rsd::frame_view(dump.bounds, w as f64, h as f64, k, o.frames, o.zoom);
        let t0 = Instant::now();
        let canvas = gpu.surface.canvas();
        canvas.clear(Color::WHITE);
        let encoded = match &picture {
            Some(pic) => {
                canvas.draw_picture(pic, Some(&matrix(v)), None);
                dump.draws.len()
            }
            None => draw_dump(canvas, &dump, &a, v, v, w as f64, h as f64, cull),
        };
        let t1 = Instant::now();
        gpu.submit();
        let t2 = Instant::now();
        gpu.wait();
        let t3 = Instant::now();
        stats.record(f, warmup, t0, t1, t2, t3);
        if f == 0 {
            encoded_fit = encoded;
        }
        if f >= warmup && k == o.png_frame && let Some(png) = &o.png {
            let px = gpu.read_rgba(w, h);
            rsd::write_png(png, w, h, &px);
        }
    }
    let lib = format!("{}{}{}", backend::NAME, if use_picture { "_picture" } else { "" }, if msaa > 1 { format!("_msaa{msaa}") } else { String::new() });
    stats.print(&lib, &o, dump.draws.len(), &format!("\"encoded_fit\":{encoded_fit},\"load_ms\":{load_ms:.1},\"picture_build_ms\":{picture_ms:.1},"));
}
__SKIA_SRC_MAIN_RS__

w $S/rive/rivebench.mm <<'__RIVE_RIVEBENCH_MM__'
//! rivebench: replays an RSD dump written by `rabench export` with the Rive renderer on Metal, & times each frame.
//!   rivebench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--nocull] [--atomics] [--msaa N]
//!
//! Mapping from Rasterizer semantics (as skia_bench, & rsd's lib.rs, which this mirrors in C++):
//! - device = flip(H) * view * scene ctm * draw ctm. Rive keeps transforms on its save stack, so each run of draws with the
//!   same clip saves the scene's transform & clips, & each draw saves its own ctm
//! - Rive has no hairlines: width -1 is a stroke 1 device pixel wide, other negative widths |width| device pixels, recomputed
//!   every frame. Rive's miter limit is fixed at 4, Rasterizer's 3.864
//! - quads are elevated to cubics with RawPath::quadToCubic, as the renderer only draws cubics (asserted only in debug)
//! - clip rect & clip path (anti-aliased, with their rule) are pushed once per run of draws with the same clip
//! - gradients: in brush space with Bitmap::ctm as their shader transform. Images: modulated image paints over the path bounds
//! - offscreen draws are culled on the CPU, as drawList does (--nocull to disable)
//! - Rive picks its interlock mode, raster ordering on Apple GPUs, with shaders compiled synchronously & no dithering.
//!   --atomics disables raster ordering; --msaa N forces its depth-stencil mode with N samples (1 for none)

#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#import <CoreServices/CoreServices.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "rive/renderer/rive_renderer.hpp"
#include "rive/renderer/rive_render_image.hpp"
#include "rive/renderer/rive_render_paint.hpp"
#include "rive/renderer/render_context.hpp"
#include "rive/renderer/metal/render_context_metal_impl.h"

namespace rb {    // raviewer includes this file for its replay, without main
using namespace rive;
using gpu::RenderContext;
using Clock = std::chrono::steady_clock;

enum { kMove, kLine, kQuad, kCubic, kClose };
enum { kEvenOdd = 1 << 1, kRoundCap = 1 << 2, kSquareCap = 1 << 3, kRoundJoin = 1 << 4, kClipEvenOdd = 1 << 5 };
enum { kPaintColor, kLinear, kRadial, kImage };
static const uint32_t kNone = UINT32_MAX;
static const float kHuge = 5e11f;

/// x' = a x + c y + e, y' = b x + d y + f, as Ra::Transform & rive::Mat2D
struct Affine {
    double m[6] = { 1, 0, 0, 1, 0, 0 };
    static Affine make(double a, double b, double c, double d, double e, double f) { Affine t; t.m[0] = a, t.m[1] = b, t.m[2] = c, t.m[3] = d, t.m[4] = e, t.m[5] = f; return t; }
    static Affine translate(double x, double y) { return make(1, 0, 0, 1, x, y); }
    static Affine scale(double s) { return make(s, 0, 0, s, 0, 0); }
    static Affine flip(double h) { return make(1, 0, 0, -1, 0, h); }     // Rasterizer's y-up device space to a y-down one of height h
    double det() const { return m[0] * m[3] - m[1] * m[2]; }
    void apply(double x, double y, double& px, double& py) const { px = m[0] * x + m[2] * y + m[4], py = m[1] * x + m[3] * y + m[5]; }
    Affine operator*(const Affine& r) const {    // Applies r first
        const double *a = m, *b = r.m;
        return make(a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
                    a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]);
    }
    Mat2D mat() const { return Mat2D(float(m[0]), float(m[1]), float(m[2]), float(m[3]), float(m[4]), float(m[5])); }
};

struct Bounds {
    double lx = 1e300, ly = 1e300, ux = -1e300, uy = -1e300;
    Bounds transformed(const Affine& t) const {
        Bounds b;  double x, y, cs[4][2] = { { lx, ly }, { ux, ly }, { lx, uy }, { ux, uy } };
        for (auto& c : cs)
            t.apply(c[0], c[1], x, y), b.lx = fmin(b.lx, x), b.ly = fmin(b.ly, y), b.ux = fmax(b.ux, x), b.uy = fmax(b.uy, y);
        return b;
    }
};

struct Path { std::vector<uint8_t> types;  std::vector<float> pts;  Bounds bounds; };
struct Draw { uint32_t scene, path;  Affine ctm;  uint8_t paintType, flags, rgba[4];  uint32_t paintIndex;  float width, clip[4];  uint32_t clipPath; };
struct Gradient { bool radial;  Affine ctm;  std::vector<float> locs;  std::vector<ColorInt> colors; };
struct Image { uint32_t w, h;  std::vector<uint8_t> bgra; };
struct Dump {
    float bounds[4];  std::vector<Affine> sceneCtms;  std::vector<std::array<float, 4>> sceneClips;
    std::vector<Path> paths;  std::vector<Gradient> grads;  std::vector<Image> images;  std::vector<Draw> draws;
};

struct Reader {
    const uint8_t *p, *end;
    const uint8_t *bytes(size_t n) { if (p + n > end) { fprintf(stderr, "rivebench: truncated dump\n"); exit(1); } const uint8_t *s = p; p += n; return s; }
    uint32_t u32() { uint32_t v; memcpy(& v, bytes(4), 4); return v; }
    float f32() { float v; memcpy(& v, bytes(4), 4); return v; }
    Affine affine() { float t[6]; for (float& v : t) v = f32(); return Affine::make(t[0], t[1], t[2], t[3], t[4], t[5]); }
};

/// The RSD dump, as rsd::load
static Dump load(const char *file) {
    FILE *fp = fopen(file, "rb");
    if (!fp) { perror(file); exit(1); }
    std::vector<uint8_t> data;  uint8_t buf[1 << 16];  size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        data.insert(data.end(), buf, buf + n);
    fclose(fp);
    Reader r { data.data(), data.data() + data.size() };
    if (memcmp(r.bytes(4), "RSD1", 4) != 0 || r.u32() != 1) { fprintf(stderr, "rivebench: %s isn't an RSD1 dump\n", file); exit(1); }
    Dump d;
    for (float& v : d.bounds) v = r.f32();
    for (uint32_t i = 0, count = r.u32(); i < count; i++) {
        d.sceneCtms.push_back(r.affine());
        std::array<float, 4> c;  for (float& v : c) v = r.f32();
        d.sceneClips.push_back(c);
    }
    d.paths.resize(r.u32());
    for (Path& path : d.paths) {
        uint32_t count = r.u32();
        const uint8_t *types = r.bytes(count);
        path.types.assign(types, types + count), r.bytes((4 - (count & 3)) & 3);
        path.pts.resize(2 * count);
        for (float& v : path.pts) v = r.f32();
        for (uint32_t i = 0; i < count; i++) {
            double x = path.pts[2 * i], y = path.pts[2 * i + 1];
            path.bounds.lx = fmin(path.bounds.lx, x), path.bounds.ly = fmin(path.bounds.ly, y), path.bounds.ux = fmax(path.bounds.ux, x), path.bounds.uy = fmax(path.bounds.uy, y);
        }
    }
    d.grads.resize(r.u32());
    for (Gradient& g : d.grads) {
        g.radial = r.u32() == 2, g.ctm = r.affine();
        uint32_t count = r.u32();
        for (uint32_t i = 0; i < count; i++) g.locs.push_back(r.f32());
        for (uint32_t i = 0; i < count; i++) { const uint8_t *c = r.bytes(4); g.colors.push_back(colorARGB(c[3], c[2], c[1], c[0])); }
    }
    d.images.resize(r.u32());
    for (Image& im : d.images) {
        im.w = r.u32(), im.h = r.u32();
        const uint8_t *px = r.bytes(4 * size_t(im.w) * im.h);
        im.bgra.assign(px, px + 4 * size_t(im.w) * im.h);
    }
    d.draws.resize(r.u32());
    for (Draw& draw : d.draws) {
        draw.scene = r.u32(), draw.path = r.u32(), draw.ctm = r.affine();
        const uint8_t *pt = r.bytes(4), *bgra = r.bytes(4);
        draw.paintType = pt[0], draw.flags = pt[1];
        draw.rgba[0] = bgra[2], draw.rgba[1] = bgra[1], draw.rgba[2] = bgra[0], draw.rgba[3] = bgra[3];
        draw.paintIndex = r.u32(), draw.width = r.f32();
        for (float& v : draw.clip) v = r.f32();
        draw.clipPath = r.u32();
    }
    return d;
}

/// Bounds::fitTransform(bounds) into WxH, then a zoom by zmax^(k/(n-1)) around the centre, as rabench's frameView
static Affine frameView(const float b[4], double w, double h, size_t k, size_t n, double zmax) {
    double bw = b[2] - b[0], bh = b[3] - b[1], s = fmin(w / bw, h / bh);
    Affine fit = Affine::make(s, 0, 0, s, 0.5 * (w - s * bw) - s * b[0], 0.5 * (h - s * bh) - s * b[1]);
    double z = n > 1 ? pow(zmax, double(k) / double(n - 1)) : 1.0, cx = 0.5 * w, cy = 0.5 * h;
    return Affine::translate(cx, cy) * Affine::scale(z) * Affine::translate(-cx, -cy) * fit;
}

/// The clip a run of draws shares: the scene clip & draw clip rect (scene space), or none for a huge rect, & clip path & its rule
struct ClipKey {
    uint32_t scene = kNone, path = kNone;  bool hasRect = false, evenOdd = false;  float rect[4] = {};
    ClipKey() {}
    ClipKey(const Dump& d, const Draw& draw) : scene(draw.scene), path(draw.clipPath) {
        const auto& s = d.sceneClips[draw.scene];
        rect[0] = fmaxf(s[0], draw.clip[0]), rect[1] = fmaxf(s[1], draw.clip[1]), rect[2] = fminf(s[2], draw.clip[2]), rect[3] = fminf(s[3], draw.clip[3]);
        hasRect = !(rect[0] <= -kHuge && rect[1] <= -kHuge && rect[2] >= kHuge && rect[3] >= kHuge);
        evenOdd = draw.clipPath != kNone && (draw.flags & kClipEvenOdd);
    }
    bool operator==(const ClipKey& o) const {
        return scene == o.scene && path == o.path && hasRect == o.hasRect && evenOdd == o.evenOdd && (!hasRect || memcmp(rect, o.rect, sizeof(rect)) == 0);
    }
};

/// A draw's device transforms & stroke width for a frame, or false if it's culled (offscreen, as drawList does) or degenerate
struct Placed { Affine sceneM, m;  double localWidth; };
static bool place(const Dump& d, const Draw& draw, const Affine& device, double w, double h, bool cull, Placed& p) {
    p.sceneM = device * d.sceneCtms[draw.scene], p.m = p.sceneM * draw.ctm;
    double det = fabs(p.m.det());
    if (det == 0.0 && draw.width >= 0.f)
        return false;
    double devWidth = draw.width > 0.f ? draw.width * sqrt(det) : -draw.width;
    if (cull) {
        Bounds b = d.paths[draw.path].bounds.transformed(p.m);
        if (b.ux + devWidth < 0.0 || b.uy + devWidth < 0.0 || b.lx - devWidth > w || b.ly - devWidth > h)
            return false;
    }
    p.localWidth = draw.width >= 0.f ? draw.width : -draw.width / sqrt(det);
    return true;
}

static rcp<RenderPath> makePath(Factory *factory, const Path& path, FillRule rule) {
    RawPath raw;
    const float *p = path.pts.data();
    for (size_t i = 0; i < path.types.size(); ) {
        switch (path.types[i]) {
            case kMove:  raw.moveTo(p[2 * i], p[2 * i + 1]), i += 1; break;
            case kLine:  raw.lineTo(p[2 * i], p[2 * i + 1]), i += 1; break;
            case kQuad:  raw.quadToCubic(p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3]), i += 2; break;
            case kCubic: raw.cubicTo(p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3], p[2 * i + 4], p[2 * i + 5]), i += 3; break;
            case kClose: raw.close(), i += 1; break;
            default: fprintf(stderr, "rivebench: bad path type %d\n", path.types[i]); exit(1);
        }
    }
    return factory->makeRenderPath(raw, rule);
}

/// The dump's Rive objects: each path non-zero, & an even-odd copy for even-odd fills & clip paths, clip rects by scene clip
struct Assets {
    std::vector<rcp<RenderPath>> paths, evenOdd;
    std::vector<rcp<RenderShader>> grads;
    std::vector<Affine> gradCtms;
    std::vector<rcp<RenderImage>> images;
    rcp<RenderPaint> paint;
};

static Assets makeAssets(const Dump& d, RenderContext *context) {
    Assets a;
    for (const Path& path : d.paths)
        a.paths.push_back(makePath(context, path, FillRule::nonZero));
    a.evenOdd.resize(d.paths.size());
    for (const Draw& draw : d.draws)
        for (auto [i, needed] : { std::pair<uint32_t, bool>(draw.path, draw.width == 0.f && (draw.flags & kEvenOdd)), std::pair<uint32_t, bool>(draw.clipPath, draw.clipPath != kNone && (draw.flags & kClipEvenOdd)) })
            if (needed && a.evenOdd[i] == nullptr)
                a.evenOdd[i] = makePath(context, d.paths[i], FillRule::evenOdd);
    for (const Gradient& g : d.grads) {     // Brush space: linear t = y from 0 to 1, radial t = |p|, pad extend
        a.grads.push_back(g.radial ? context->makeRadialGradient(0.f, 0.f, 1.f, g.colors.data(), g.locs.data(), g.colors.size())
                                   : context->makeLinearGradient(0.f, 0.f, 0.f, 1.f, g.colors.data(), g.locs.data(), g.colors.size()));
        a.gradCtms.push_back(g.ctm);
    }
    auto *impl = context->static_impl_cast<gpu::RenderContextMetalImpl>();
    for (const Image& im : d.images) {      // BGRA, premultiplied, to RGBA
        std::vector<uint8_t> rgba(im.bgra.size());
        for (size_t i = 0; i < rgba.size(); i += 4)
            rgba[i] = im.bgra[i + 2], rgba[i + 1] = im.bgra[i + 1], rgba[i + 2] = im.bgra[i], rgba[i + 3] = im.bgra[i + 3];
        a.images.push_back(make_rcp<RiveRenderImage>(impl->makeImageTexture(im.w, im.h, 1, GPUTextureFormat::rgba32, rgba.data())));
    }
    a.paint = context->makeRenderPaint();
    return a;
}

/// Draws the dump with y-down device transform device, & returns the number of draws encoded
static size_t drawDump(Renderer& renderer, RenderContext *context, const Dump& d, Assets& a, const Affine& device, double w, double h, bool cull) {
    size_t encoded = 0;  bool hasKey = false;  ClipKey current;
    RenderPaint *paint = a.paint.get();
    renderer.save();
    for (const Draw& draw : d.draws) {
        Placed p;
        if (!place(d, draw, device, w, h, cull, p))
            continue;
        ClipKey key(d, draw);
        if (!hasKey || !(key == current)) {
            renderer.restore(), renderer.save();
            renderer.transform(p.sceneM.mat());
            if (key.hasRect) {
                RawPath rect;
                rect.addRect(AABB(key.rect[0], key.rect[1], key.rect[2], key.rect[3]));
                renderer.clipPath(context->makeRenderPath(rect, FillRule::nonZero).get());
            }
            if (key.path != kNone)
                renderer.clipPath((key.evenOdd ? a.evenOdd[key.path] : a.paths[key.path]).get());
            current = key, hasKey = true;
        }
        paint->shader(nullptr), paint->modulatedImage(nullptr, ImageSampler::LinearClamp(), Mat2D());
        if (draw.paintType == kLinear || draw.paintType == kRadial) {
            paint->color(0xFF000000);
            paint->shader(a.grads[draw.paintIndex]);
            paint->shaderTransform(a.gradCtms[draw.paintIndex].mat());
        } else if (draw.paintType == kImage) {     // The image covers the path bounds, row 0 at uy
            const Bounds& b = d.paths[draw.path].bounds;
            paint->color(0xFFFFFFFF);
            paint->modulatedImage(a.images[draw.paintIndex].get(), ImageSampler::LinearClamp(), Affine::make(b.ux - b.lx, 0, 0, b.ly - b.uy, b.lx, b.uy).mat());
        } else
            paint->color(colorARGB(draw.rgba[3], draw.rgba[0], draw.rgba[1], draw.rgba[2]));
        RenderPath *path = a.paths[draw.path].get();
        if (draw.width == 0.f) {
            paint->style(RenderPaintStyle::fill);
            if (draw.flags & kEvenOdd)
                path = a.evenOdd[draw.path].get();
        } else {
            paint->style(RenderPaintStyle::stroke);
            paint->thickness(float(p.localWidth));
            paint->cap(draw.flags & kRoundCap ? StrokeCap::round : draw.flags & kSquareCap ? StrokeCap::square : StrokeCap::butt);
            paint->join(draw.flags & kRoundJoin ? StrokeJoin::round : StrokeJoin::miter);
        }
        renderer.save();
        renderer.transform(draw.ctm.mat());
        renderer.drawPath(path, paint);
        renderer.restore();
        encoded++;
    }
    renderer.restore();
    return encoded;
}

static void writePNG(const char *path, uint32_t w, uint32_t h, std::vector<uint8_t>& rgba) {
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, 4 * w, rgb, kCGImageAlphaPremultipliedLast);
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, kUTTypePNG, 1, nullptr);
    CGImageDestinationAddImage(dst, image, nullptr);
    if (!CGImageDestinationFinalize(dst)) { fprintf(stderr, "rivebench: can't write %s\n", path); exit(1); }
    CFRelease(dst), CFRelease(url), CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
}

static double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
static double minimum(const std::vector<double>& v) { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); }
}    // namespace rb

#if !RIVEBENCH_NO_MAIN
int main(int argc, const char **argv) {
    using namespace rb;
    using namespace rive;
    @autoreleasepool {
        const char *file = argc > 1 ? argv[1] : "", *png = nullptr;
        uint32_t w = 2048, h = 2048, msaa = 0;  size_t frames = 60, pngFrame = 0;  double zoom = 8.0;  bool cull = true, atomics = false;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            auto next = [&]() { if (i + 1 >= argc) { fprintf(stderr, "rivebench: %s needs a value\n", argv[i]); exit(2); } return argv[++i]; };
            if (arg == "--size") { const char *s = next(); w = uint32_t(atoi(s)), h = uint32_t(atoi(strchr(s, 'x') ? strchr(s, 'x') + 1 : s)); }
            else if (arg == "--frames") frames = size_t(std::max(1, atoi(next())));
            else if (arg == "--zoom") zoom = atof(next());
            else if (arg == "--png") png = next();
            else if (arg == "--pngframe") pngFrame = size_t(atoi(next()));
            else if (arg == "--msaa") msaa = uint32_t(atoi(next()));
            else if (arg == "--nocull") cull = false;
            else if (arg == "--atomics") atomics = true;
        }
        if (!*file || file[0] == '-') {
            fprintf(stderr, "usage: rivebench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--nocull] [--atomics] [--msaa N]\n");
            return 2;
        }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        gpu::RenderContextMetalImpl::ContextOptions options;
        options.shaderCompilationMode = gpu::ShaderCompilationMode::alwaysSynchronous;
        std::unique_ptr<RenderContext> context = gpu::RenderContextMetalImpl::MakeContext(device, options);
        auto *impl = context->static_impl_cast<gpu::RenderContextMetalImpl>();
        impl->setCommandQueue(queue);
        rcp<gpu::RenderTargetMetal> target = impl->makeRenderTarget(MTLPixelFormatRGBA8Unorm, w, h);
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.storageMode = MTLStorageModeShared;
        target->setTargetTexture([device newTextureWithDescriptor:desc]);

        auto t = Clock::now();
        Dump dump = load(file);
        Assets assets = makeAssets(dump, context.get());
        double loadMs = std::chrono::duration<double, std::milli>(Clock::now() - t).count();

        RenderContext::FrameDescriptor frame;
        frame.renderTargetWidth = w, frame.renderTargetHeight = h, frame.loadAction = gpu::LoadAction::clear, frame.clearColor = 0xFFFFFFFF;
        frame.msaaSampleCount = msaa, frame.disableRasterOrdering = atomics, frame.ditherMode = gpu::DitherMode::none;
        Affine flip = Affine::flip(h);
        const size_t warmup = 3;
        std::vector<double> encode, cpu, submitToDone, wall;
        double firstCpu = 0, firstWall = 0;  size_t encodedFit = 0;
        gpu::InterlockMode mode = gpu::InterlockMode::rasterOrdering;
        auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        for (size_t f = 0; f < warmup + frames; f++) {
            @autoreleasepool {
                size_t k = f < warmup ? 0 : f - warmup;
                Affine v = flip * frameView(dump.bounds, w, h, k, frames, zoom);
                auto t0 = Clock::now();
                context->beginFrame(frame);
                mode = context->frameInterlockMode();
                RiveRenderer renderer(context.get());
                size_t encoded = drawDump(renderer, context.get(), dump, assets, v, w, h, cull);
                auto t1 = Clock::now();
                id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
                context->flush({ .renderTarget = target.get(), .externalCommandBuffer = (__bridge void *)commandBuffer });
                [commandBuffer commit];
                auto t2 = Clock::now();
                [commandBuffer waitUntilCompleted];
                auto t3 = Clock::now();
                if (commandBuffer.error) { fprintf(stderr, "rivebench: %s\n", commandBuffer.error.localizedDescription.UTF8String); return 1; }
                if (f == 0)
                    firstCpu = ms(t0, t2), firstWall = ms(t0, t3), encodedFit = encoded;
                if (f >= warmup)
                    encode.push_back(ms(t0, t1)), cpu.push_back(ms(t0, t2)), submitToDone.push_back(ms(t2, t3)), wall.push_back(ms(t0, t3));
                if (f >= warmup && k == pngFrame && png) {
                    std::vector<uint8_t> rgba(4 * size_t(w) * h);
                    [target->targetTexture() getBytes:rgba.data() bytesPerRow:4 * w fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
                    writePNG(png, w, h, rgba);
                }
            }
        }
        std::string stem = file;
        stem = stem.substr(stem.find_last_of('/') + 1), stem = stem.substr(0, stem.find_last_of('.'));
        const char *modes[] = { "raster_ordering", "atomics", "clockwise", "clockwise_atomic", "depth_stencil" };
        std::string lib = std::string("rive") + (atomics ? "_atomics" : "") + (msaa > 1 ? "_msaa" + std::to_string(msaa) : msaa == 1 ? "_depth" : "");
        printf("{\"lib\":\"%s\",\"file\":\"%s\",\"w\":%u,\"h\":%u,\"frames\":%zu,\"zoom\":%g,\"draws\":%zu,\"encoded_fit\":%zu,\"load_ms\":%.1f,\"interlock\":\"%s\","
               "\"first_cpu_ms\":%.3f,\"first_wall_ms\":%.3f,\"encode_min\":%.3f,\"encode_med\":%.3f,\"cpu_min\":%.3f,\"cpu_med\":%.3f,"
               "\"submit_to_done_min\":%.3f,\"submit_to_done_med\":%.3f,\"wall_min\":%.3f,\"wall_med\":%.3f}\n",
               lib.c_str(), stem.c_str(), w, h, frames, zoom, dump.draws.size(), encodedFit, loadMs, modes[int(mode)], firstCpu, firstWall,
               minimum(encode), median(encode), minimum(cpu), median(cpu), minimum(submitToDone), median(submitToDone), minimum(wall), median(wall));
    }
    return 0;
}
#endif
__RIVE_RIVEBENCH_MM__

w $S/viewer/Cargo.toml <<'__VIEWER_CARGO_TOML__'
[package]
name = "raviewer"
version = "0.1.0"
edition = "2024"

[lib]
crate-type = ["staticlib"]

[dependencies]
rsd = { path = "../rsd" }
vello_bench = { path = "../vello" }
skia_bench = { path = "../skia" }
vello = "=0.11.0"
skia-safe = { version = "=0.153.3", features = ["metal", "ganesh"] }
pollster = "0.4"

[profile.release]
debug = 1

# As vello_bench's: Vello's dynamic buffers scaled for the big PDFs
[patch.crates-io]
vello_encoding = { path = "../../vendor/vello_encoding" }
__VIEWER_CARGO_TOML__

w $S/viewer/src/lib.rs <<'__VIEWER_SRC_LIB_RS__'
//! raviewer's Skia & Vello renderers, as a static library with a C interface (see viewer.h). Each loads an RSD dump, & draws
//! it with a view, Rasterizer's y-up device transform: Skia into a Metal texture with raviewer's device & queue, Vello into its
//! own CAMetalLayer through a wgpu surface. Their replays are skia_bench's & vello_bench's, with CPU culling.

use rsd::{Affine, Dump};
use skia_safe::{Color, ColorType, gpu};
use std::{
    ffi::{CStr, c_char, c_void},
    num::NonZeroUsize,
    ptr::null_mut,
    time::Instant,
};
use vello::{AaConfig, AaSupport, RenderParams, Renderer, RendererOptions, Scene, wgpu};

/// The y-down device transform for a frame of height h, from raviewer's view
unsafe fn device(view: *const f64, h: u32) -> Affine {
    let v = unsafe { std::slice::from_raw_parts(view, 6) };
    Affine::flip(h as f64) * Affine([v[0], v[1], v[2], v[3], v[4], v[5]])
}

unsafe fn path(p: *const c_char) -> String {
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

pub struct Skia {
    context: gpu::DirectContext,
    scene: Option<(Dump, skia_bench::Assets)>,
}

/// A Ganesh context on raviewer's MTLDevice & MTLCommandQueue, which outlive it
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_skia_new(device: *mut c_void, queue: *mut c_void) -> *mut Skia {
    let backend = unsafe { gpu::mtl::BackendContext::new(device as gpu::mtl::Handle, queue as gpu::mtl::Handle) };
    match gpu::direct_contexts::make_metal(&backend, None) {
        Some(context) => Box::into_raw(Box::new(Skia { context, scene: None })),
        None => null_mut(),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_skia_load(s: *mut Skia, file: *const c_char) {
    let s = unsafe { &mut *s };
    let dump = rsd::load(&unsafe { path(file) });
    let assets = skia_bench::assets(&dump);
    s.scene = Some((dump, assets));
}

/// Draws into texture, an MTLTexture of w x h, & submits the work to the queue. Returns the number of draws encoded
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_skia_draw(s: *mut Skia, texture: *mut c_void, w: u32, h: u32, view: *const f64) -> usize {
    let s = unsafe { &mut *s };
    let Some((dump, assets)) = &s.scene else { return 0 };
    let info = unsafe { gpu::mtl::TextureInfo::new(texture as gpu::mtl::Handle) };
    let target = gpu::backend_render_targets::make_mtl((w as i32, h as i32), &info);
    let Some(mut surface) =
        gpu::surfaces::wrap_backend_render_target(&mut s.context, &target, gpu::SurfaceOrigin::TopLeft, ColorType::BGRA8888, None, None)
    else {
        return 0;
    };
    let v = unsafe { device(view, h) };
    let canvas = surface.canvas();
    canvas.clear(Color::WHITE);
    let encoded = skia_bench::draw_dump(canvas, dump, assets, v, v, w as f64, h as f64, true);
    drop(surface);
    s.context.flush_and_submit();
    encoded
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_skia_free(s: *mut Skia) {
    if !s.is_null() {
        drop(unsafe { Box::from_raw(s) });
    }
}

pub struct Vello {
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    renderer: Renderer,
    blitter: wgpu::util::TextureBlitter,
    format: wgpu::TextureFormat,
    size: (u32, u32),
    target: Option<(wgpu::Texture, wgpu::TextureView)>,
    scene: Scene,
    dump: Option<(Dump, vello_bench::Assets)>,
}

/// A wgpu device, as vello_bench's, presenting to layer, a CAMetalLayer that outlives it
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_vello_new(layer: *mut c_void) -> *mut Vello {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle_from_env());
    let Ok(surface) = (unsafe { instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::CoreAnimationLayer(layer)) }) else {
        return null_mut();
    };
    let Ok(adapter) = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::HighPerformance,
        compatible_surface: Some(&surface),
        ..Default::default()
    })) else {
        return null_mut();
    };
    let Ok((device, queue)) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
        label: None,
        required_features: adapter.features() & (wgpu::Features::CLEAR_TEXTURE | wgpu::Features::PIPELINE_CACHE),
        required_limits: adapter.limits(),
        ..Default::default()
    })) else {
        return null_mut();
    };
    let Ok(renderer) = Renderer::new(&device, RendererOptions {
        use_cpu: false,
        antialiasing_support: AaSupport::area_only(),
        num_init_threads: NonZeroUsize::new(1),
        pipeline_cache: None,
    }) else {
        return null_mut();
    };
    let format = wgpu::TextureFormat::Bgra8Unorm;
    let blitter = wgpu::util::TextureBlitter::new(&device, format);
    Box::into_raw(Box::new(Vello { surface, device, queue, renderer, blitter, format, size: (0, 0), target: None, scene: Scene::new(), dump: None }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_vello_load(v: *mut Vello, file: *const c_char) {
    let v = unsafe { &mut *v };
    let dump = rsd::load(&unsafe { path(file) });
    let assets = vello_bench::assets(&dump);
    v.dump = Some((dump, assets));
}

/// Renders a w x h frame into Vello's storage texture, blits it to the surface's & presents it, waiting for the GPU between
/// the two, or only renders it while the window is occluded. times gets the CPU ms to submit, & submit to GPU done.
/// Returns the number of draws encoded
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_vello_draw(v: *mut Vello, w: u32, h: u32, view: *const f64, times: *mut f64) -> usize {
    let v = unsafe { &mut *v };
    let Some((dump, assets)) = &v.dump else { return 0 };
    if v.size != (w, h) {
        v.surface.configure(&v.device, &wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: v.format,
            width: w,
            height: h,
            present_mode: wgpu::PresentMode::AutoVsync,
            desired_maximum_frame_latency: 2,
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            view_formats: vec![],
            color_space: wgpu::SurfaceColorSpace::Auto,
        });
        let texture = v.device.create_texture(&wgpu::TextureDescriptor {
            label: None,
            size: wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        v.target = Some((texture, view));
        v.size = (w, h);
    }
    let Some((_, target)) = &v.target else { return 0 };
    let t0 = Instant::now();
    let m = unsafe { device(view, h) };
    let encoded = vello_bench::build_scene(&mut v.scene, dump, assets, m, m, w as f64, h as f64, true);
    let params = RenderParams { base_color: vello::peniko::Color::WHITE, width: w, height: h, antialiasing_method: AaConfig::Area };
    if let Err(e) = v.renderer.render_to_texture(&v.device, &v.queue, &v.scene, target, &params) {
        eprintln!("raviewer: Vello can't render: {e}");
        return 0;
    }
    // wgpu has no texture for a surface in an occluded window, when the frame is drawn but not shown
    let frame = match v.surface.get_current_texture() {
        wgpu::CurrentSurfaceTexture::Success(frame) | wgpu::CurrentSurfaceTexture::Suboptimal(frame) => Some(frame),
        _ => None,
    };
    if let Some(frame) = &frame {
        let frame_view = frame.texture.create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = v.device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        v.blitter.copy(&v.device, &mut encoder, target, &frame_view);
        v.queue.submit([encoder.finish()]);
    }
    let t1 = Instant::now();
    let _ = v.device.poll(wgpu::PollType::wait_indefinitely());
    let t2 = Instant::now();
    if let Some(frame) = frame {
        v.queue.present(frame);
    }
    let times = unsafe { std::slice::from_raw_parts_mut(times, 2) };
    times[0] = (t1 - t0).as_secs_f64() * 1e3;
    times[1] = (t2 - t1).as_secs_f64() * 1e3;
    encoded
}

/// Copies the last frame, RGBA rows from the top, into rgba, which holds 4 w h bytes for the last draw's w & h
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_vello_read(v: *mut Vello, rgba: *mut u8) -> bool {
    let v = unsafe { &mut *v };
    let (Some((texture, _)), (w, h)) = (&v.target, v.size) else { return false };
    let bpr = (w * 4).div_ceil(256) * 256;
    let buf = v.device.create_buffer(&wgpu::BufferDescriptor {
        label: None,
        size: (bpr * h) as u64,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut encoder = v.device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
    encoder.copy_texture_to_buffer(
        texture.as_image_copy(),
        wgpu::TexelCopyBufferInfo { buffer: &buf, layout: wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(bpr), rows_per_image: None } },
        wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
    );
    v.queue.submit([encoder.finish()]);
    let slice = buf.slice(..);
    slice.map_async(wgpu::MapMode::Read, |_| {});
    let _ = v.device.poll(wgpu::PollType::wait_indefinitely());
    let Ok(data) = slice.get_mapped_range() else { return false };
    let out = unsafe { std::slice::from_raw_parts_mut(rgba, (w * h * 4) as usize) };
    for row in 0..h as usize {
        let (s, d, n) = (row * bpr as usize, row * w as usize * 4, w as usize * 4);
        out[d..d + n].copy_from_slice(&data[s..s + n]);
    }
    true
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rv_vello_free(v: *mut Vello) {
    if !v.is_null() {
        drop(unsafe { Box::from_raw(v) });
    }
}
__VIEWER_SRC_LIB_RS__

w $S/viewer/viewer.h <<'__VIEWER_VIEWER_H__'
//  raviewer's renderers outside raviewer.mm, as C functions: Skia & Vello from the raviewer Rust library (src/lib.rs), Rive from
//  rive_view.mm. Each loads an RSD dump & draws it with view, Rasterizer's y-up device transform of the w x h frame
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct Skia Skia;
typedef struct Vello Vello;
typedef struct RiveView RiveView;

Skia *rv_skia_new(void *device, void *queue);
void rv_skia_load(Skia *skia, const char *path);
size_t rv_skia_draw(Skia *skia, void *texture, uint32_t w, uint32_t h, const double view[6]);
void rv_skia_free(Skia *skia);

Vello *rv_vello_new(void *layer);
void rv_vello_load(Vello *vello, const char *path);
size_t rv_vello_draw(Vello *vello, uint32_t w, uint32_t h, const double view[6], double times[2]);
bool rv_vello_read(Vello *vello, uint8_t *rgba);
void rv_vello_free(Vello *vello);

RiveView *rv_rive_new(void *device, void *queue);
void rv_rive_load(RiveView *rive, const char *path);
size_t rv_rive_draw(RiveView *rive, void *texture, uint32_t w, uint32_t h, const double view[6], void *commandBuffer);
void rv_rive_free(RiveView *rive);
#ifdef __cplusplus
}
#endif
__VIEWER_VIEWER_H__

w $S/viewer/rive_view.mm <<'__VIEWER_RIVE_VIEW_MM__'
//  raviewer's Rive renderer: rivebench's replay, drawn into raviewer's drawable texture & command buffer
#define RIVEBENCH_NO_MAIN 1
#include "../rive/rivebench.mm"
#include "viewer.h"

struct RiveView {
    std::unique_ptr<rb::RenderContext> context;
    rive::rcp<rive::gpu::RenderTargetMetal> target;
    rb::Dump dump;  rb::Assets assets;  bool loaded = false;
};

RiveView *rv_rive_new(void *device, void *queue) {
    rive::gpu::RenderContextMetalImpl::ContextOptions options;
    options.shaderCompilationMode = rive::gpu::ShaderCompilationMode::alwaysSynchronous;
    RiveView *r = new RiveView;
    r->context = rive::gpu::RenderContextMetalImpl::MakeContext((__bridge id<MTLDevice>)device, options);
    r->context->static_impl_cast<rive::gpu::RenderContextMetalImpl>()->setCommandQueue((__bridge id<MTLCommandQueue>)queue);
    return r;
}

void rv_rive_load(RiveView *r, const char *path) {
    r->assets = rb::Assets(), r->dump = rb::load(path), r->assets = rb::makeAssets(r->dump, r->context.get()), r->loaded = true;
}

size_t rv_rive_draw(RiveView *r, void *texture, uint32_t w, uint32_t h, const double view[6], void *commandBuffer) {
    if (!r->loaded)
        return 0;
    if (!r->target || r->target->width() != w || r->target->height() != h)
        r->target = r->context->static_impl_cast<rive::gpu::RenderContextMetalImpl>()->makeRenderTarget(MTLPixelFormatBGRA8Unorm, w, h);
    r->target->setTargetTexture((__bridge id<MTLTexture>)texture);
    rb::RenderContext::FrameDescriptor frame;
    frame.renderTargetWidth = w, frame.renderTargetHeight = h, frame.loadAction = rive::gpu::LoadAction::clear, frame.clearColor = 0xFFFFFFFF;
    frame.ditherMode = rive::gpu::DitherMode::none;
    r->context->beginFrame(frame);
    rive::RiveRenderer renderer(r->context.get());
    rb::Affine v = rb::Affine::flip(h) * rb::Affine::make(view[0], view[1], view[2], view[3], view[4], view[5]);
    size_t encoded = rb::drawDump(renderer, r->context.get(), r->dump, r->assets, v, w, h, true);
    r->context->flush({ .renderTarget = r->target.get(), .externalCommandBuffer = commandBuffer });
    return encoded;
}

void rv_rive_free(RiveView *r) {
    delete r;
}
__VIEWER_RIVE_VIEW_MM__

w $S/viewer/raviewer.mm <<'__VIEWER_RAVIEWER_MM__'
//  raviewer: an interactive viewer for RSD dumps, & TestFiles SVGs & PDFs (exported on the fly), that switches between
//  renderers on the same view: Rasterizer (rabench's Offscreen copy of RasterizerLayer's encode loop), CoreGraphics
//  (RasterizerCG), Rive, Skia Ganesh & Vello (their bench replays).
//    raviewer <metallib> [file] [--snapshot <dir>]
//  Trackpad: two-finger scrolling pans, pinching zooms & rotating rotates, around the pointer. A mouse drags to pan & its wheel
//  zooms. Keys: 1-5 or tab picks the renderer, f fits (as does a double click or two-finger double tap), c toggles continuous
//  redraw for timing, left & right step through the file's directory, & cmd-O opens a file, as does dropping one on the window.
//  The title shows the renderer's median frame times over its last 30 frames: CPU to submit, submit to GPU done (frames wait
//  for the GPU, as in the benchmark), & Metal GPU time where there is one. CoreGraphics only has CPU time.
//  --snapshot draws the fit view of file with each renderer in turn, writes <dir>/<file>_<renderer>.png, prints each title
//  line, & quits.

#define RABENCH_NO_MAIN 1
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "../ra/rabench.mm"
#import "RasterizerCG.hpp"
#include "viewer.h"

enum { kRasterizer, kCoreGraphics, kRive, kSkia, kVello, kRendererCount };
static NSString *const kNames[kRendererCount] = { @"Rasterizer", @"CoreGraphics", @"Rive", @"Skia Ganesh", @"Vello" };

#pragma mark - RSD import

// An RSD dump (see rabench.mm) as a SceneList, for Rasterizer & CoreGraphics: each scene, & its draws with their paths,
// paints & clips. Gradients & images are shared by their dump index, as the export shares their bitmaps
static bool importDump(const char *path, Ra::SceneList& list) {
    NSData *data = [NSData dataWithContentsOfFile:@(path)];
    const uint8_t *p = (const uint8_t *)data.bytes, *end = p + data.length;
    bool ok = true;
    auto bytes = [&](size_t n) { const uint8_t *s = p; if (p + n > end) ok = false, s = nullptr; else p += n; return s; };
    auto u32 = [&]() { const uint8_t *s = bytes(4); uint32_t v = 0; if (s) memcpy(& v, s, 4); return v; };
    auto f32s = [&](float *v, size_t n) { const uint8_t *s = bytes(4 * n); if (s) memcpy(v, s, 4 * n); else memset(v, 0, 4 * n); };
    const uint8_t *magic = bytes(4);
    if (!magic || memcmp(magic, "RSD1", 4) != 0 || u32() != 1)
        return false;
    float b[6];
    f32s(b, 4);
    std::vector<Ra::SceneRef> scenes(u32());
    std::vector<Ra::Transform> sceneCtms;  std::vector<Ra::Bounds> sceneClips;
    for (size_t i = 0; i < scenes.size() && ok; i++) {
        f32s(b, 6), sceneCtms.emplace_back(b[0], b[1], b[2], b[3], b[4], b[5]);
        f32s(b, 4), sceneClips.emplace_back(b[0], b[1], b[2], b[3]);
    }
    std::vector<Ra::Path> paths(u32());
    for (Ra::Path& path : paths) {
        uint32_t n = u32();
        const uint8_t *types = bytes(n);  bytes((4 - (n & 3)) & 3);
        std::vector<float> pts(2 * n);
        f32s(pts.data(), pts.size());
        if (!ok)
            return false;
        for (size_t i = 0; i < n; ) {
            const float *q = & pts[2 * i];
            switch (types[i]) {
                case Ra::Geometry::kMove: path->moveTo(q[0], q[1]), i += 1; break;
                case Ra::Geometry::kLine: path->lineTo(q[0], q[1]), i += 1; break;
                case Ra::Geometry::kQuadratic: path->quadTo(q[0], q[1], q[2], q[3]), i += 2; break;
                case Ra::Geometry::kCubic: path->cubicTo(q[0], q[1], q[2], q[3], q[4], q[5]), i += 3; break;
                case Ra::Geometry::kClose: path->close(), i += 1; break;
                default: return false;
            }
        }
    }
    std::vector<Ra::Paint> grads(u32());
    for (Ra::Paint& paint : grads) {
        bool radial = u32() == 2;
        f32s(b, 6);
        uint32_t n = u32();
        std::vector<float> locs(n);  std::vector<Ra::Color> colors(n);
        f32s(locs.data(), n);
        const uint8_t *bgra = bytes(4 * size_t(n));
        if (!ok)
            return false;
        memcpy(colors.data(), bgra, 4 * size_t(n));
        paint = Ra::Paint(colors.data(), locs.data(), n, Ra::Transform(b[0], b[1], b[2], b[3], b[4], b[5]), radial);
    }
    std::vector<Ra::Paint> images(u32());
    for (Ra::Paint& paint : images) {
        uint32_t w = u32(), h = u32();
        const uint8_t *bgra = bytes(4 * size_t(w) * h);
        if (!ok)
            return false;
        paint = Ra::Paint((Ra::Color *)bgra, w, h, 4 * size_t(w));
    }
    uint32_t count = u32();
    for (uint32_t i = 0; i < count && ok; i++) {
        uint32_t s = u32(), path = u32();
        float ctm[6], width, clip[4];
        f32s(ctm, 6);
        const uint8_t *pt = bytes(4), *bgra = bytes(4);
        uint32_t paintIndex = u32();
        f32s(& width, 1), f32s(clip, 4);
        uint32_t clipPath = u32();
        if (!ok || s >= scenes.size() || path >= paths.size() || (clipPath != ~0u && clipPath >= paths.size()))
            return false;
        Ra::Paint paint;
        if (pt[0] == Ra::Paint::kLinear || pt[0] == Ra::Paint::kRadial)
            paint = paintIndex < grads.size() ? grads[paintIndex] : Ra::Paint();
        else if (pt[0] == Ra::Paint::kImage)
            paint = paintIndex < images.size() ? images[paintIndex] : Ra::Paint();
        else
            paint = Ra::Paint(Ra::Color(bgra[0], bgra[1], bgra[2], bgra[3]));
        Ra::Bounds bounds(clip[0], clip[1], clip[2], clip[3]);
        scenes[s]->addPath(paths[path], Ra::Transform(ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]), paint, width, pt[1],
                           bounds.isHuge() ? nullptr : & bounds, clipPath == ~0u ? nullptr : & paths[clipPath]);
    }
    for (size_t i = 0; i < scenes.size(); i++)
        list.addScene(scenes[i], sceneCtms[i], sceneClips[i]);
    return ok && list.pathsCount() > 0;
}

#pragma mark - Viewer

// The document & each renderer's state. The view is Rasterizer's y-up device transform, in drawable pixels
struct Viewer {
    std::string metallib, file, dump;       // The open file, & its RSD dump: itself, or an export of it
    Ra::SceneList list;
    Ra::Transform view;  bool fitted = true;
    int renderer = kRasterizer;  bool continuous = false, dirty = true;
    bool loaded[kRendererCount] = {};
    Offscreen offscreen;  size_t offscreenW = 0, offscreenH = 0;
    RenderBuffer buffers[2];  RasterizerRenderer rasterizer;  size_t frame = 0;
    id<MTLDevice> device;  id<MTLCommandQueue> queue;
    RiveView *rive = nullptr;  Skia *skia = nullptr;  Vello *vello = nullptr;
    std::vector<double> cpu, done, gpu;  size_t encoded = 0;
    std::string snapshot;       // --snapshot's directory
};

// A snapshot of a frame: 4 w h bytes, BGRA, or RGBA from Vello, rows from the top
static void writeSnapshot(const std::string& path, const void *pixels, uint32_t w, uint32_t h, bool bgra) {
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo info = bgra ? kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little : kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
    CGContextRef ctx = CGBitmapContextCreate((void *)pixels, w, h, 8, 4 * w, rgb, info);
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:@(path.c_str())], (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, image, nullptr);
    if (!CGImageDestinationFinalize(dst))
        fprintf(stderr, "raviewer: can't write %s\n", path.c_str());
    CFRelease(dst), CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
}

static double median(std::vector<double> v, size_t last) {
    if (v.size() > last)
        v.erase(v.begin(), v.end() - last);
    return median(v);
}

@interface ViewerView : NSView
- (instancetype)initWithFrame:(NSRect)frame metallib:(const char *)metallib;
- (void)load:(NSString *)path;
@end

@implementation ViewerView {
    Viewer _v;
    CAMetalLayer *_metalLayer, *_velloLayer;  CALayer *_cgLayer;
    CADisplayLink *_displayLink;
}

- (instancetype)initWithFrame:(NSRect)frame metallib:(const char *)metallib {
    self = [super initWithFrame:frame];
    _v.metallib = metallib;
    _v.device = MTLCreateSystemDefaultDevice(), _v.queue = [_v.device newCommandQueue];
    _v.buffers[0].device = _v.buffers[1].device = _v.device;
    self.wantsLayer = YES;
    self.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    _metalLayer = [CAMetalLayer layer], _velloLayer = [CAMetalLayer layer], _cgLayer = [CALayer layer];
    _metalLayer.device = _v.device, _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm, _metalLayer.framebufferOnly = NO;
    _metalLayer.colorspace = rgb, _velloLayer.colorspace = rgb;
    CGColorSpaceRelease(rgb);
    for (CALayer *layer in @[ _metalLayer, _velloLayer, _cgLayer ]) {
        layer.frame = self.bounds, layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable, layer.opaque = YES;
        layer.actions = @{ @"contents": [NSNull null], @"bounds": [NSNull null], @"position": [NSNull null], @"hidden": [NSNull null] };
        [self.layer addSublayer:layer];
    }
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    [self showLayer];
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [_displayLink invalidate], _displayLink = nil;
    if (self.window) {
        [self viewDidChangeBackingProperties];
        _displayLink = [self displayLinkWithTarget:self selector:@selector(tick:)];
        [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    CGFloat scale = self.window.backingScaleFactor ?: 1;
    for (CALayer *layer in @[ _metalLayer, _velloLayer, _cgLayer ])
        layer.contentsScale = scale;
    [self resized];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self resized];
}

- (void)resized {
    CGSize size = [self convertSizeToBacking:self.bounds.size];
    _metalLayer.drawableSize = size;
    if (_v.fitted)
        [self fit];
    _v.dirty = true;
}

#pragma mark - Document

- (void)load:(NSString *)path {
    path = path.stringByStandardizingPath;
    std::string file = path.fileSystemRepresentation, dump = file;
    NSString *ext = path.pathExtension.lowercaseString;
    Ra::SceneList list;
    bool ok = false;
    self.window.title = [NSString stringWithFormat:@"Loading %@…", path.lastPathComponent];
    [self.window displayIfNeeded];
    if ([ext isEqualToString:@"rsd"])
        ok = importDump(file.c_str(), list);
    else if ([ext isEqualToString:@"svg"] || [ext isEqualToString:@"pdf"]) {
        NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:[path.lastPathComponent.stringByDeletingPathExtension stringByAppendingPathExtension:@"rsd"]];
        dump = tmp.fileSystemRepresentation;
        ok = loadList(file.c_str(), list) && exportList(list, dump.c_str()) == 0;
    }
    if (!ok) {
        NSBeep();
        self.window.title = [NSString stringWithFormat:@"Can't load %@", path.lastPathComponent];
        return;
    }
    _v.file = file, _v.dump = dump, _v.list = list;
    for (bool& loaded : _v.loaded)
        loaded = false;
    _v.loaded[kRasterizer] = _v.loaded[kCoreGraphics] = true;
    [NSDocumentController.sharedDocumentController noteNewRecentDocumentURL:[NSURL fileURLWithPath:path]];
    [self resetStats];
    [self fit];
}

// The renderer's copy of the dump, which Rive, Skia & Vello each load when first shown
- (void)prepareRenderer {
    int r = _v.renderer;
    if (_v.loaded[r] || _v.dump.empty())
        return;
    self.window.title = [NSString stringWithFormat:@"Loading %s into %@…", strrchr(_v.file.c_str(), '/') + 1, kNames[r]];
    [self.window displayIfNeeded];
    if (r == kRive) {
        if (!_v.rive)
            _v.rive = rv_rive_new((__bridge void *)_v.device, (__bridge void *)_v.queue);
        rv_rive_load(_v.rive, _v.dump.c_str());
    } else if (r == kSkia) {
        if (!_v.skia)
            _v.skia = rv_skia_new((__bridge void *)_v.device, (__bridge void *)_v.queue);
        if (_v.skia)
            rv_skia_load(_v.skia, _v.dump.c_str());
    } else if (r == kVello) {
        if (!_v.vello)
            _v.vello = rv_vello_new((__bridge void *)_velloLayer);
        if (_v.vello)
            rv_vello_load(_v.vello, _v.dump.c_str());
    }
    _v.loaded[r] = true;
}

- (void)step:(int)direction {
    NSString *path = @(_v.file.c_str()), *dir = path.stringByDeletingLastPathComponent;
    NSMutableArray<NSString *> *files = [NSMutableArray array];
    for (NSString *name in [[NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil] sortedArrayUsingSelector:@selector(localizedStandardCompare:)])
        if ([@[ @"rsd", @"svg", @"pdf" ] containsObject:name.pathExtension.lowercaseString])
            [files addObject:[dir stringByAppendingPathComponent:name]];
    if (files.count == 0)
        return;
    NSInteger i = [files indexOfObject:path];
    i = i == NSNotFound ? 0 : (i + direction + files.count) % files.count;
    [self load:files[i]];
}

- (void)openDocument:(id)sender {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    UTType *rsd = [UTType typeWithFilenameExtension:@"rsd"];
    panel.allowedContentTypes = rsd ? @[ rsd, UTTypeSVG, UTTypePDF ] : @[ UTTypeSVG, UTTypePDF ];
    if ([panel runModal] == NSModalResponseOK)
        [self load:panel.URL.path];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSURL *url = [sender.draggingPasteboard readObjectsForClasses:@[ NSURL.class ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }].firstObject;
    if (url)
        [self load:url.path];
    return url != nil;
}

#pragma mark - View

- (void)fit {
    CGSize size = [self convertSizeToBacking:self.bounds.size];
    if (_v.list.pathsCount() && size.width > 0 && size.height > 0)
        _v.view = frameView(_v.list.bounds(), size.width, size.height, 0, 1, 1.f);
    _v.fitted = true, _v.dirty = true;
}

// Applies t after the view, in device pixels
- (void)apply:(Ra::Transform)t {
    _v.view = _v.view.concat(t), _v.fitted = false, _v.dirty = true;
}

// An event's location in device pixels, which are y-up, as the view isn't flipped
- (NSPoint)devicePoint:(NSEvent *)event {
    return [self convertPointToBacking:[self convertPoint:event.locationInWindow fromView:nil]];
}

- (void)zoom:(double)s at:(NSPoint)p {
    [self apply:Ra::Transform(s, 0.f, 0.f, s, p.x - s * p.x, p.y - s * p.y)];
}

- (void)scrollWheel:(NSEvent *)event {
    CGFloat scale = self.window.backingScaleFactor;
    if (event.hasPreciseScrollingDeltas)
        [self apply:Ra::Transform(1.f, 0.f, 0.f, 1.f, event.scrollingDeltaX * scale, -event.scrollingDeltaY * scale)];
    else
        [self zoom:pow(1.1, event.scrollingDeltaY) at:[self devicePoint:event]];
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self zoom:1.0 + event.magnification at:[self devicePoint:event]];
}

- (void)rotateWithEvent:(NSEvent *)event {
    NSPoint p = [self devicePoint:event];
    double a = event.rotation * M_PI / 180.0, c = cos(a), s = sin(a);
    [self apply:Ra::Transform(c, s, -s, c, p.x - (c * p.x - s * p.y), p.y - (s * p.x + c * p.y))];
}

- (void)smartMagnifyWithEvent:(NSEvent *)event {
    [self fit];
}

- (void)mouseDown:(NSEvent *)event {
    if (event.clickCount == 2)
        [self fit];
}

- (void)mouseDragged:(NSEvent *)event {
    CGFloat scale = self.window.backingScaleFactor;
    [self apply:Ra::Transform(1.f, 0.f, 0.f, 1.f, event.deltaX * scale, -event.deltaY * scale)];
}

- (void)keyDown:(NSEvent *)event {
    unichar key = event.charactersIgnoringModifiers.length ? [event.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if (key >= '1' && key < '1' + kRendererCount)
        [self select:key - '1'];
    else if (key == '\t')
        [self select:(_v.renderer + 1) % kRendererCount];
    else if (key == NSBackTabCharacter)
        [self select:(_v.renderer + kRendererCount - 1) % kRendererCount];
    else if (key == 'f')
        [self fit];
    else if (key == 'c')
        _v.continuous = !_v.continuous, [self resetStats], _v.dirty = true;
    else if (key == NSLeftArrowFunctionKey)
        [self step:-1];
    else if (key == NSRightArrowFunctionKey)
        [self step:1];
    else
        [super keyDown:event];
}

- (void)select:(int)renderer {
    _v.renderer = renderer, _v.dirty = true;
    [self resetStats];
    [self showLayer];
}

- (void)showLayer {
    _metalLayer.hidden = _v.renderer == kCoreGraphics || _v.renderer == kVello;
    _cgLayer.hidden = _v.renderer != kCoreGraphics;
    _velloLayer.hidden = _v.renderer != kVello;
}

- (void)resetStats {
    _v.cpu.clear(), _v.done.clear(), _v.gpu.clear(), _v.dirty = true;
}

#pragma mark - Drawing

- (void)tick:(CADisplayLink *)link {
    if ((_v.dirty || _v.continuous) && _v.list.pathsCount())
        [self draw];
}

// For --snapshot, a blit of the drawable into a shared buffer, before the command buffer presents it
- (id<MTLBuffer>)capture:(id<MTLTexture>)texture in:(id<MTLCommandBuffer>)cb {
    if (_v.snapshot.empty())
        return nil;
    NSUInteger w = texture.width, h = texture.height;
    id<MTLBuffer> buffer = [_v.device newBufferWithLength:4 * w * h options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:buffer destinationOffset:0 destinationBytesPerRow:4 * w destinationBytesPerImage:4 * w * h];
    [blit endEncoding];
    return buffer;
}

- (std::string)snapshotPath {
    NSString *name = [NSString stringWithFormat:@"%@_%@.png", @(_v.file.c_str()).lastPathComponent.stringByDeletingPathExtension, [kNames[_v.renderer] stringByReplacingOccurrencesOfString:@" " withString:@"_"]];
    return [@(_v.snapshot.c_str()) stringByAppendingPathComponent:name].fileSystemRepresentation;
}

- (void)draw {
    CGSize size = [self convertSizeToBacking:self.bounds.size];
    uint32_t w = uint32_t(size.width), h = uint32_t(size.height);
    if (w == 0 || h == 0)
        return;
    _v.dirty = false;
    [self prepareRenderer];
    const Ra::Transform& m = _v.view;
    double view[6] = { m.a, m.b, m.c, m.d, m.tx, m.ty }, t0 = now(), t1 = t0, t2 = t0, gpu = -1, times[2];
    size_t encoded = 0;
    id<MTLBuffer> captured = nil;
    switch (_v.renderer) {
        case kRasterizer: {
            if (_v.offscreenW != w || _v.offscreenH != h) {
                _v.offscreen = Offscreen();
                if (!_v.offscreen.init(_v.metallib.c_str(), w, h))
                    return;
                _v.offscreenW = w, _v.offscreenH = h;
            }
            id<CAMetalDrawable> drawable = _metalLayer.nextDrawable;
            if (!drawable)
                return;
            t0 = now();
            RenderBuffer *rb = & _v.buffers[_v.frame++ & 1];
            _v.list.ctm = m;
            _v.rasterizer.renderList(_v.list, 1.f, w, h, rb);
            _v.offscreen.target = drawable.texture;
            id<MTLCommandBuffer> cb = _v.offscreen.encode(rb);
            captured = [self capture:drawable.texture in:cb];
            [cb presentDrawable:drawable], [cb commit];
            t1 = now(), [cb waitUntilCompleted], t2 = now();
            gpu = cb.GPUEndTime - cb.GPUStartTime, encoded = _v.list.pathsCount();
            break;
        }
        case kCoreGraphics: {
            CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, w * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
            _v.list.ctm = m;
            RasterizerCG::renderListWithClear(_v.list, 1.f, w, h, ctx);
            CGImageRef image = CGBitmapContextCreateImage(ctx);
            t1 = t2 = now();
            _cgLayer.contents = (__bridge id)image;
            if (!_v.snapshot.empty())
                writeSnapshot([self snapshotPath], CGBitmapContextGetData(ctx), w, h, true);
            CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
            encoded = _v.list.pathsCount();
            break;
        }
        case kRive:
        case kSkia: {
            if ((_v.renderer == kRive && !_v.rive) || (_v.renderer == kSkia && !_v.skia))
                return;
            id<CAMetalDrawable> drawable = _metalLayer.nextDrawable;
            if (!drawable)
                return;
            t0 = now();
            id<MTLCommandBuffer> cb = [_v.queue commandBuffer];
            if (_v.renderer == kRive)
                encoded = rv_rive_draw(_v.rive, (__bridge void *)drawable.texture, w, h, view, (__bridge void *)cb);
            else
                encoded = rv_skia_draw(_v.skia, (__bridge void *)drawable.texture, w, h, view);
            captured = [self capture:drawable.texture in:cb];
            [cb presentDrawable:drawable], [cb commit];
            t1 = now(), [cb waitUntilCompleted], t2 = now();
            if (_v.renderer == kRive)
                gpu = cb.GPUEndTime - cb.GPUStartTime;
            break;
        }
        case kVello: {
            if (!_v.vello)
                return;
            encoded = rv_vello_draw(_v.vello, w, h, view, times);
            if (encoded == 0 && _v.list.pathsCount()) {     // Its surface had no texture: try again next frame
                _v.dirty = true;
                return;
            }
            t1 = t0 + times[0] * 1e-3, t2 = t1 + times[1] * 1e-3;
            if (!_v.snapshot.empty()) {
                std::vector<uint8_t> rgba(4 * size_t(w) * h);
                if (rv_vello_read(_v.vello, rgba.data()))
                    writeSnapshot([self snapshotPath], rgba.data(), w, h, false);
            }
            break;
        }
    }
    if (captured)
        writeSnapshot([self snapshotPath], captured.contents, w, h, true);
    _v.cpu.push_back(t1 - t0), _v.done.push_back(t2 - t1), _v.encoded = encoded;
    if (gpu >= 0)
        _v.gpu.push_back(gpu);
    [self updateTitle];
}

// --snapshot: each renderer's fit view, after 3 warm-up frames, then quits. Each renderer's layer is shown first, as Vello's
// surface has no drawables for a hidden layer
- (void)snapshotInto:(NSString *)dir {
    _v.snapshot = dir.fileSystemRepresentation;
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    for (int r = 0; r < kRendererCount; r++) {
        [self select:r], [self fit], [CATransaction flush];
        std::string snapshot = _v.snapshot;
        _v.snapshot.clear();
        for (int i = 0; i < 3; i++)
            [self draw];
        [self resetStats], _v.snapshot = snapshot, [self draw];
        printf("%s\n", self.window.title.UTF8String), fflush(stdout);
    }
    [NSApp terminate:nil];
}

- (void)updateTitle {
    auto ms = [](const std::vector<double>& v) { return median(v, 30) * 1e3; };
    Ra::Bounds b = _v.list.bounds();
    CGSize size = [self convertSizeToBacking:self.bounds.size];
    Ra::Transform fit = frameView(b, size.width, size.height, 0, 1, 1.f);
    double zoom = sqrt(fabs(_v.view.a * _v.view.d - _v.view.b * _v.view.c) / fabs(fit.a * fit.d - fit.b * fit.c));
    NSMutableString *title = [NSMutableString stringWithFormat:@"%s — %@ — CPU %.2f ms", strrchr(_v.file.c_str(), '/') + 1, kNames[_v.renderer], ms(_v.cpu)];
    if (_v.renderer != kCoreGraphics)     // CoreGraphics draws on the CPU
        [title appendFormat:@", GPU done %.2f ms", ms(_v.done)];
    if (_v.gpu.size())
        [title appendFormat:@" (Metal %.2f ms)", ms(_v.gpu)];
    [title appendFormat:@" — %zu draws, %.2fx%@", _v.encoded, zoom, _v.continuous ? @" — continuous" : @""];
    self.window.title = title;
}

@end

#pragma mark - App

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic) ViewerView *viewer;
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
- (BOOL)application:(NSApplication *)app openFile:(NSString *)path {
    [self.viewer load:path];
    return YES;
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 2)
            return fprintf(stderr, "usage: raviewer <metallib> [file.rsd|svg|pdf]\n"), 2;
        NSApplication *app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        NSMenu *bar = [NSMenu new], *appMenu = [NSMenu new], *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        [appMenu addItemWithTitle:@"Quit raviewer" action:@selector(terminate:) keyEquivalent:@"q"];
        [fileMenu addItemWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
        [bar addItemWithTitle:@"" action:nil keyEquivalent:@""].submenu = appMenu;
        [bar addItemWithTitle:@"File" action:nil keyEquivalent:@""].submenu = fileMenu;
        app.mainMenu = bar;

        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1200, 900)
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                         backing:NSBackingStoreBuffered defer:NO];
        ViewerView *viewer = [[ViewerView alloc] initWithFrame:window.contentView.bounds metallib:argv[1]];
        window.contentView = viewer;
        window.releasedWhenClosed = NO;
        [window center], [window makeKeyAndOrderFront:nil], [window makeFirstResponder:viewer];
        AppDelegate *delegate = [AppDelegate new];
        delegate.viewer = viewer, app.delegate = delegate;
        NSString *snapshot = nil;
        for (int i = 2; i < argc; i++)
            if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc)
                snapshot = @(argv[++i]);
            else
                [viewer load:@(argv[i])];
        if (snapshot)
            dispatch_async(dispatch_get_main_queue(), ^{ [viewer snapshotInto:snapshot]; });
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
__VIEWER_RAVIEWER_MM__

w $S/summarize.py <<'__SUMMARIZE_PY__'
#!/usr/bin/env python3
"""Summarises a run.sh results dir. For each file, mode & library: the best (min) across rounds of each run's median.
Writes markdown tables to stdout (wall, CPU, submit-to-done, then quality vs Rasterizer) & summary.csv."""
import json, sys, os, statistics
from collections import defaultdict

out = sys.argv[1]
runs = [json.loads(l) for l in open(os.path.join(out, 'runs.jsonl')) if l.strip()]
quality = {}
qpath = os.path.join(out, 'quality.jsonl')
if os.path.exists(qpath):
    for l in open(qpath):
        if l.strip():
            q = json.loads(l)
            quality[(q['file'], q.get('lib', 'vello'))] = q['cmp']

def stem(f):
    return os.path.splitext(f)[0] if f.endswith(('.svg', '.pdf')) else f

METRICS = ('cpu_med', 'gpu_med', 'submit_to_done_med', 'wall_med', 'encode_med')
best = defaultdict(dict)       # (file, mode) -> lib -> metric -> best median
walls = defaultdict(list)      # (file, mode, lib) -> each round's wall median
order = []
for r in runs:
    key, lib = (stem(r['file']), r['mode']), r['lib']
    if lib not in order:
        order.append(lib)
    cur = best[key].setdefault(lib, {'draws': r['draws']})
    for m in METRICS:
        if m in r:
            cur[m] = min(cur.get(m, float('inf')), r[m])
    walls[(key, lib)].append(r['wall_med'])

libs = ['rasterizer'] + [l for l in order if l != 'rasterizer']
others = libs[1:]
files = sorted({k[0] for k in best}, key=lambda f: best[(f, 'fit')]['rasterizer']['draws'])
short = lambda f: f[:26]
fmt = lambda v: f'{v:.2f}' if v is not None else '–'

def table(title, metric, mode):
    print(f'\n#### {title}\n')
    print('| File | Draws | ' + ' | '.join(libs) + ' |')
    print('|---|---:|' + '---:|' * len(libs))
    for f in files:
        b = best[(f, mode)]
        print(f"| {short(f)} | {b['rasterizer']['draws']:,} | " + ' | '.join(fmt(b.get(l, {}).get(metric)) for l in libs) + ' |')

csv = []
for mode in ('fit', 'sweep'):
    print(f"\n### {mode} ({'static fit view' if mode == 'fit' else '1x to 8x zoom sweep'}), ms per frame\n")
    table('Wall (frame start to GPU done)', 'wall_med', mode)
    print()
    print('Geometric mean wall ratio vs Rasterizer: ' + ', '.join(
        f"{l} {statistics.geometric_mean([best[(f, mode)][l]['wall_med'] / best[(f, mode)]['rasterizer']['wall_med'] for f in files if l in best[(f, mode)]]):.2f}x"
        for l in others))
    table('CPU (frame start to submit)', 'cpu_med', mode)
    table('Submit to GPU done', 'submit_to_done_med', mode)
    for f in files:
        for l in libs:
            m = best[(f, mode)].get(l)
            if m:
                q = quality.get((f, l), {})
                csv.append([mode, f, l, m['draws']] + [m.get(k) for k in METRICS] + [q.get('psnr_db'), q.get('pct_px_gt32')])

if quality:
    qlibs = [l for l in others if any((f, l) in quality for f in files)]
    print('\n### Quality of the fit frame vs Rasterizer: PSNR dB (% of pixels off by >32)\n')
    print('| File | ' + ' | '.join(qlibs) + ' |')
    print('|---|' + '---:|' * len(qlibs))
    for f in files:
        cells = []
        for l in qlibs:
            q = quality.get((f, l))
            cells.append(f"{q['psnr_db']:.1f} ({q['pct_px_gt32']:.2f}%)" if q else '–')
        print(f'| {short(f)} | ' + ' | '.join(cells) + ' |')

spread = [((max(v) - min(v)) / min(v), k) for k, v in walls.items() if len(v) > 1 and min(v) > 0]
if spread:
    worst = max(spread)
    print(f"\nLargest round-to-round wall spread: {worst[0] * 100:.0f}% ({worst[1][0][0]}, {worst[1][0][1]}, {worst[1][1]})")

with open(os.path.join(out, 'summary.csv'), 'w') as fcsv:
    fcsv.write('mode,file,lib,draws,' + ','.join(METRICS) + ',psnr_db,pct_px_gt32\n')
    for r in csv:
        fcsv.write(','.join('' if x is None else str(x) for x in r) + '\n')
__SUMMARIZE_PY__

command -v python3 > /dev/null || die "python3 is needed"
command -v cargo > /dev/null || die "Rust is needed: https://rustup.rs"
if command -v rustup > /dev/null; then
  rustup toolchain list | grep -q "^$RUST" || { step "Installing Rust $RUST"; rustup toolchain install $RUST --profile minimal --no-self-update }
fi

VE=$W/vendor/vello_encoding
if [[ ! -f $VE/.patched ]]; then
  step "Fetching & patching vello_encoding $VE_VERSION"
  rm -rf $VE && mkdir -p $W/vendor
  curl -fsSL -o $W/vendor/ve.crate https://static.crates.io/crates/vello_encoding/vello_encoding-$VE_VERSION.crate
  [[ $(shasum -a 256 $W/vendor/ve.crate | cut -d' ' -f1) == $VE_SHA256 ]] || die "vello_encoding checksum mismatch"
  tar -xzf $W/vendor/ve.crate -C $W/vendor && mv $W/vendor/vello_encoding-$VE_VERSION $VE && rm $W/vendor/ve.crate
  python3 - $VE/src/config.rs <<'PATCH'
import sys
p = sys.argv[1]; s = open(p).read()
for name, bits in [('bin_data', 18), ('tiles', 21), ('lines', 21), ('seg_counts', 21), ('segments', 21), ('blend_spill', 20), ('ptcl', 23)]:
    old = f'let {name} = BufferSize::new(1 << {bits});'
    assert old in s, old
    s = s.replace(old, f'let {name} = BufferSize::new(1 << ({bits} + BUFFER_SHIFT));')
s = s.replace('        let bin_data =', '''        // bench.sh: scaled by 1 << BUFFER_SHIFT (build-time VELLO_BUFFER_SHIFT, default 4 = 16x)
        const BUFFER_SHIFT: u32 = match option_env!("VELLO_BUFFER_SHIFT") {
            Some(s) => s.as_bytes()[0] as u32 - b'0' as u32,
            None => 4,
        };
        let bin_data =''', 1)
open(p, 'w').write(s)
PATCH
  touch $VE/.patched
fi

B=$W/bin; mkdir -p $B $W/obj
# Rasterizer's features, from its sources, which rabench & raviewer are built for
CLIP=0; grep -q kClipMask $INC/Rasterizer.hpp && CLIP=1    # Rasterizer's anti-aliased clip mask, else its stencil clipping
CLEAR=0; grep -q kClipClear $INC/Rasterizer.hpp && CLEAR=1    # Its clear cells, else a clear pass
INPASS=0; grep -q '\[\[color(1)\]\]' $INC/Shaders.metal && INPASS=1    # Its in-pass mask, read with framebuffer fetch, else mask passes
BLEND=0; grep -q kBlendNormal $INC/Rasterizer.h && BLEND=1    # Its blend modes, blended in the shader
IMAGES=0; grep -q 'function_constant(kImages)' $INC/Shaders.metal && IMAGES=1    # Its images, from an argument buffer
RULE=0; grep -q kClipEvenOdd $INC/Rasterizer.hpp && RULE=1    # Its draws' clip fill rules, else even-odd clips
PATHS=0; grep -q 'struct PathData' $INC/Rasterizer.hpp && PATHS=1    # Its per-path data as one array of structs, else an array each
BATCHES=0; grep -q kBlendInstances $INC/Rasterizer.hpp && BATCHES=1    # Its batches of draws with blend modes, else a frame's blend mode
HEIGHTS=0; grep -q 'size_t idx, height' $INC/Rasterizer.hpp && HEIGHTS=1    # Its edges passes' heights, from the accumulation texture's top
LIMIT=0; grep -q 'std::vector<Ra::Context> contexts' $INC/RasterizerRenderer.hpp && LIMIT=1    # Its contexts' count capped by --contexts
QUADS=0; grep -q 'iid = vi >> 2' $INC/Shaders.metal && QUADS=1    # Its quads as indexed triangles, else 4 vertex instances
RAFLAGS=(-DRA_CLIP_MASK=$CLIP -DRA_CLIP_CLEAR_CELLS=$CLEAR -DRA_CLIP_IN_PASS=$INPASS -DRA_BLEND=$BLEND -DRA_IMAGES=$IMAGES -DRA_CLIP_RULE=$RULE -DRA_PATH_DATA=$PATHS
         -DRA_BLEND_BATCHES=$BATCHES -DRA_EDGES_HEIGHT=$HEIGHTS -DRA_CONTEXT_LIMIT=$LIMIT -DRA_QUAD_INDICES=$QUADS)
if [[ ! -x $B/rabench || $S/ra/rabench.mm -nt $B/rabench || -n $(find $INC $RA/Rasterizer/Demo/RasterizerPDF.hpp -newer $B/rabench 2>/dev/null) || $(cat $W/obj/rev 2>/dev/null) != $RA ]]; then
  step "Building rabench"
  clang -O3 -c $RA/Package/Sources/RasterizerCpp/xxhash.c -I$INC -o $W/obj/xxhash.o
  clang++ -O3 -std=c++17 -c -x objective-c++ $RA/Package/Sources/RasterizerCpp/nanosvg.cc -I$INC -o $W/obj/nanosvg.o
  clang++ -O3 -std=c++17 -fobjc-arc $RAFLAGS -x objective-c++ $S/ra/rabench.mm -x none $W/obj/xxhash.o $W/obj/nanosvg.o \
    -I$INC -I$RA/Rasterizer/Demo ${=${PDFIUM:+-I$PDFIUM/Headers -L$PDFIUM -lpdfium -Wl,-rpath,$PDFIUM}} \
    -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices \
    -Wno-deprecated-declarations -o $B/rabench
  [[ -z $PDFIUM ]] || install_name_tool -change ./libpdfium.dylib @rpath/libpdfium.dylib $B/rabench    # pdfium's install name is ./libpdfium.dylib
  print $RA > $W/obj/rev
fi
cargo_build() {    # crate, target dir name, binary, extra cargo args
  step "Building $1${4:+ ($4)}"
  (cd $S/$1 && cargo build --release --quiet --target-dir $W/target/$2 ${=4:-})
  ln -sf $W/target/$2/release/$3 $B/$2
}
cargo_build rsd rsdcompare rsdcompare
[[ ,$CONFIGS, == *,vello* ]] && cargo_build vello vello vello_bench
[[ ,$CONFIGS, == *,skia_ganesh* ]] && cargo_build skia skia_ganesh skia_bench

# Rive: rive-runtime's renderer & the core sources it needs, sparse-cloned at RIVE_TAG, its shaders built by its own Makefile
# (which runs its minifier, with ply, & xcrun metal), its sources by clang with its own release flags, then rivebench
RV=$W/vendor/rive-runtime PLY=$W/vendor/ply RL=$W/rive
fetch_git() {    # repo url, dir, tag, sha, sparse dirs..
  local url=$1 dir=$2 tag=$3 sha=$4; shift 4
  [[ $(git -C $dir rev-parse HEAD 2>/dev/null) == $sha ]] && return
  step "Fetching ${url:t:r} $tag"
  rm -rf $dir && mkdir -p ${dir:h}
  git -c advice.detachedHead=false clone -q --depth 1 --branch $tag ${*:+--filter=blob:none} ${*:+--sparse} $url $dir
  (( $# )) && git -C $dir sparse-checkout set "$@"
  [[ $(git -C $dir rev-parse HEAD) == $sha ]] || die "${url:t:r} $tag isn't $sha"
}
build_rive() {
  fetch_git https://github.com/rive-app/rive-runtime.git $RV $RIVE_TAG $RIVE_SHA renderer/src renderer/include include src decoders/include
  fetch_git https://github.com/dabeaz/ply.git $PLY $PLY_TAG $PLY_SHA
  local G=$RL/include/generated/shaders O=$RL/obj
  local -a flags=(-O3 -std=c++17 -DNDEBUG -ffp-contract=on -fassociative-math -w -I$RV/renderer/include -I$RV/renderer/src -I$RV/include -I$RV/decoders/include -I$RL/include)
  if [[ $(cat $RL/rev 2>/dev/null) != $RIVE_SHA ]]; then
    step "Building Rive $RIVE_TAG"
    rm -rf $RL && mkdir -p $G $O
    make -C $RV/renderer/src/shaders -j$(sysctl -n hw.ncpu) OUT=$G FLAGS="-p $PLY" rive_pls_macosx_metallib > $RL/shaders.log 2>&1 \
      || die "can't build Rive's shaders, which need the Metal toolchain (see $RL/shaders.log)"
    local -a srcs=($RV/renderer/src/*.cpp $RV/renderer/src/ore/ore_binding_map.cpp $RV/renderer/src/ore/ore_bind_group_layout.cpp
                   $RV/renderer/src/metal/*.mm $RV/src/math/*.cpp $RV/src/renderer.cpp $RV/src/shapes/paint/color.cpp $RV/src/shapes/paint/paint_outset.cpp)
    local f; local -a objc; integer n=$(sysctl -n hw.ncpu)
    for f in $srcs; do
      objc=(); [[ $f == *.mm ]] && objc=(-fobjc-arc -x objective-c++)
      clang++ $flags $objc -c $f -o $O/${f:h:t}_${f:t:r}.o 2>> $RL/build.log &
      while (( ${#jobstates} >= n )); do sleep 0.05; done
    done
    wait
    for f in $srcs; do [[ -f $O/${f:h:t}_${f:t:r}.o ]] || die "can't compile $f (see $RL/build.log)"; done
    libtool -static -o $RL/librive.a $O/*.o
    print $RIVE_SHA > $RL/rev
  fi
  if [[ ! -x $B/rive || $S/rive/rivebench.mm -nt $B/rive || $RL/librive.a -nt $B/rive ]]; then
    step "Building rivebench"
    clang++ $flags -fobjc-arc -x objective-c++ $S/rive/rivebench.mm -x none $RL/librive.a -Wl,-dead_strip \
      -framework Foundation -framework Metal -framework CoreGraphics -framework ImageIO -framework CoreServices -Wno-deprecated-declarations -o $B/rive
  fi
}
[[ ,$CONFIGS, == *,rive,* ]] && build_rive

# raviewer: raviewer.mm, with rabench's loaders & renderer, built for Rasterizer's features, rive_view.mm, with rivebench's replay, &
# the raviewer Rust library's Skia & Vello, with skia_bench's & vello_bench's
build_viewer() {
  step "Building raviewer"
  (cd $S/viewer && cargo build --release --quiet --target-dir $W/target/viewer)
  local lib=$W/target/viewer/release/libraviewer.a f
  for f in $S/viewer/raviewer.mm $S/viewer/rive_view.mm $S/viewer/viewer.h $S/ra/rabench.mm $S/rive/rivebench.mm $B/rabench $RL/librive.a $lib; do
    [[ -x $B/raviewer && ! $f -nt $B/raviewer ]] && continue
    clang++ -O3 -std=c++17 -fobjc-arc $RAFLAGS -x objective-c++ -c $S/viewer/raviewer.mm -I$INC -I$RA/Rasterizer/Demo ${=${PDFIUM:+-I$PDFIUM/Headers}} \
      -Wno-deprecated-declarations -o $W/obj/raviewer.o
    clang++ -O3 -std=c++17 -fobjc-arc -w -x objective-c++ -c $S/viewer/rive_view.mm -I$RV/renderer/include -I$RV/include -o $W/obj/rive_view.o
    clang++ $W/obj/raviewer.o $W/obj/rive_view.o $W/obj/xxhash.o $W/obj/nanosvg.o $RL/librive.a $lib -Wl,-dead_strip \
      ${=${PDFIUM:+-L$PDFIUM -lpdfium -Wl,-rpath,$PDFIUM}} -framework Foundation -framework AppKit -framework Metal -framework QuartzCore \
      -framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices -framework UniformTypeIdentifiers -o $B/raviewer
    [[ -z $PDFIUM ]] || install_name_tool -change ./libpdfium.dylib @rpath/libpdfium.dylib $B/raviewer
    break
  done
}
(( VIEWER )) && build_viewer

MLSRC="--metallib"
if [[ -z $ML ]]; then
  ML=$W/metallib/$SHA/default.metallib
  if [[ -f $ML && $ML -nt $INC/Shaders.metal ]]; then
    MLSRC="compiled from $SHA (cached)"
  else
    mkdir -p ${ML:h}
    if xcrun -sdk macosx metal -c $INC/Shaders.metal -I$INC -o ${ML:h}/Shaders.air 2> ${ML:h}/metal.log \
        && xcrun -sdk macosx metallib ${ML:h}/Shaders.air -o $ML 2>> ${ML:h}/metal.log; then
      MLSRC="compiled from $SHA"
    else
      rm -f $ML
      ML=$(ls -t ~/Library/Developer/Xcode/DerivedData/Rasterizer-*/Build/Products/Release/RasterizerSwift_RasterizerObjC.bundle/Contents/Resources/default.metallib 2>/dev/null | head -1) || true
      [[ -f $ML ]] || die "can't compile Shaders.metal (see ${ML:h}/metal.log): install the Metal toolchain, or pass --metallib"
      MLSRC="DerivedData Release build, which may not match $SHA"
      print -u2 "bench.sh: no Metal toolchain, so using $ML"
    fi
  fi
fi
if (( VIEWER && !BUILD_ONLY )); then
  step "Opening raviewer"
  exec $B/raviewer $ML ${VFILE:-$RA/TestFiles/tiger.svg}
fi
(( BUILD_ONLY )) && { step "Built into $B"; exit 0 }

D=$W/dumps/$SHA; mkdir -p $D
step "Exporting TestFiles"
for f in $RA/TestFiles/*; do
  n=${${f:t}%.*}
  [[ $D/$n.rsd -nt $f && $D/$n.rsd -nt $B/rabench ]] || $B/rabench export "$f" $D/$n.rsd > /dev/null 2>> $D/export.log
done

# Each config: name, binary & extra args. rasterizer takes the source file, the replays its dump
typeset -A BIN ARGS
BIN=(vello vello vello_retained vello skia_ganesh skia_ganesh rive rive)
ARGS=(vello "" vello_retained --retained skia_ganesh "" rive "")
REPLAYS=(${${(s:,:)CONFIGS}:#rasterizer})
run() {    # config, file, png | frames zoom
  local c=$1 f=$2 n=${${2:t}%.*}; shift 2
  if [[ $c == rasterizer ]]; then $B/rabench bench "$f" $ML --size $SIZE ${=CONTEXTS:+--contexts $CONTEXTS} "$@"
  else $B/$BIN[$c] $D/$n.rsd --size $SIZE ${=ARGS[$c]} "$@"; fi
}

OUT=$W/results/$(date +%Y%m%d-%H%M%S)-$SHA-$SIZE${CONTEXTS:+-c$CONTEXTS}; mkdir -p $OUT/png
{
  print "date: $(date -u +%FT%TZ)"
  print "rasterizer: $SHA ($RA)${CONTEXTS:+, contexts capped at $CONTEXTS}"
  print "metallib: $ML ($MLSRC)"
  print "machine: $(sysctl -n hw.model), $(sysctl -n machdep.cpu.brand_string), $(( $(sysctl -n hw.memsize) / 1073741824 )) GB"
  print "macOS: $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
  print "clang: $(clang --version | head -1)"
  print "rustc: $(cd $S/rsd && rustc --version)"
  print "crates: $(cat $S/*/Cargo.lock | awk '/^name = /{n=$3} /^version = /{if (n ~ /^"(vello|vello_encoding|wgpu|skia-safe|skia-bindings)"$/) print n "@" $3}' | tr -d '"' | sort -u | tr '\n' ' ')"
  if [[ ,$CONFIGS, == *,rive,* ]]; then print "rive: rive-runtime $RIVE_TAG ($RIVE_SHA)"; fi
  print "args: rounds $ROUNDS, size $SIZE, frames $FRAMES, configs $CONFIGS"
} > $OUT/info.txt
cat $OUT/info.txt

step "Quality pass"
for f in $RA/TestFiles/*; do
  n=${${f:t}%.*}
  run rasterizer "$f" --frames 1 --zoom 1 --png $OUT/png/${n}_rasterizer.png > /dev/null
  for c in $REPLAYS; do
    run $c "$f" --frames 1 --zoom 1 --png $OUT/png/${n}_$c.png > /dev/null
    print -n "{\"file\":\"$n\",\"lib\":\"$c\",\"cmp\":" >> $OUT/quality.jsonl
    $B/rsdcompare $OUT/png/${n}_rasterizer.png $OUT/png/${n}_$c.png $OUT/png/${n}_${c}_diff.png | tr -d '\n' >> $OUT/quality.jsonl
    print "}" >> $OUT/quality.jsonl
  done
done

for r in $(seq $ROUNDS); do
  step "Round $r of $ROUNDS"
  for f in $RA/TestFiles/*; do
    for mode in fit:1 sweep:8; do
      for c in ${(s:,:)CONFIGS}; do
        run $c "$f" --frames $FRAMES --zoom ${mode#*:} | sed "s/^{/{\"round\":$r,\"mode\":\"${mode%:*}\",/" >> $OUT/runs.jsonl
      done
    done
  done
done
python3 $S/summarize.py $OUT > $OUT/summary.md
cat $OUT/summary.md
step "Results in $OUT"
