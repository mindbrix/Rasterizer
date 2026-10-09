#!/bin/zsh
# bench.sh: rebuilds & runs the Rasterizer vs Vello vs Skia benchmark over TestFiles from scratch.
#
#   Benchmarks/bench.sh [--rounds N] [--size WxH] [--frames N] [--configs a,b,..] [--rev <git rev>] [--metallib path] [--build-only]
#
# Everything is generated under Benchmarks/build/, which ignores itself:
#   src/        the harness sources, written from this script each run, so this script is their only copy
#   vendor/     vello_encoding 0.11.0 from crates.io, checksummed & patched (see below)
#   target/     cargo builds, bin/ the tools, dumps/<rev>/ the exported TestFiles, metallib/<rev>/
#   results/<stamp>-<rev>-<WxH>/  info.txt (machine, versions), runs.jsonl, quality.jsonl, summary.md, summary.csv, png/
#
# Needs: Xcode command line tools (clang), python3, curl, & Rust (rustup installs the pinned toolchain). The first Skia build
# downloads prebuilt Skia from github.com/rust-skia/skia-binaries. The Rasterizer shaders are compiled from the checkout when the
# Metal toolchain is installed (xcodebuild -downloadComponent MetalToolchain); otherwise pass --metallib, or the newest Release
# build in DerivedData is used, which may not match the source.
#
# --rev benchmarks another commit: its tree is exported with git archive, & its TestFiles, parsers & shaders are used.
#
# How it works:
# - rabench loads each TestFile exactly as RasterizerDemo does (RaSVG / RaPDF page 0), exports the parsed SceneList as an RSD dump,
#   and benchmarks Rasterizer offscreen with a copy of RasterizerLayer's encode loop: keep that in sync with RasterizerLayer -draw.
# - vello_bench (Vello 0.11) & skia_bench (skia-safe 0.153, Metal, Ganesh or Graphite) replay the dumps, so every library draws
#   identical geometry. rsd is their shared reader, view formula, culling, clip runs, stats & PNG I/O, & rsdcompare diffs PNGs.
# - Frames are serialised (build, submit, wait): CPU is to submit, submit-to-done to GPU completion, wall the whole frame, &
#   Rasterizer also reports Metal GPU time. 3 warm-up then N timed frames per run; fit (static) & sweep (1x to 8x zoom) modes;
#   rounds interleave libraries; summary.md takes the best median across rounds, & compares each fit frame with Rasterizer's.
# - Vello's dynamic GPU buffers are fixed sizes picked for Paris-30K & overflow silently (blank frames on the big PDFs), so
#   vello_encoding is patched to scale them 16x. Prebuilt Skia exists for metal+ganesh & metal+graphite but not both, so
#   skia_bench is built twice. Ganesh without MSAA falls back to CPU masks for large AA paths; Chrome renders with MSAA.
set -euo pipefail

HERE=${0:a:h}; REPO=${HERE:h}; W=$HERE/build
RUST=1.99.0
VE_VERSION=0.11.0
VE_SHA256=b1fb9548d2b46b0f79137b5b1d208ecbd76d432f74e7a2ba838615d117f11532
ALL_CONFIGS=rasterizer,vello,vello_retained,skia_ganesh,skia_ganesh_msaa4,skia_graphite
ROUNDS=3; SIZE=2048x2048; FRAMES=30; CONFIGS=$ALL_CONFIGS; REV=; ML=${METALLIB:-}; BUILD_ONLY=0
while (( $# )); do
  case $1 in
    --rounds) ROUNDS=$2; shift ;;
    --size) SIZE=$2; shift ;;
    --frames) FRAMES=$2; shift ;;
    --configs) CONFIGS=$2; shift ;;
    --rev) REV=$2; shift ;;
    --metallib) ML=${2:a}; shift ;;
    --build-only) BUILD_ONLY=1 ;;
    *) sed -n '2,4p' $0; exit 2 ;;
  esac
  shift
done
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
//  bench opts: --size WxH  --frames N  --zoom Z  --png out.png  --pngframe k
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
// RA_IMAGES, set when Shaders.metal samples images from an argument buffer, binds the frame's images once per drawable pass
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

    bool init(const char *metallib, size_t w, size_t h) {
        device = MTLCreateSystemDefaultDevice(), queue = [device newCommandQueue];
        NSError *error = nil;
        library = [device newLibraryWithURL:[NSURL fileURLWithPath:@(metallib)] error:& error];
        if (!library)
            return fprintf(stderr, "metallib: %s\n", error.localizedDescription.UTF8String), false;
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
#if RA_BLEND
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

        bool useClip = false, useImage = false, inMask = false, skipMask = false;
        enum PassKind { kDrawablePass, kEdgesPass, kMaskPass } passKind = kDrawablePass;
        auto beginPass = [&](PassKind kind) {
            if (lazyPasses && passKind == kind && kind != kEdgesPass)
                return;
            [commandEncoder endEncoding];
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
                case Ra::Buffer::kInstancesBase:  instbase = entry.begin;  break;
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
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texCtms atIndex:8];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texIdxs atIndex:9];
                    reverse = uint32_t((entry.end - entry.begin) / sizeof(Ra::Opaque));
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder setVertexBytes:& reverse length:sizeof(reverse) atIndex:12];
                    [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                    [commandEncoder setVertexBytes:& texCount length:sizeof(texCount) atIndex:14];
                    [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:15];
                    [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
                    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:reverse baseInstance:0];
                    break;
                case Ra::Buffer::kQuadEdges:
                case Ra::Buffer::kFastEdges:
                case Ra::Buffer::kFastMolecules:
                case Ra::Buffer::kQuadMolecules:
                    if (entry.type == Ra::Buffer::kQuadEdges) {
                        beginPass(kEdgesPass);
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
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:instbase atIndex:5];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                        [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->p16s atIndex:8];
                        [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                        [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                        [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:14];
                        [commandEncoder setFragmentBuffer:mtlBuffer offset:segbase atIndex:2];
                        [commandEncoder setFragmentBytes:& buffer->params length:sizeof(Ra::Params) atIndex:14];
                        [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:(entry.end - entry.begin) / sizeof(Ra::Edge) baseInstance:0];
                    }
                    break;
                case Ra::Buffer::kInstances:
                    if (skipMask)
                        break;
#if RA_CLIP_IN_PASS
                    beginPass(kDrawablePass);
                    if (inMask) {
                        [commandEncoder setDepthStencilState:clipMaskDepthState];
                        [commandEncoder setRenderPipelineState:clipMask];
                    } else {
                        [commandEncoder setDepthStencilState:instancesDepthState];
                        [commandEncoder setRenderPipelineState:useClip ? instancesClipState : instancesState];
                        uint32_t bounds[4] = { uint32_t(maskBounds.x), uint32_t(maskBounds.y), uint32_t(maskBounds.width), uint32_t(maskBounds.height) };
                        [commandEncoder setFragmentBytes:bounds length:sizeof(bounds) atIndex:0];
#if RA_BLEND
                        if (hasBlends)
                            [commandEncoder setFragmentBuffer:mtlBuffer offset:buffer->modes atIndex:1];
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
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->clips atIndex:5];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texCtms atIndex:8];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texIdxs atIndex:9];
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                    [commandEncoder setVertexBytes:& texCount length:sizeof(texCount) atIndex:14];
                    [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:15];
                    [commandEncoder setFragmentTexture:accumulationTexture atIndex:0];
                    [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
                    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:(entry.end - entry.begin) / sizeof(Ra::Instance) baseInstance:0];
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

static int bench(Ra::SceneList& list, const char *name, const char *metallib, size_t W, size_t H, size_t frames, float zmax, const char *png, long pngFrame) {
    Offscreen gpu;
    if (!gpu.init(metallib, W, H))
        return 1;
    RenderBuffer buffers[2];
    buffers[0].device = buffers[1].device = gpu.device;
    RasterizerRenderer renderer;
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

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 4)
            return fprintf(stderr, "usage: rabench export <in> <out.rsd> | bench <in> <metallib> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k]\n"), 2;
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
        size_t W = 2048, H = 2048, frames = 60;  float zoom = 8.f;  const char *png = nullptr;  long pngFrame = 0;
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
        }
        if (frames == 0)
            frames = 1;
        return bench(list, name, argv[3], W, H, frames, zoom, png, pngFrame);
    }
}
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

w $S/vello/src/main.rs <<'__VELLO_SRC_MAIN_RS__'
//! vello_bench: replays an RSD dump written by `rabench export` with Vello, & times each frame.
//!   vello_bench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--aa area|msaa8|msaa16] [--nocull] [--retained]
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
//! - --retained encodes once & appends per frame: no culling, & hairlines keep their fit-view width as the view zooms

use rsd::{Affine as RAffine, ClipKey, Dump, Opts, Seg, Stats};
use std::{num::NonZeroUsize, sync::Arc, time::Instant};
use vello::{
    AaConfig, AaSupport, RenderParams, Renderer, RendererOptions, Scene,
    kurbo::{Affine, BezPath, Cap, Join, Rect, Stroke},
    peniko::{Blob, BrushRef, Color, Fill, Gradient, ImageAlphaType, ImageBrush, ImageData, ImageFormat},
    wgpu,
};

struct Assets {
    paths: Vec<BezPath>,
    grads: Vec<Gradient>,
    images: Vec<ImageBrush>,
}

fn kurbo(m: RAffine) -> Affine {
    Affine::new(m.0)
}

fn assets(d: &Dump) -> Assets {
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
fn build_scene(scene: &mut Scene, d: &Dump, a: &Assets, device: RAffine, hair: RAffine, w: f64, h: f64, cull: bool) -> usize {
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

# Prebuilt Skia binaries exist for metal+ganesh & metal+graphite, not both, so pick one per build (bench.sh builds both)
[features]
ganesh = ["skia-safe/ganesh"]
graphite = ["skia-safe/graphite"]

[dependencies]
skia-safe = { version = "=0.153.3", features = ["metal"] }
objc2 = "0.6.3"
objc2-metal = { version = "0.3.2", features = ["MTLDevice", "MTLCommandQueue"] }
rsd = { path = "../rsd" }

[profile.release]
debug = 1
__SKIA_CARGO_TOML__

w $S/skia/src/main.rs <<'__SKIA_SRC_MAIN_RS__'
//! skia_bench: replays an RSD dump written by `rabench export` with Skia on Metal, & times each frame.
//! Built with --features ganesh or --features graphite (prebuilt Skia binaries exist for each, not both).
//!   skia_bench <dump.rsd> [--size WxH] [--frames N] [--zoom Z] [--png out.png] [--pngframe k] [--nocull] [--picture] [--msaa N]
//!
//! Mapping from Rasterizer semantics (as vello_bench, see rsd):
//! - device = flip(H) * view * scene ctm * draw ctm, set as each draw's canvas matrix
//! - width -1 is a Skia hairline (width 0), other negative widths |width| / sqrt|det| local, recomputed every frame
//! - clip rect & clip path (even-odd, anti-aliased) are pushed once per run of draws with the same clip
//! - gradients: shaders in brush space with Bitmap::ctm as their local matrix. Images: shaders over the path bounds
//! - offscreen draws are culled on the CPU, as drawList does (--nocull to disable)
//! - --picture records an SkPicture once (with an R-tree) & draws it with each frame's view: no CPU culling of
//!   our own, & widths other than -1 keep their fit-view width as the view zooms
//! - --msaa N: Ganesh render target sample count (default 1, Skia's analytic AA)

#[cfg(not(any(feature = "ganesh", feature = "graphite")))]
compile_error!("build with --features ganesh or --features graphite");

use rsd::{Affine, ClipKey, Dump, Opts, Seg, Stats};
use skia_safe::{
    AlphaType, ClipOp, Color, ColorType, Data, FilterMode, ImageInfo, Matrix, MipmapMode, Paint, PaintCap, PaintJoin,
    PaintStyle, Path, PathBuilder, PathFillType, Picture, PictureRecorder, Rect, SamplingOptions, Shader, TileMode,
    images,
    gradient::{Colors, Gradient, Interpolation, shaders as gradients},
};
use std::time::Instant;

struct Assets {
    paths: Vec<Path>,
    even_odd: Vec<Option<Path>>,
    grads: Vec<Shader>,
    images: Vec<skia_safe::Image>,
}

fn matrix(m: Affine) -> Matrix {
    Matrix::from_affine(&m.0.map(|v| v as f32))
}

fn assets(d: &Dump) -> Assets {
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
fn draw_dump(canvas: &skia_safe::Canvas, d: &Dump, a: &Assets, device: Affine, hair: Affine, w: f64, h: f64, cull: bool) -> usize {
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

#[cfg(feature = "ganesh")]
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

#[cfg(feature = "graphite")]
mod backend {
    use skia_safe::gpu::graphite::{self, mtl as gmtl};
    use skia_safe::{AlphaType, ColorType, ImageInfo, Surface, gpu};
    pub const NAME: &str = "skia_graphite";
    pub struct Gpu {
        pub context: graphite::Context,
        pub recorder: graphite::Recorder,
        pub surface: Surface,
        _device: objc2::rc::Retained<objc2::runtime::ProtocolObject<dyn objc2_metal::MTLDevice>>,
        _queue: objc2::rc::Retained<objc2::runtime::ProtocolObject<dyn objc2_metal::MTLCommandQueue>>,
    }
    impl Gpu {
        pub fn new(w: u32, h: u32, _msaa: usize) -> Self {
            use objc2::rc::Retained;
            use objc2_metal::{MTLCreateSystemDefaultDevice, MTLDevice};
            let device = MTLCreateSystemDefaultDevice().expect("Metal device");
            let queue = device.newCommandQueue().expect("command queue");
            let backend = unsafe {
                gmtl::BackendContext::new(Retained::as_ptr(&device) as *mut std::ffi::c_void, Retained::as_ptr(&queue) as *mut std::ffi::c_void)
            };
            let mut context = gmtl::context_factory::make_metal(&backend, None).expect("Graphite Metal context");
            let mut recorder = context.make_recorder(None).expect("recorder");
            let info = ImageInfo::new((w as i32, h as i32), ColorType::RGBA8888, AlphaType::Premul, None);
            let surface = graphite::surfaces::render_target(&mut recorder, &info, gpu::Mipmapped::No, None, Some("skia_bench")).expect("render target");
            Gpu { context, recorder, surface, _device: device, _queue: queue }
        }
        /// Graphite draws only texture-backed images, so upload once up front
        pub fn prepare_image(&mut self, image: skia_safe::Image) -> skia_safe::Image {
            graphite::images::texture_from_image(&mut self.recorder, &image).expect("texture image")
        }
        /// Snaps the recorder's work into a recording & commits it
        pub fn submit(&mut self) {
            let mut recording = self.recorder.snap().expect("snap");
            self.context.insert_recording(&graphite::InsertRecordingInfo::new(&mut recording));
            self.context.submit(None);
        }
        pub fn wait(&mut self) {
            self.context.submit_and_wait();
        }
        pub fn read_rgba(&mut self, w: u32, h: u32) -> Vec<u8> {
            let info = ImageInfo::new((w as i32, h as i32), ColorType::RGBA8888, AlphaType::Premul, None);
            let mut px = vec![0u8; (w * h * 4) as usize];
            assert!(self.context.read_pixels(&mut self.surface, &info, &mut px, w as usize * 4, (0, 0)), "read_pixels");
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
if [[ ! -x $B/rabench || $S/ra/rabench.mm -nt $B/rabench || -n $(find $INC $RA/Rasterizer/Demo/RasterizerPDF.hpp -newer $B/rabench 2>/dev/null) || $(cat $W/obj/rev 2>/dev/null) != $RA ]]; then
  step "Building rabench"
  clang -O3 -c $RA/Package/Sources/RasterizerCpp/xxhash.c -I$INC -o $W/obj/xxhash.o
  clang++ -O3 -std=c++17 -c -x objective-c++ $RA/Package/Sources/RasterizerCpp/nanosvg.cc -I$INC -o $W/obj/nanosvg.o
  CLIP=0; grep -q kClipMask $INC/Rasterizer.hpp && CLIP=1    # Rasterizer's anti-aliased clip mask, else its stencil clipping
  CLEAR=0; grep -q kClipClear $INC/Rasterizer.hpp && CLEAR=1    # Its clear cells, else a clear pass
  INPASS=0; grep -q '\[\[color(1)\]\]' $INC/Shaders.metal && INPASS=1    # Its in-pass mask, read with framebuffer fetch, else mask passes
  BLEND=0; grep -q kBlendNormal $INC/Rasterizer.h && BLEND=1    # Its blend modes, blended in the shader
  IMAGES=0; grep -q 'function_constant(kImages)' $INC/Shaders.metal && IMAGES=1    # Its images, from an argument buffer
  RULE=0; grep -q kClipEvenOdd $INC/Rasterizer.hpp && RULE=1    # Its draws' clip fill rules, else even-odd clips
  clang++ -O3 -std=c++17 -fobjc-arc -DRA_CLIP_MASK=$CLIP -DRA_CLIP_CLEAR_CELLS=$CLEAR -DRA_CLIP_IN_PASS=$INPASS -DRA_BLEND=$BLEND -DRA_IMAGES=$IMAGES -DRA_CLIP_RULE=$RULE -x objective-c++ $S/ra/rabench.mm -x none $W/obj/xxhash.o $W/obj/nanosvg.o \
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
[[ ,$CONFIGS, == *,skia_ganesh* ]] && cargo_build skia skia_ganesh skia_bench "--features ganesh"
[[ ,$CONFIGS, == *,skia_graphite* ]] && cargo_build skia skia_graphite skia_bench "--features graphite"

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
(( BUILD_ONLY )) && { step "Built into $B"; exit 0 }

D=$W/dumps/$SHA; mkdir -p $D
step "Exporting TestFiles"
for f in $RA/TestFiles/*; do
  n=${${f:t}%.*}
  [[ $D/$n.rsd -nt $f && $D/$n.rsd -nt $B/rabench ]] || $B/rabench export "$f" $D/$n.rsd > /dev/null 2>> $D/export.log
done

# Each config: name, binary & extra args. rasterizer takes the source file, the replays its dump
typeset -A BIN ARGS
BIN=(vello vello vello_retained vello skia_ganesh skia_ganesh skia_ganesh_msaa4 skia_ganesh skia_graphite skia_graphite)
ARGS=(vello "" vello_retained --retained skia_ganesh "" skia_ganesh_msaa4 "--msaa 4" skia_graphite "")
REPLAYS=(${${(s:,:)CONFIGS}:#rasterizer})
run() {    # config, file, png | frames zoom
  local c=$1 f=$2 n=${${2:t}%.*}; shift 2
  if [[ $c == rasterizer ]]; then $B/rabench bench "$f" $ML --size $SIZE "$@"
  else $B/$BIN[$c] $D/$n.rsd --size $SIZE ${=ARGS[$c]} "$@"; fi
}

OUT=$W/results/$(date +%Y%m%d-%H%M%S)-$SHA-$SIZE; mkdir -p $OUT/png
{
  print "date: $(date -u +%FT%TZ)"
  print "rasterizer: $SHA ($RA)"
  print "metallib: $ML ($MLSRC)"
  print "machine: $(sysctl -n hw.model), $(sysctl -n machdep.cpu.brand_string), $(( $(sysctl -n hw.memsize) / 1073741824 )) GB"
  print "macOS: $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
  print "clang: $(clang --version | head -1)"
  print "rustc: $(cd $S/rsd && rustc --version)"
  print "crates: $(cat $S/*/Cargo.lock | awk '/^name = /{n=$3} /^version = /{if (n ~ /^"(vello|vello_encoding|wgpu|skia-safe|skia-bindings)"$/) print n "@" $3}' | tr -d '"' | sort -u | tr '\n' ' ')"
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
