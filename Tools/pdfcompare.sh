#!/bin/zsh
#
#   Copyright 2026 Nigel Timothy Barber - nigel@mindbrix.co.uk
#   SPDX-License-Identifier: MIT
#
# pdfcompare.sh: renders PDF pages with Rasterizer & Core Graphics at the same size, & ranks them by how much they differ.
#
#   Tools/pdfcompare.sh [--size N] [--pages N] [--threshold T] [--keep P] [--timeout S] [--metallib path] [<pdf or folder>...]
#
# With no paths it compares TestFiles. Folders are searched recursively for PDFs. Everything is generated under Tools/build/,
# which ignores itself:
#   src/, bin/, obj/   pdfcompare.mm, written from this script, & its build
#   metallib/<rev>/    the shaders, compiled from the checkout
#   results/<stamp>-<rev>/   report.md, worst first, report.tsv, & png/ with ra, cg & diff images of the pages that differ
#
# Options: --size, the square render size (2048); --pages, the most pages per file (all); --threshold, the channel difference a
# differing pixel exceeds (32); --keep, the differing % above which a page's images are kept (0.5), as they are for any page with
# fallback draws; --timeout, the seconds a file may take (300); --metallib, the shaders, else they're compiled from the checkout.
#
# How it works:
# - Each page is parsed by RaPDF, as the demo app loads it, & rendered offscreen by the Metal renderer with the rabench harness,
#   whose source is extracted from Benchmarks/bench.sh, so its copy of RasterizerLayer's encode loop is the only one.
# - Core Graphics renders the page into a DeviceRGB bitmap with the same transform: the loader's page transform, then a fit of the
#   rotated media box to the render size, on white. Neither clips to the crop box.
# - A pixel differs if no pixel of the other render within 1 pixel is within --threshold of it, both ways, so edge anti-aliasing
#   & half pixel shifts don't count. A page's score is the % of pixels that differ.
# - Pages with draws in RaPDF's fallback color, red for content it can't draw, are flagged, as are files that crash or time out:
#   each file is compared in its own process. The fallback color is built as an odd red, so content's own red isn't counted.
set -euo pipefail

HERE=${0:a:h}; REPO=${HERE:h}; W=$HERE/build
SIZE=2048; PAGES=0; THRESHOLD=32; KEEP=0.5; TIMEOUT=300; ML=${METALLIB:-}; INPUTS=()
while (( $# )); do
  case $1 in
    --size) SIZE=$2; shift ;;
    --pages) PAGES=$2; shift ;;
    --threshold) THRESHOLD=$2; shift ;;
    --keep) KEEP=$2; shift ;;
    --timeout) TIMEOUT=$2; shift ;;
    --metallib) ML=${2:a}; shift ;;
    -*) sed -n '8p' $0; exit 2 ;;
    *) INPUTS+=(${1:a}) ;;
  esac
  shift
done
(( $#INPUTS )) || INPUTS=($REPO/TestFiles)
die() { print -u2 "pdfcompare.sh: $*"; exit 1 }
step() { print -P "%B==> $*%b" }
mkdir -p $W; print '*' > $W/.gitignore

INC=$REPO/Package/Sources/RasterizerCpp/include
SHA=$(git -C $REPO rev-parse --short HEAD)
git -C $REPO diff --quiet HEAD -- Package || SHA=$SHA-dirty

S=$W/src; B=$W/bin; O=$W/obj; mkdir -p $S $B $O
w() { mkdir -p ${1:h}; cat > $1.new; cmp -s $1.new $1 && rm $1.new || mv $1.new $1 }    # Keeps mtimes, so builds stay incremental

# The rabench harness, with its main renamed, as pdfcompare.mm includes it
sed -n "/<<'__RA_RABENCH_MM__'/,/^__RA_RABENCH_MM__/p" $REPO/Benchmarks/bench.sh | sed '1d;$d' | sed 's/^int main(/static int rabench_main(/' | w $S/rabench.mm
[[ -s $S/rabench.mm ]] || die "can't extract rabench.mm from Benchmarks/bench.sh"

w $S/pdfcompare.mm <<'__PDFCOMPARE_MM__'
//  pdfcompare <metallib> <size> <pages> <threshold> <keep %> <png prefix> <pdf>: renders each page with Rasterizer & Core Graphics,
//  & prints a TSV line per page: page, pages, differing %, max channel difference, fallback draws, draws, parse ms, & whether its
//  images were kept, as <png prefix>_p<page>_{ra,cg,diff}.png
#include "rabench.mm"

static bool writeImage(const uint8_t *bgra, size_t w, size_t h, const std::string& path) {
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate((void *)bgra, w, h, 8, w * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:@(path.c_str())], kUTTypePNG, 1, nullptr);
    CGImageDestinationAddImage(dst, image, nullptr);
    bool ok = CGImageDestinationFinalize(dst);
    CFRelease(dst), CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
    return ok;
}

// The offscreen target's BGRA pixels, top row first
static std::vector<uint8_t> readTarget(Offscreen& gpu) {
    size_t w = gpu.target.width, h = gpu.target.height, bpr = w * 4;
    id<MTLBuffer> readback = [gpu.device newBufferWithLength:bpr * h options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [gpu.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:gpu.target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:readback destinationOffset:0 destinationBytesPerRow:bpr destinationBytesPerImage:bpr * h];
    [blit endEncoding], [cb commit], [cb waitUntilCompleted];
    const uint8_t *p = (const uint8_t *)readback.contents;
    return std::vector<uint8_t>(p, p + bpr * h);
}

// Core Graphics' render of the page, on white, with ctm from page space to the bitmap's, which is y up like Rasterizer's
static std::vector<uint8_t> renderCG(CGPDFPageRef page, Ra::Transform ctm, size_t w, size_t h) {
    std::vector<uint8_t> px(w * h * 4);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1), CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
    // Glyphs as outlines, as Rasterizer draws them, without smoothing's dilation or quantized positions
    CGContextSetAllowsFontSmoothing(ctx, false), CGContextSetAllowsFontSubpixelQuantization(ctx, false), CGContextSetAllowsFontSubpixelPositioning(ctx, true);
    CGContextConcatCTM(ctx, CGAffineTransformMake(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx, ctm.ty));
    CGContextDrawPDFPage(ctx, page);
    CGContextRelease(ctx), CGColorSpaceRelease(rgb);
    return px;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 8)
            return fprintf(stderr, "usage: pdfcompare <metallib> <size> <pages> <threshold> <keep %%> <png prefix> <pdf>\n"), 2;
        const char *metallib = argv[1], *path = argv[7];
        size_t W = strtoul(argv[2], nullptr, 10), H = W, maxPages = strtoul(argv[3], nullptr, 10);
        int threshold = atoi(argv[4]);  double keep = atof(argv[5]);  std::string prefix = argv[6];
        CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:@(path)]);
        size_t pages = doc ? CGPDFDocumentGetNumberOfPages(doc) : 0;
        if (pages == 0)
            return fprintf(stderr, "can't open %s\n", path), 1;
        Offscreen gpu;
        if (!gpu.init(metallib, W, H))
            return 1;
        RenderBuffer buffer;  buffer.device = gpu.device;
        RasterizerRenderer renderer;
        Ra::Color fallback = RaPDF::fallbackColor();
        for (size_t p = 0; p < (maxPages ? std::min(pages, maxPages) : pages); p++) {
            @autoreleasepool {
                CGPDFPageRef page = CGPDFDocumentGetPage(doc, p + 1);
                double t0 = now();
                Ra::SceneRef scene;
                Ra::Transform m = RaPDF::addPdfPageToScene(path, p, scene);
                double parse = now() - t0;
                size_t draws = 0, fallbacks = 0;
                for (size_t i = 0; i < scene->count(); i++) {
                    const Ra::Draw& d = scene->draws[i];
                    if (d.flags & Ra::Draw::kInvisible)
                        continue;
                    draws++;
                    if (d.paint.type == Ra::Paint::kColor && !memcmp(& d.paint.color, & fallback, sizeof(fallback)))
                        fallbacks++;
                }
                // Both fit the media box, rotated by the loader's page transform, to the render size
                CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
                Ra::Bounds media(box.origin.x, box.origin.y, box.origin.x + box.size.width, box.origin.y + box.size.height);
                Ra::Transform fit = Ra::Bounds(0.f, 0.f, W, H).fitTransform(Ra::Bounds(media.quad(m)));
                Ra::SceneList list;
                list.addScene(scene, m);
                list.ctm = fit;
                list.prepare();
                renderer.renderList(list, 1.f, W, H, & buffer);
                id<MTLCommandBuffer> cb = gpu.encode(& buffer);
                [cb commit], [cb waitUntilCompleted];
                if (cb.status == MTLCommandBufferStatusError)
                    fprintf(stderr, "%s page %zu: %s\n", path, p, cb.error.localizedDescription.UTF8String);
                std::vector<uint8_t> ra = readTarget(gpu), cg = renderCG(page, m.concat(fit), W, H);

                // A pixel's difference is the least to the other render's pixels within 1 pixel, both ways, so anti-aliasing &
                // half pixel shifts at edges don't count, but missing, extra or wrongly colored areas do
                auto nearest = [&](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t x, size_t y) {
                    int least = 255;
                    for (size_t v = y ? y - 1 : y; v <= std::min(H - 1, y + 1); v++)
                        for (size_t u = x ? x - 1 : x; u <= std::min(W - 1, x + 1); u++) {
                            const uint8_t *p = & a[(y * W + x) * 4], *q = & b[(v * W + u) * 4];
                            least = std::min(least, std::max({ abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2]) }));
                        }
                    return least;
                };
                size_t differ = 0;  int maxDiff = 0;
                std::vector<uint8_t> diff(cg.size());
                for (size_t i = 0; i < W * H; i++) {
                    int d = std::max(nearest(ra, cg, i % W, i / W), nearest(cg, ra, i % W, i / W));
                    maxDiff = std::max(maxDiff, d);
                    differ += d > threshold;
                    for (int k = 0; k < 3; k++)     // Differing pixels red, over a faded CG render
                        diff[i * 4 + k] = d > threshold ? (k == 2 ? 255 : 0) : uint8_t(192 + cg[i * 4 + k] / 4);
                    diff[i * 4 + 3] = 255;
                }
                double percent = 100.0 * differ / (W * H);
                bool kept = percent > keep || fallbacks > 0;
                if (kept) {
                    std::string base = prefix + "_p" + std::to_string(p);
                    writeImage(ra.data(), W, H, base + "_ra.png"), writeImage(cg.data(), W, H, base + "_cg.png"), writeImage(diff.data(), W, H, base + "_diff.png");
                }
                printf("%zu\t%zu\t%.3f\t%d\t%zu\t%zu\t%.1f\t%d\n", p, pages, percent, maxDiff, fallbacks, draws, parse * 1e3, kept);
                fflush(stdout);
            }
        }
        CGPDFDocumentRelease(doc);
    }
    return 0;
}
__PDFCOMPARE_MM__

if [[ ! -x $B/pdfcompare || $0 -nt $B/pdfcompare || $S/pdfcompare.mm -nt $B/pdfcompare || $S/rabench.mm -nt $B/pdfcompare || -n $(find $INC -newer $B/pdfcompare) ]]; then
  step "Building pdfcompare"
  clang -O3 -c $REPO/Package/Sources/RasterizerCpp/xxhash.c -I$INC -o $O/xxhash.o
  clang++ -O3 -std=c++17 -c -x objective-c++ $REPO/Package/Sources/RasterizerCpp/nanosvg.cc -I$INC -o $O/nanosvg.o
  # rabench's feature flags, as Benchmarks/bench.sh sets them for this checkout
  CLIP=0; grep -q kClipMask $INC/Rasterizer.hpp && CLIP=1
  CLEAR=0; grep -q kClipClear $INC/Rasterizer.hpp && CLEAR=1
  INPASS=0; grep -q '\[\[color(1)\]\]' $INC/Shaders.metal && INPASS=1
  BLEND=0; grep -q kBlendNormal $INC/Rasterizer.h && BLEND=1
  IMAGES=0; grep -q 'function_constant(kImages)' $INC/Shaders.metal && IMAGES=1
  RULE=0; grep -q kClipEvenOdd $INC/Rasterizer.hpp && RULE=1
  # Fallback draws are an odd red, so ones of the content's own red aren't counted
  clang++ -O3 -std=c++17 -fobjc-arc -DRA_PDF_FALLBACK_BGRA=3,1,254,255 -DRA_CLIP_MASK=$CLIP -DRA_CLIP_CLEAR_CELLS=$CLEAR -DRA_CLIP_IN_PASS=$INPASS -DRA_BLEND=$BLEND -DRA_IMAGES=$IMAGES -DRA_CLIP_RULE=$RULE \
    -x objective-c++ $S/pdfcompare.mm -x none $O/xxhash.o $O/nanosvg.o -I$S -I$INC \
    -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices \
    -Wno-deprecated-declarations -Wno-unused-function -o $B/pdfcompare
fi

if [[ -z $ML ]]; then
  ML=$W/metallib/$SHA/default.metallib
  if [[ ! -f $ML || $INC/Shaders.metal -nt $ML ]]; then
    step "Compiling shaders"
    mkdir -p ${ML:h}
    if ! { xcrun -sdk macosx metal -c $INC/Shaders.metal -I$INC -o ${ML:h}/Shaders.air 2> ${ML:h}/metal.log \
        && xcrun -sdk macosx metallib ${ML:h}/Shaders.air -o $ML 2>> ${ML:h}/metal.log }; then
      rm -f $ML
      die "can't compile Shaders.metal (see ${ML:h}/metal.log): install the Metal toolchain, or pass --metallib"
    fi
  fi
fi

FILES=(); ROOTS=()    # Each file, & the folder its name in the report is relative to
for i in $INPUTS; do
  if [[ -d $i ]]; then for f in $i/**/*.(pdf|PDF)(N.); do FILES+=($f); ROOTS+=(${i:h}); done
  elif [[ -f $i ]]; then FILES+=($i); ROOTS+=(${i:h:h})
  else die "no such file or folder: $i"; fi
done
(( $#FILES )) || die "no PDFs in ${INPUTS[*]}"

OUT=$W/results/$(date +%Y%m%d-%H%M%S)-$SHA; mkdir -p $OUT/png
TSV=$OUT/report.tsv; FAILS=$OUT/failures.tsv
print "file\tpage\tpages\tdiffer_pct\tmax_diff\tfallbacks\tdraws\tparse_ms\tkept\timages" > $TSV
: > $FAILS
step "Comparing $#FILES files at ${SIZE}x$SIZE"
n=0
for f in $FILES; do
  (( ++n ))
  rel=${f#${ROOTS[n]}/}; name=$(printf '%03d-%s' $n ${${f:t:r}//[^A-Za-z0-9._-]/_})
  print -n "\r\e[K[$n/$#FILES] $rel"
  set +e
  lines=$(perl -e 'alarm shift; exec @ARGV' $TIMEOUT $B/pdfcompare $ML $SIZE $PAGES $THRESHOLD $KEEP $OUT/png/$name $f 2> $OUT/png/$name.log)
  st=$?
  set -e
  [[ -s $OUT/png/$name.log ]] || rm -f $OUT/png/$name.log
  [[ -n $lines ]] && print -r -- "$lines" | while IFS=$'\t' read -r page pages pct maxd fb draws ms kept; do
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' $rel $page $pages $pct $maxd $fb $draws $ms $kept ${name}_p$page >> $TSV
  done
  if (( st == 142 )); then printf '%s\t%s\n' $rel "timed out after ${TIMEOUT}s" >> $FAILS
  elif (( st > 128 )); then printf '%s\t%s\n' $rel "crashed with signal $(( st - 128 )) ($(kill -l $(( st - 128 ))))" >> $FAILS
  elif (( st )); then printf '%s\t%s\n' $rel "failed: $(head -1 $OUT/png/$name.log 2>/dev/null)" >> $FAILS
  fi
done
print "\r\e[K"

# report.md: failures, then pages worst first, with links to the kept images
{
  print "# PDF compare: Rasterizer vs Core Graphics\n"
  print "Rev $SHA, ${SIZE}x$SIZE, a pixel differs if no pixel of the other render within 1 pixel is within $THRESHOLD in each channel. $#FILES files, $(( $(wc -l < $TSV) - 1 )) pages.\n"
  if [[ -s $FAILS ]]; then
    print "## Failures\n\n| File | Problem |\n|---|---|"
    while IFS=$'\t' read -r file problem; do print "| $file | $problem |"; done < $FAILS
    print
  fi
  print "## Pages, worst first\n\n| # | File | Page | Differ % | Max | Fallbacks | Draws | Parse ms | Images |\n|---|---|---|---|---|---|---|---|---|"
  tail -n +2 $TSV | sort -t$'\t' -k4,4gr | awk -F'\t' '{
    img = $9 ? sprintf("[ra](png/%s_ra.png) [cg](png/%s_cg.png) [diff](png/%s_diff.png)", $10, $10, $10) : ""
    printf "| %d | %s | %d/%d | %.2f | %d | %s | %d | %s | %s |\n", NR, $1, $2 + 1, $3, $4, $5, ($6 > 0 ? "**" $6 "**" : "0"), $7, $8, img }'
} > $OUT/report.md

step "Results in $OUT"
print "$(grep -c . $FAILS) failures, $(tail -n +2 $TSV | awk -F'\t' '$6 > 0' | wc -l | tr -d ' ') pages with fallback draws. Worst pages:"
tail -n +2 $TSV | sort -t$'\t' -k4,4gr | head -10 | awk -F'\t' '{ printf "  %6.2f%%  %s p%d\n", $4, $1, $2 + 1 }'
