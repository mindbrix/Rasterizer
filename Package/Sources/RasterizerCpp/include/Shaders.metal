//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "Rasterizer.h"
#import <metal_stdlib>
using namespace metal;

constexpr sampler s = sampler(coord::normalized, address::clamp_to_zero, mag_filter::nearest, min_filter::nearest, mip_filter::linear);
constexpr sampler cs = sampler(coord::normalized, address::clamp_to_edge, mag_filter::linear, min_filter::linear, mip_filter::linear);

struct Transform {
    float a, b, c, d, tx, ty;
};

struct Bounds {
    float lx, ly, ux, uy;
};

struct Colorant {
    uint8_t b, g, r, a;
};

struct Point16 {
    enum Flags { isCurve = 1 << 15, kMask = ~isCurve };
    uint16_t x, y;
};
struct Segment {
    union { float x0; uint32_t ix0; };  float y0, x1, y1;
};

struct Cell {
    uint16_t lx, ly, ux, uy, ox, oy;
};

struct Quad {
    Cell cell;  short cover;  int base, biid;
};

struct Quadratic {
    float x0, y0, x1, y1, x2, y2;
};

struct Outline {
    Quadratic quad;
    short prev, next;
};

struct Opaque {
    uint32_t iz;  union { Cell cell;  Quadratic quad; };
};

struct Instance {
    enum Flags {
        kRoundJoin = 1 << 21,   kStencil = 1 << 21,
        kIsRadial = 1 << 22,
        kIsGradient = 1 << 23,
        kIsCurve = 1 << 24,
        kMolecule = 1 << 25,    kPCap = 1 << 25,
        kFastEdges = 1 << 26,   kNCap = 1 << 26,
        kEdge = 1 << 27,        kF0 = 1 << 27,
        kRoundCap = 1 << 28,    kF1 = 1 << 28,
        kOutlines = 1 << 29,
        kSquareCap = 1 << 30,
        kEvenOdd = 1 << 31,
        kFragmentMask = (kOutlines | kSquareCap | kEvenOdd)
    };
    uint32_t iz;  union { Quad quad;  Outline outline; };
};

struct Edge {
    uint32_t ic;  enum Flags { ue0 = 0xF << 28, ue1 = 0xF << 24, kMask = ~(ue0 | ue1) };
    uint16_t i0, ux;
};

struct Params {
    bool useClips, useCurves, showOpaques, showOutlines;
    Colorant clearColor;
};


// https://www.shadertoy.com/view/4dsfRS

float sqBezier(float2 p0, float2 p1, float2 p2) {
    // Calculate roots.
    float2 vb = p1 - p0, va, vc = { p0.y - p2.y, p2.x - p0.x }, rp1;
    float t = dot(vc, vb) / dot(vc, vc);
    float dt = kCubicSolverLimit - min(kCubicSolverLimit, abs(t));
    rp1 = p1 + dt * sign(t) * vc;
    vb = rp1 - p0, va = p2 - rp1, va -= vb;
    float kk = 1.0 / dot(va, va);
    float a = kk * dot(vb, va), b = kk * (2.0 * dot(vb, vb) + dot(p0, va)), c = kk * dot(p0, vb);
    float p = b - 3.0 * a * a, p3 = p * p * p;
    float q = a * (2.0 * a * a - b) + c;
    float d = q * q + 4.0 * p3 / 27.0;
    float third = 1.0 / 3.0;
    vb = p1 - p0, va = p2 - p1, va -= vb, vb *= 2.0;
    if (d >= 0.0) {
        float z = sqrt(d);
        float2 x = 0.5 * (float2(z, -z) - q);
        float2 uv = sign(x) * pow(abs(x), third);
        float t = saturate(uv.x + uv.y - a);
        float2 pt = fma(fma(va, t, vb), t, p0);
        return dot(pt, pt);
    }
    float v = acos(-sqrt(-27.0 / p3) * 0.5 * q) * third;
    float m = cos(v), n = sin(v) * 1.732050808;
    float2 ts = saturate(float2(m + m, -n - m) * sqrt(-p * third) - a);
    float2 pt0 = fma(fma(va, ts.x, vb), ts.x, p0);
    float2 pt1 = fma(fma(va, ts.y, vb), ts.y, p0);
    return min(dot(pt0, pt0), dot(pt1, pt1));
}

float dwinding(float x0, float y0, float x1, float y1, float w0, float w1) {
    float cover, s, wm, dx, ax, dy, ay, dist;
    cover = w1 - w0, s = 1.0 / cover, wm = 0.5 * (w0 + w1);
    dx = x1 - x0, ax = x0 - 0.5;
    dy = (y1 - y0) * s, ay = (y0 - wm) * s;
    dist = (ax * dy - ay * dx) * rsqrt(dx * dx + dy * dy);
    return saturate(0.5 - dist) * cover;
}

// Calculate winding using an inverse lerp algorithm on signed areas from the line (x0, y0), (x1,y1) to the pixel rect
// bounded vertically by w0 & w1 in the [0..1] pixel coordinate space.
// The nearest corner is at t = 1 (full coverage) and the furthest corner is at t = 0.
// Coverage t is then filtered to compensate for the varying distance between the corners depending on line slope.
// This filter can also be used to create zero-cost box blurs.
//
float awinding(float x0, float y0, float x1, float y1, float w0, float w1) {
    float dx, dy, a0, t, b, f, cover = w1 - w0;
    dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.0 ? w0 : w1) - y0) - dy * (1.0 - x0);
    dx = abs(dx), t = -a0 / fma(dx, cover, dy), dy = abs(dy);
    b = max(dx, dy), f = b / (b - min(dx, dy) * 0.4142135624);
    return saturate((t - 0.5) * f + 0.5) * cover;
}

// Winding for a line start(x0, y0), end(x1, y1)
//
float lineWinding(float x0, float y0, float x1, float y1) {
    return awinding(x0, y0, x1, y1, saturate(y0), saturate(y1));
}

// Winding for a quadratic curve start(x0, y0), control(x1, y1), end(x2, y2)
//
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2) {
    float w0 = saturate(y0), w2 = saturate(y2);
    if (max(x0, max(x1, x2)) <= 0.0)
        return w2 - w0;
    float w = 0.0, ay = y2 - y1, by = y1 - y0, cy, t, s, w1;
    // Only one solution for a monotonic curve
    w1 = saturate(ay * by >= 0.0 ? y2 : y0 - by * by / (ay - by)), ay -= by, by *= 2.0;
    if (w0 != w1) {
        // Solve the quadratic for the window centre
        cy = y0 - 0.5 * (w0 + w1);
        t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + sign(w1 - w0) * sqrt(max(0.0, by * by - 4.0 * ay * cy))) / ay * 0.5;
        // Winding for the tangent vector at t
        s = 1.0 - t, w += awinding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w0, w1);
    }
    if (w1 != w2) {
        cy = y0 - 0.5 * (w1 + w2);
        t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + sign(w2 - w1) * sqrt(max(0.0, by * by - 4.0 * ay * cy))) / ay * 0.5;
        s = 1.0 - t, w += awinding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w1, w2);
    }
    return w;
}

#pragma mark - Stencil

struct StencilVertex
{
    float4 position [[position]];
};

vertex StencilVertex stencil_vertex_main(const device Opaque *stencils [[buffer(1)]],
                                         constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                         uint vid [[vertex_id]], uint iid [[instance_id]])
{
    const device Opaque& inst = stencils[iid];
    const device Quadratic& quad = inst.quad;
    float x = vid == 0 ? quad.x0 : vid == 1 ? quad.x1 : quad.x2;
    float y = vid == 0 ? quad.y0 : vid == 1 ? quad.y1 : quad.y2;
    StencilVertex vert;
    vert.position = {
        x / *width * 2.0 - 1.0,
        y / *height * 2.0 - 1.0,
        1.0,
        1.0
    };
    return vert;
}


#pragma mark - Opaques

struct OpaquesVertex
{
    float4 position [[position]];
    float3 tex;
};

vertex OpaquesVertex opaques_vertex_main(const device Colorant *colors [[buffer(0)]],
                                         const device Opaque *opaques [[buffer(1)]],
                                         const device float *widths [[buffer(6)]],
                                         const device Transform *texCtms [[buffer(8)]],
                                         const device uint32_t *texIdxs [[buffer(9)]],
                                         constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                         constant uint *reverse [[buffer(12)]],
                                         constant uint *pathCount [[buffer(13)]],
                                         constant uint *texCount [[buffer(14)]],
                                         constant Params *params [[buffer(15)]],
                                         uint vid [[vertex_id]], uint iid [[instance_id]])
{
    const device Opaque& inst = opaques[*reverse - 1 - iid];
    const uint iz = inst.iz & kPathIndexMask;
    const bool isGradient = inst.iz & Instance::kIsGradient;
    const bool isRadial = inst.iz & Instance::kIsRadial;
    const device Transform& texCtm = texCtms[iz];
    const device Cell& cell = inst.cell;
    const device Quadratic& quad = inst.quad;
    float x, y, z = kDepthRange * float((inst.iz & kPathIndexMask) + 1) / float(*pathCount);
    
    if (inst.iz & Instance::kOutlines) {
        const float dw = 0.5 * (widths[inst.iz & kPathIndexMask] - 1.0);
        const float x0 = quad.x0, y0 = quad.y0, x1 = quad.x1, y1 = quad.y1, x2 = quad.x2, y2 = quad.y2;
        const bool isFlat = x1 == FLT_MAX;
        const bool isTop = vid >> 1, isRight = vid & 1;
        const bool pcap = inst.iz & Instance::kPCap;
        const bool ncap = inst.iz & Instance::kNCap;
        const bool isCap = (isTop && ncap) || (!isTop && pcap);
        const bool useCurves = params->useCurves;
        const float sign = isRight ? -1.0 : 1.0;
        
        float ax, bx, cx, ay, by, cy, cross, dx, dy, ow, miterLen;
        ax = x2 - x1, bx = x1 - x0, cx = x2 - x0;
        ay = y2 - y1, by = y1 - y0, cy = y2 - y0;
        float2 unit = normalize({
            isFlat || (isCap && !useCurves) ? cx : (isTop ? ax : bx),
            isFlat || (isCap && !useCurves) ? cy : (isTop ? ay : by)
        });
        cross = bx * -cy + by * cx;
        ow = isFlat || !useCurves || sign * cross > 0.0 ? 0.0 : 0.5 * abs(cross / (cx * unit.x + cy * unit.y));
        miterLen = sign * max(-dw, dw - ow);

        dx = isCap ? 0.5 * unit.x : 0.0, x = (isTop ? x2 - dx : x0 + dx) + miterLen * -unit.y;
        dy = isCap ? 0.5 * unit.y : 0.0, y = (isTop ? y2 - dy : y0 + dy) + miterLen * unit.x;
    } else {
        x = select(cell.lx, cell.ux, vid & 1);
        y = select(cell.ly, cell.uy, vid >> 1);
    }
    OpaquesVertex vert;
    vert.position = {
        x / *width * 2.0 - 1.0,
        y / *height * 2.0 - 1.0,
        z,
        1.0
    };
    int tw = kColorTextureWidth, th = (*pathCount + tw - 0) / tw;
    uint tiz = params->showOpaques ? iz : *pathCount;
    vert.tex.x = (0.5 + (tiz % tw)) / float(tw);
    vert.tex.y = (0.5 + (tiz / tw)) / float(th + *texCount);
    vert.tex.z = 0.0;
    
    if (params->showOpaques && isGradient) {
        vert.tex.x = x * texCtm.b + y * texCtm.d + texCtm.ty;
        vert.tex.y = (0.5 + (th + texIdxs[iz])) / float(th + *texCount);
        vert.tex.z = isRadial ? x * texCtm.a + y * texCtm.c + texCtm.tx : 0;
    }
    return vert;
}

fragment float4 opaques_fragment_main(OpaquesVertex vert [[stage_in]], texture2d<float> colorTexture [[texture(1)]])
{
    float x = vert.tex.x, y = vert.tex.y, z = vert.tex.z;
    return colorTexture.sample(cs, float2(z == 0.0 ? x : sqrt(x * x + z * z), y));
}

#pragma mark - Fast Molecules

struct FastMoleculesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4;
};

vertex FastMoleculesVertex fast_molecules_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Segment *segments [[buffer(2)]],
                                const device Transform *ctms [[buffer(4)]],
                                const device Instance *instances [[buffer(5)]],
                                const device Bounds *bounds [[buffer(7)]],
                                const device Point16 *points [[buffer(8)]],
                                constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                uint vid [[vertex_id]], uint iid [[instance_id]])
{
    FastMoleculesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    int segcount = edge.i0, i;
    const device Transform& m = ctms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    const device Point16 *pts = & points[inst.quad.base + (iid - inst.quad.biid) * kFastSegments];
    thread float *dst = & vert.x0;
    float tx, ty, scale, ma, mb, mc, md, x16, y16, slx, sux, sly, suy, x0, y0;
    bool skip = false;
    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;
    
    x16 = pts->x & Point16::kMask, y16 = pts->y & Point16::kMask, pts++;
    x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
    
    *dst++ = slx = sux = x0, *dst++ = sly = suy = y0;

    for (i = 0; i < kFastSegments; i++, dst += 2, pts++) {
        skip |= i >= segcount;
        
        x16 = pts->x & Point16::kMask, y16 = pts->y & Point16::kMask;
        x0 = skip ? x0 : x16 * ma + y16 * mc + tx, slx = min(slx, x0), sux = max(sux, x0);
        y0 = skip ? y0 : x16 * mb + y16 * md + ty, sly = min(sly, y0), suy = max(suy, y0);
        dst[0] = x0, dst[1] = y0;
    }
    
    float ux = edge.ux;
    float dx = clamp(select(floor(slx), ux, vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly), ceil(suy), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, offx = 0.5 - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, offy = 0.5 - dy;
    vert.position = float4(x, y, 1.0, slx == sux && sly == suy ? 0.0 : 1.0);
    for (dst = & vert.x0, i = 0; i < kFastSegments + 1; i++, dst += 2)
        dst[0] += offx, dst[1] += offy;
    return vert;
}

fragment float4 fast_molecules_fragment_main(FastMoleculesVertex vert [[stage_in]])
{
    return lineWinding(vert.x0, vert.y0, vert.x1, vert.y1)
        + lineWinding(vert.x1, vert.y1, vert.x2, vert.y2)
        + lineWinding(vert.x2, vert.y2, vert.x3, vert.y3)
        + lineWinding(vert.x3, vert.y3, vert.x4, vert.y4);
}


#pragma mark - Quad Molecules

struct QuadMoleculesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2;
};

vertex QuadMoleculesVertex quad_molecules_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Segment *segments [[buffer(2)]],
                                const device Transform *ctms [[buffer(4)]],
                                const device Instance *instances [[buffer(5)]],
                                const device Bounds *bounds [[buffer(7)]],
                                const device Point16 *points [[buffer(8)]],
                                constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                uint vid [[vertex_id]], uint iid [[instance_id]])
{
    QuadMoleculesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Transform& m = ctms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    const device Point16 *p = & points[inst.quad.base + ((edge.ic & Edge::ue0) >> 12) + edge.i0];
    const bool isCurve = p->x & Point16::isCurve;
    const float ix0 = p->x & Point16::kMask, iy0 = p->y & Point16::kMask;
    const float ix1 = (p + 1)->x & Point16::kMask, iy1 = (p + 1)->y & Point16::kMask;
    const float ix2 = (p + 2)->x & Point16::kMask, iy2 = (p + 2)->y & Point16::kMask;
    float tx, ty, scale, ma, mb, mc, md, x0, y0, x1, y1, x2, y2, slx, sux, sly, suy;
    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;
    
    x0 = ix0 * ma + iy0 * mc + tx, y0 = ix0 * mb + iy0 * md + ty;
    x1 = ix1 * ma + iy1 * mc + tx, y1 = ix1 * mb + iy1 * md + ty;
    if (isCurve) {
        x2 = ix2 * ma + iy2 * mc + tx, y2 = ix2 * mb + iy2 * md + ty;
        x1 = 2.f * x1 - 0.5f * (x0 + x2), y1 = 2.f * y1 - 0.5f * (y0 + y2);
    } else {
        x2 = x1, x1 = 0.5 * (x0 + x2);
        y2 = y1, y1 = 0.5 * (y0 + y2);
    }
    slx = min(x0, x2), slx = min(x1, slx);
    sly = min(y0, y2), sly = min(y1, sly);
    sux = max(x0, x2), sux = max(x1, sux);
    suy = max(y0, y2), suy = max(y1, suy);
    
    sux = edge.ux;
    float dx = clamp(select(floor(slx), ceil(sux), vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly), ceil(suy), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, offx = 0.5 - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, offy = 0.5 - dy;
    
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 = x0 + offx, vert.y0 = y0 + offy;
    vert.x1 = x1 + offx, vert.y1 = y1 + offy;
    vert.x2 = x2 + offx, vert.y2 = y2 + offy;
    
    return vert;
}

fragment float4 quad_molecules_fragment_main(QuadMoleculesVertex vert [[stage_in]])
{
    return quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2);
}

#pragma mark - Fast & Quad Edges

struct EdgesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5;
};

vertex EdgesVertex edges_vertex_main(const device Edge *edges [[buffer(1)]],
                                     const device Segment *segments [[buffer(2)]],
                                     const device Transform *ctms [[buffer(4)]],
                                     const device Instance *instances [[buffer(5)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant Params *params [[buffer(14)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    EdgesVertex vert;
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Cell& cell = inst.quad.cell;
    thread float *dstFloats = & vert.x0;
    uint32_t ids[2] = { ((edge.ic & Edge::ue0) >> 12) + edge.i0, ((edge.ic & Edge::ue1) >> 8) + edge.ux };
    const thread uint32_t *idxes = & ids[0];
    float slx = cell.ux, sly = FLT_MAX, suy = -FLT_MAX;
    float visible = 1.0;
    float x0, y0, x1, y1, x2, y2;
    
    for (int i = 0; i < 2; i++) {
        if (idxes[i] != 0xFFFFF) {
            const device Segment& s = segments[inst.quad.base + idxes[i]];
            x0 = s.x0, y0 = s.y0;
            bool curve = params->useCurves && s.ix0 & 1;
            if (curve) {
                const device Segment& n = segments[inst.quad.base + idxes[i] + 1];
                x2 = n.x1, y2 = n.y1;
                x1 = s.x1, y1 = s.y1;
                dstFloats[i * 6 + 0] = x0, dstFloats[i * 6 + 1] = y0;
                dstFloats[i * 6 + 2] = x1, dstFloats[i * 6 + 3] = y1;
                dstFloats[i * 6 + 4] = x2, dstFloats[i * 6 + 5] = y2;
                
                float ay, by, cy, ax, bx, d, r, t0, t1;
                ax = x2 - x1, bx = x1 - x0, ax -= bx, bx *= 2.0;
                ay = y2 - y1, by = y1 - y0, ay -= by, by *= 2.0;
                cy = y0 - float(cell.ly), d = by * by - 4.0 * ay * cy, r = sqrt(max(0.0, d));
                t0 = saturate(abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(r, y2 - y0)) / ay * 0.5);
                cy = y0 - float(cell.uy), d = by * by - 4.0 * ay * cy, r = sqrt(max(0.0, d));
                t1 = saturate(abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(r, y2 - y0)) / ay * 0.5);
                if (t0 != t1)
                    slx = min(slx, min(fma(fma(ax, t0, bx), t0, x0), fma(fma(ax, t1, bx), t1, x0)));
            } else {
                x2 = s.x1, y2 = s.y1;
                dstFloats[i * 6 + 0] = x0, dstFloats[i * 6 + 1] = y0;
                dstFloats[i * 6 + 2] = 0.5 * (x0 + x2), dstFloats[i * 6 + 3] = 0.5 * (y0 + y2);
                dstFloats[i * 6 + 4] = x2, dstFloats[i * 6 + 5] = y2;
                
                float m = (x2 - x0) / (y2 - y0), c = x0 - m * y0;
                slx = min(slx, max(min(x0, x2), min(m * clamp(y0, float(cell.ly), float(cell.uy)) + c, m * clamp(y2, float(cell.ly), float(cell.uy)) + c)));
            }
            sly = min(sly, min(y0, y2)), suy = max(suy, max(y0, y2));
        } else {
            dstFloats[i * 6 + 0] = 0.0, dstFloats[i * 6 + 1] = 0.0;
            dstFloats[i * 6 + 2] = 0.0, dstFloats[i * 6 + 3] = 0.0;
            dstFloats[i * 6 + 4] = 0.0, dstFloats[i * 6 + 5] = 0.0;
        }
    }
    float dx = select(max(floor(slx), float(cell.lx)), float(cell.ux), vid & 1);
    float dy = select(max(floor(sly), float(cell.ly)), min(ceil(suy), float(cell.uy)), vid >> 1);
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0;
    vert.position = float4(x, y, 1.0, visible);
    
    for (int i = 0; i < 12; i += 2)
        dstFloats[i] += 0.5 - dx, dstFloats[i + 1] += 0.5 - dy;
    
    return vert;
}

fragment float4 fast_edges_fragment_main(EdgesVertex vert [[stage_in]]) {
    float winding = 0;
    winding += lineWinding(vert.x0, vert.y0, vert.x2, vert.y2);
    winding += lineWinding(vert.x3, vert.y3, vert.x5, vert.y5);
    return winding;
}

fragment float4 quad_edges_fragment_main(EdgesVertex vert [[stage_in]]) {
    float winding = 0;
    winding += quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2);
    winding += quadraticWinding(vert.x3, vert.y3, vert.x4, vert.y4, vert.x5, vert.y5);
    return winding;
}

#pragma mark - Instances

struct InstancesVertex
{
    float4 position [[position]];
    float2 clip;
    float3 tex;
    float u, v, w, cover, alpha;
    float x, y, z;
    uint32_t iz;
};

vertex InstancesVertex instances_vertex_main(
            const device Instance *instances [[buffer(1)]],
            const device Transform *ctms [[buffer(4)]],
            const device Transform *clips [[buffer(5)]],
            const device float *widths [[buffer(6)]],
            const device Bounds *bounds [[buffer(7)]],
            const device Transform *texCtms [[buffer(8)]],
            const device uint32_t *texIdxs [[buffer(9)]],
            constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
            constant uint *pathCount [[buffer(13)]],
            constant uint *texCount [[buffer(14)]],
            constant Params *params [[buffer(15)]],
            uint vid [[vertex_id]], uint iid [[instance_id]])
{
    InstancesVertex vert;
    constexpr float err = 1e-3;
    const bool isRight = vid & 1, isTop = vid & 2;
    const float sign = isRight ? -1.0 : 1.0;
    const device Instance& inst = instances[iid];
    uint iz = inst.iz & kPathIndexMask, flags = inst.iz & Instance::kFragmentMask;
    const bool isGradient = inst.iz & Instance::kIsGradient;
    const bool isRadial = inst.iz & Instance::kIsRadial;
    
    const device Transform& clip = clips[iz];
    const device Transform& texCtm = texCtms[iz];
    
    float w = widths[iz], cw = max(1.0, w), dw = 0.5 * (1.0 + cw);
    float alpha = select(1.0, w / cw, w != 0), dx, dy;
    if (inst.iz & Instance::kOutlines) {
        const bool roundCap = w > 1.0 && inst.iz & Instance::kRoundCap;
        const bool squareCap = inst.iz & Instance::kSquareCap;
        const bool roundJoin = inst.iz & Instance::kRoundJoin;
        const short prevIndex = inst.outline.prev, nextIndex = inst.outline.next;
        const device Instance & pinst = instances[iid + prevIndex], & ninst = instances[iid + nextIndex];
        const device Quadratic& p = pinst.outline.quad, & o = inst.outline.quad, & n = ninst.outline.quad;
        const bool pcurve = params->useCurves && p.x1 != FLT_MAX;
        const bool ncurve = params->useCurves && n.x1 != FLT_MAX;
        
        float x0, y0, x1, y1, x2, y2;
        float ax, bx, cx, ay, by, cy, ow, lcap;
        bool pcap = prevIndex == 0 || p.x2 != o.x0 || p.y2 != o.y0;
        bool ncap = nextIndex == 0 || n.x0 != o.x2 || n.y0 != o.y2;
        x0 = o.x0, y0 = o.y0, x1 = o.x1, y1 = o.y1, x2 = o.x2, y2 = o.y2;
        
        float px0, py0, pdot, nx1, ny1, ndot;
        px0 = x0 - (pcurve ? p.x1 : p.x0);
        py0 = y0 - (pcurve ? p.y1 : p.y0);
        nx1 = (ncurve ? n.x1 : n.x2) - x2;
        ny1 = (ncurve ? n.y1 : n.y2) - y2;
        
        ax = x1 - x2, bx = x1 - x0, cx = x2 - x0;
        ay = y1 - y2, by = y1 - y0, cy = y2 - y0;
        float cdot = cx * cx + cy * cy, rc = rsqrt(cdot);
        float area = cx * by - cy * bx;
        float tc = abs(area / cdot);
        
        const bool isCurve = params->useCurves && x1 != FLT_MAX && tc > 1e-3;
        ow = isCurve ? 0.5 * tc / rc : 0.0;
        
        lcap = (isCurve ? 0.41 * dw : 0.0) + (squareCap || roundCap ? dw : 0.5);
        float caplimit = dw == 1.0 ? 0.0 : -0.866025403784439;
    
        pdot = px0 * px0 + py0 * py0;
        ndot = nx1 * nx1 + ny1 * ny1;
        
        float2 no = float2(cx, cy) * rc, prev, next, tangent, miter0, miter1;
        
        next = normalize({ (isCurve ? x1 : x2) - x0, (isCurve ? y1 : y2) - y0 });
        prev = rsqrt(pdot) * float2(px0, py0);
        pcap = pcap || pdot < 1e-6 || dot(prev, next) < caplimit;
        tangent = pcap ? no : normalize(prev + next);
        miter0 = (dw + ow) / abs(dot(no, tangent)) * float2(-tangent.y, tangent.x);
        
        prev = normalize({ x2 - (isCurve ? x1 : x0), y2 - (isCurve ? y1 : y0) });
        next = rsqrt(ndot) * float2(nx1, ny1);
        ncap = ncap || ndot < 1e-6 || dot(prev, next) < caplimit;
        tangent = ncap ? no : normalize(prev + next);
        miter1 = (dw + ow) / abs(dot(no, tangent)) * float2(-tangent.y, tangent.x);
                
        float lp, cx0, cy0, ln, cx1, cy1, t, dt, rt;
        lp = select(0.0, lcap, pcap) + err, cx0 = x0 - no.x * lp, cy0 = y0 - no.y * lp;
        ln = select(0.0, lcap, ncap) + err, cx1 = x2 + no.x * ln, cy1 = y2 + no.y * ln;
        t = ((cx1 - cx0) * miter1.y - (cy1 - cy0) * miter1.x) / (miter0.x * miter1.y - miter0.y * miter1.x);
        rt = sign * t, dt = sign * (rt < 0.0 ? 1.0 : min(1.0, rt));
        dx = isTop ? fma(miter1.x, dt, cx1) : fma(miter0.x, dt, cx0);
        dy = isTop ? fma(miter1.y, dt, cy1) : fma(miter0.y, dt, cy0);
        
        if (!isCurve) {
            vert.u = (dx - x0) * -no.y + (dy - y0) * no.x;
            float offset = roundCap ? 0.0 : squareCap ? dw : 0.5;
            float d0 = (dx - x0) * no.x + (dy - y0) * no.y;
            float d1 = (dx - x2) * -no.x + (dy - y2) * -no.y;
            vert.w = pcap ? offset + d0 : roundJoin ? d0 : FLT_MAX;
            vert.z = ncap ? offset + d1 : roundJoin ? d1 : FLT_MAX;
            
            flags = flags | ((pcap && roundCap) || (!pcap && roundJoin)) * Instance::kF0;
            flags = flags | ((ncap && roundCap) || (!ncap && roundJoin)) * Instance::kF1;
        } else
        {
            vert.u = x0 - dx, vert.v = x1 - dx, vert.w = x2 - dx;
            vert.x = y0 - dy, vert.y = y1 - dy, vert.z = y2 - dy;
            
            flags = flags | Instance::kIsCurve;
            flags = flags | (pcap ? !roundCap : !roundJoin) * Instance::kF0;
            flags = flags | (ncap ? !roundCap : !roundJoin) * Instance::kF1;
        }
        vert.cover = dw;
        flags = flags | pcap * Instance::kPCap | ncap * Instance::kNCap;
    } else {
        const device Cell& cell = inst.quad.cell;
        dx = isRight ? cell.ux : cell.lx;
        dy = isTop ? cell.uy : cell.ly;
        vert.u = cell.ox == kNullIndex ? FLT_MAX : (dx - (cell.lx - cell.ox)) / *width;
        vert.v = 1.0 - (dy - (cell.ly - cell.oy)) / *height;
        vert.cover = inst.quad.cover;
    }
    float x = dx / *width * 2.0 - 1.0, y = dy / *height * 2.0 - 1.0;
    float z = kDepthRange * float(iz + 1) / float(*pathCount);
    vert.position = float4(x, y, z, 1.0);
    vert.clip = params->useClips ? float2(dx * clip.a + dy * clip.c + clip.tx, dx * clip.b + dy * clip.d + clip.ty) : 0.5;
    vert.alpha = alpha;
    vert.iz = iz | flags;
    
    int tw = kColorTextureWidth, th = (*pathCount + tw - 0) / tw;
    if (isGradient) {
        vert.tex.x = dx * texCtm.b + dy * texCtm.d + texCtm.ty;
        vert.tex.y = (0.5 + (th + texIdxs[iz])) / float(th + *texCount);
        vert.tex.z = isRadial ? dx * texCtm.a + dy * texCtm.c + texCtm.tx : 0;
    } else {
        vert.tex.x = (0.5 + (iz % tw)) / float(tw);
        vert.tex.y = (0.5 + (iz / tw)) / float(th + *texCount);
        vert.tex.z = 0.0;
    }
    
    return vert;
}

fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]],
                                        texture2d<float> accumulation [[texture(0)]],
                                        texture2d<float> colorTexture [[texture(1)]]
)
{
    float alpha = 1.0;
    if (vert.iz & Instance::kOutlines) {
        bool isCurve = vert.iz & Instance::kIsCurve;
        bool squareCap = vert.iz & Instance::kSquareCap;
        bool pcap = vert.iz & Instance::kPCap, ncap = vert.iz & Instance::kNCap;
        bool f0 = vert.iz & Instance::kF0, f1 = vert.iz & Instance::kF1;
        const float dw = vert.cover;
        
        if (!isCurve) {
            float dx = vert.u, dy = min(vert.w, vert.z), ry = min(0.0, dy);
            float rect = saturate(dw - abs(dx)) * saturate(dy);
            float lozenge = saturate(dw - sqrt(dx * dx + ry * ry));
            alpha = vert.w < vert.z ? (f0 ? lozenge : rect) : (f1 ? lozenge : rect);
        } else
        {
            float x0, y0, x1, y1, x2, y2, d0, dm0, d1, dm1, sqdist, sd0, sd1, cap, cap0, cap1;
                
            x0 = vert.u, x1 = vert.v, x2 = vert.w;
            y0 = vert.x, y1 = vert.y, y2 = vert.z;
            
            sqdist = sqBezier(float2(x0, y0), float2(x1, y1), float2(x2, y2));
            float outline = saturate(dw - sqrt(sqdist));
            
            cap = squareCap ? dw : 0.5;
            
            float bx = x0 - x1, by = y0 - y1, bdot = bx * bx + by * by, rb = rsqrt(bdot);
            d0 = (x0 * bx + y0 * by) * rb, dm0 = (x0 * -by + y0 * bx) * rb;
            cap0 = (pcap ? saturate(cap + d0) : 1.0) * saturate(dw - abs(dm0));
            sd0 = f0 ? saturate(d0) : 1.0;
            
            float ax = x2 - x1, ay = y2 - y1, adot = ax * ax + ay * ay, ra = rsqrt(adot);
            d1 = (x2 * ax + y2 * ay) * ra, dm1 = (x2 * -ay + y2 * ax) * ra;
            cap1 = (ncap ? saturate(cap + d1) : 1.0) * saturate(dw - abs(dm1));
            sd1 = f1 ? saturate(d1) : 1.0;
            
            alpha = cap0 * (1.0 - sd0) + cap1 * (1.0 - sd1) + (sd0 + sd1 - 1.0) * outline;
        }
    } else
    if (vert.u != FLT_MAX) {
        float cover = abs(vert.cover + accumulation.sample(s, float2(vert.u, vert.v)).x);
        alpha = vert.iz & Instance::kEvenOdd ? 1.0 - abs(fmod(cover, 2.0) - 1.0) : min(1.0, cover);
    }
    float clx = vert.clip.x, cly = vert.clip.y, a = dfdx(clx), b = dfdy(clx), c = dfdx(cly), d = dfdy(cly);
    float sx = rsqrt(a * a + b * b), sy = rsqrt(c * c + d * d);
    float clip = saturate(0.5 + clx * sx) * saturate(0.5 + (1.0 - clx) * sx) * saturate(0.5 + cly * sy) * saturate(0.5 + (1.0 - cly) * sy);
    
    float x = vert.tex.x, y = vert.tex.y, z = vert.tex.z;
    return alpha * vert.alpha * clip * colorTexture.sample(cs, float2(z == 0.0 ? x : sqrt(x * x + z * z), y));
}
