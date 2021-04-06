//
//  Shaders.metal
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.h"
#include <metal_stdlib>
using namespace metal;

constexpr sampler s = sampler(coord::normalized, address::clamp_to_zero, mag_filter::nearest, min_filter::nearest, mip_filter::linear);

struct Transform {
    Transform() : a(1), b(0), c(0), d(1), tx(0), ty(0) {}
    float a, b, c, d, tx, ty;
};

struct Bounds {
    float lx, ly, ux, uy;
};

struct Colorant {
    uint8_t b, g, r, a;
};

struct Point16 {
    uint16_t x, y;
};
struct Segment {
    float x0, y0, x1, y1;
};

struct Cell {
    uint16_t lx, ly, ux, uy, ox, oy;
};

struct Quad {
    Cell cell;  short cover;  int base;
};
struct Outline {
    Segment s;
    short prev, next;
};
struct Instance {
    enum Type { kPCurve = 1 << 24, kEvenOdd = 1 << 24, kRoundCap = 1 << 25, kEdge = 1 << 26, kNCurve = 1 << 27, kSquareCap = 1 << 28, kOutlines = 1 << 29, kFastEdges = 1 << 30, kMolecule = 1 << 31 };
    uint32_t iz;  union { Quad quad;  Outline outline; };
};
struct Edge {
    uint32_t ic;  enum Flags { a0 = 1 << 31, a1 = 1 << 30, kMask = ~(a0 | a1) };
    uint16_t i0, ux;
};

float4 distances(Transform ctm, float dx, float dy) {
    float det, rlab, rlcd, d0, d1;
    det = ctm.a * ctm.d - ctm.b * ctm.c;
    rlab = copysign(rsqrt(ctm.a * ctm.a + ctm.b * ctm.b), det);
    rlcd = copysign(rsqrt(ctm.c * ctm.c + ctm.d * ctm.d), det);
    d0 = ((ctm.tx - dx) * ctm.b - (ctm.ty - dy) * ctm.a) * rlab;
    d1 = ((ctm.tx + ctm.a - dx) * ctm.d - (ctm.ty + ctm.b - dy) * ctm.c) * rlcd;
    return { 0.5 + d0, 0.5 + d1, 0.5 - d0 + det * rlab, 0.5 - d1 + det * rlcd };
}
float closestT(float x0, float y0, float x1, float y1, float x2, float y2) {
    float ax, bx, ay, by, t0, t1, t, d2, d4, d8, d, d0, d1, x, y, tx, ty;
    ax = x2 - x1, bx = x1 - x0, ax -= bx, bx *= 2.0, ay = y2 - y1, by = y1 - y0, ay -= by, by *= 2.0;
    d2 = -((x0 + x2 + 2.0 * x1) * (x2 - x0) + (y0 + y2 + 2.0 * y1) * (y2 - y0));
    t0 = select(0.5, -0.0, d2 < 0.0), t1 = select(1.0, 0.5, d2 < 0.0), t = 0.5 * (t0 + t1);
    tx = fma(2.0 * ax, t, bx), x = fma(fma(ax, t, bx), t, x0), ty = fma(2.0 * ay, t, by), y = fma(fma(ay, t, by), t, y0);
    d4 = -(x * tx + y * ty);
    t0 = select(t, t0, d4 < 0.0), t1 = select(t1, t, d4 < 0.0), t = 0.5 * (t0 + t1);
    tx = fma(2.0 * ax, t, bx), x = fma(fma(ax, t, bx), t, x0), ty = fma(2.0 * ay, t, by), y = fma(fma(ay, t, by), t, y0);
    d8 = -(x * tx + y * ty) * rsqrt(tx * tx + ty * ty);
    t0 = select(t, t0, d8 < 0.0), t1 = select(t1, t, d8 < 0.0), t = select(t1, t0, d8 < 0.0);
    tx = fma(2.0 * ax, t, bx), x = fma(fma(ax, t, bx), t, x0), ty = fma(2.0 * ay, t, by), y = fma(fma(ay, t, by), t, y0);
    d = -(x * tx + y * ty) * rsqrt(tx * tx + ty * ty);
    d0 = select(d8, d, d8 < 0.0), d1 = select(d, d8, d8 < 0.0), t = d0 / (d0 - d1);
    return (1.0 - t) * t0 + t * t1;
}
float roundedDistance(float x0, float y0, float x1, float y1) {
    float ax = x1 - x0, ay = y1 - y0, t = saturate(-(ax * x0 + ay * y0) / (ax * ax + ay * ay)), x = fma(ax, t, x0), y = fma(ay, t, y0);
    return x * x + y * y;
}
float roundedDistance(float x0, float y0, float x1, float y1, float x2, float y2) {
    if (x1 == FLT_MAX)
        return roundedDistance(x0, y0, x2, y2);
    float t = saturate(closestT(x0, y0, x1, y1, x2, y2)), s = 1.0 - t;
    return roundedDistance(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2);
}

float winding(float x0, float y0, float x1, float y1, float w0, float w1, float cover) {
    float dx, dy, a0, t, b, f;
    dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.0 ? w0 : w1) - y0) - dy * (1.0 - x0);
    dx = abs(dx), t = -a0 / fma(dx, cover, dy), dy = abs(dy);
    b = max(dx, dy), f = b / (b - min(dx, dy) * 0.4142135624);
    return saturate((t - 0.5) * f + 0.5) * cover;
}
float fastWinding(float x0, float y0, float x1, float y1) {
    float w0 = saturate(y0), w1 = saturate(y1), cover = w1 - w0;
    if ((x0 <= 0.0 && x1 <= 0.0) || cover == 0.0)
        return cover;
    return winding(x0, y0, x1, y1, w0, w1, cover);
}
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2, bool a, float iy) {
    if (x1 == FLT_MAX)
        return fastWinding(x0, y0, x2, y2);
    float w0 = saturate(a ? y0 : iy), w1 = saturate(a ? iy : y2), cover = w1 - w0, ay, by, cy, t, s;
    if (cover == 0.0 || (x0 <= 0.0 && x1 <= 0.0 && x2 <= 0.0))
        return cover;
    ay = y0 + y2 - y1 - y1, by = 2.0 * (y1 - y0), cy = y0 - 0.5 * (w0 + w1);
    t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(sqrt(max(0.0, by * by - 4.0 * ay * cy)), cover)) / ay * 0.5, s = 1.0 - t;
    return winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w0, w1, cover);
}
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2) {
    if (x1 == FLT_MAX)
        return fastWinding(x0, y0, x2, y2);
    float w0 = saturate(y0), w2 = saturate(y2);
    if (x0 <= 0.0 && x1 <= 0.0 && x2 <= 0.0)
        return w2 - w0;
    float w = 0.0, ay = y2 - y1, by = y1 - y0, cy, t, s, w1;
    bool mono = abs(ay) < kMonotoneFlatness || abs(by) < kMonotoneFlatness || (ay > 0.0) == (by > 0.0);
    w1 = saturate(mono ? y2 : y0 - by * by / (ay - by)), ay -= by, by *= 2.0;
    if (w0 != w1) {
        cy = y0 - 0.5 * (w0 + w1);
        t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(sqrt(max(0.0, by * by - 4.0 * ay * cy)), w1 - w0)) / ay * 0.5;
        s = 1.0 - t, w += winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w0, w1, w1 - w0);
    }
    if (w1 != w2) {
        cy = y0 - 0.5 * (w1 + w2);
        t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(sqrt(max(0.0, by * by - 4.0 * ay * cy)), w2 - w1)) / ay * 0.5;
        s = 1.0 - t, w += winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w1, w2, w2 - w1);
    }
    return w;
}

#pragma mark - Opaques

struct OpaquesVertex
{
    float4 position [[position]];
    float4 color;
};

vertex OpaquesVertex opaques_vertex_main(const device Colorant *colors [[buffer(0)]],
                                         const device Instance *instances [[buffer(1)]],
                                         constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                         constant uint *reverse [[buffer(12)]], constant uint *pathCount [[buffer(13)]],
                                         uint vid [[vertex_id]], uint iid [[instance_id]])
{
    const device Instance& inst = instances[*reverse - 1 - iid];
    const device Colorant& color = colors[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    OpaquesVertex vert;
    vert.position = {
        select(cell.lx, cell.ux, vid & 1) / *width * 2.0 - 1.0,
        select(cell.ly, cell.uy, vid >> 1) / *height * 2.0 - 1.0,
        ((inst.iz & kPathIndexMask) * 2 + 2) / float(*pathCount * 2 + 2),
        1.0
    };
    vert.color = { color.r / 255.0, color.g / 255.0, color.b / 255.0, 1.0 };
    return vert;
}

fragment float4 opaques_fragment_main(OpaquesVertex vert [[stage_in]])
{
    return vert.color;
}

#pragma mark - Fast Molecules

struct FastMoleculesVertex
{
    float4 position [[position]];
    float dw, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4;
};

vertex FastMoleculesVertex fast_molecules_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Segment *segments [[buffer(2)]],
                                const device Transform *affineTransforms [[buffer(4)]],
                                const device Instance *instances [[buffer(5)]],
                                const device float *widths [[buffer(6)]],
                                const device Bounds *bounds [[buffer(7)]],
                                const device Point16 *points [[buffer(8)]],
                                constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                constant bool *useCurves [[buffer(14)]],
                                uint vid [[vertex_id]], uint iid [[instance_id]])
{
    FastMoleculesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Transform& m = affineTransforms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    const device Point16 *pts = & points[inst.quad.base + edge.i0];
    thread float *dst = & vert.x0;
    float w = widths[inst.iz & kPathIndexMask], cw = max(1.0, w), dw = (w != 0.0) * 0.5 * (cw + 1.0);
    bool skip = false;
    int i;
    float _tx, _ty, ma, mb, mc, md, x16, y16, slx, sux, sly, suy;
    _tx = b.lx * m.a + b.ly * m.c + m.tx, _ty = b.lx * m.b + b.ly * m.d + m.ty;
    ma = m.a * (b.ux - b.lx) / 16383.0, mb = m.b * (b.ux - b.lx) / 16383.0;
    mc = m.c * (b.uy - b.ly) / 32767.0, md = m.d * (b.uy - b.ly) / 32767.0;
    x16 = pts->x & 0x3FFF, y16 = pts->y & 0x7FFF, pts++;
    *dst++ = slx = sux = x16 * ma + y16 * mc + _tx,
    *dst++ = sly = suy = x16 * mb + y16 * md + _ty;
    for (i = 0; i < kFastSegments; i++, dst += 2) {
        skip |= (w != 0.0 && (pts[-1].x & 0x4000)) || (pts->x == 0xFFFF && pts->y == 0xFFFF);
        
        x16 = pts->x & 0x3FFF, y16 = pts->y & 0x7FFF, pts++;
        dst[0] = select(x16 * ma + y16 * mc + _tx, dst[-2], skip), dst[1] = select(x16 * mb + y16 * md + _ty, dst[-1], skip);
        slx = min(slx, dst[0]), sux = max(sux, dst[0]), sly = min(sly, dst[1]), suy = max(suy, dst[1]);
    }
    float ux = select(float(edge.ux), ceil(sux + dw), dw != 0.0), offset = select(0.5, 0.0, dw != 0.0);
    float dx = clamp(select(floor(slx - dw), ux, vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly - dw), ceil(suy + dw), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, tx = offset - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, ty = offset - dy;
    vert.position = float4(x, y, 1.0, slx == sux && sly == suy ? 0.0 : 1.0);
    for (dst = & vert.x0, i = 0; i < kFastSegments + 1; i++, dst += 2)
        dst[0] += tx, dst[1] += ty;
    vert.dw = dw;
    return vert;
}

fragment float4 fast_outlines_fragment_main(FastMoleculesVertex vert [[stage_in]])
{
    float d = min(
                  min(roundedDistance(vert.x0, vert.y0, vert.x1, vert.y1), roundedDistance(vert.x1, vert.y1, vert.x2, vert.y2)),
                  min(roundedDistance(vert.x2, vert.y2, vert.x3, vert.y3), roundedDistance(vert.x3, vert.y3, vert.x4, vert.y4))
                  );
    return saturate(vert.dw - sqrt(d));
}

fragment float4 fast_molecules_fragment_main(FastMoleculesVertex vert [[stage_in]])
{
//    return 0.2;
    return fastWinding(vert.x0, vert.y0, vert.x1, vert.y1)
        + fastWinding(vert.x1, vert.y1, vert.x2, vert.y2)
        + fastWinding(vert.x2, vert.y2, vert.x3, vert.y3)
        + fastWinding(vert.x3, vert.y3, vert.x4, vert.y4);
}

#pragma mark - Quad Molecules

struct QuadMoleculesVertex
{
    float4 position [[position]];
    float dw, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8;
};

vertex QuadMoleculesVertex quad_molecules_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Segment *segments [[buffer(2)]],
                                const device Transform *affineTransforms [[buffer(4)]],
                                const device Instance *instances [[buffer(5)]],
                                const device float *widths [[buffer(6)]],
                                const device Bounds *bounds [[buffer(7)]],
                                const device Point16 *points [[buffer(8)]],
                                constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                uint vid [[vertex_id]], uint iid [[instance_id]])
{
    QuadMoleculesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Transform& m = affineTransforms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    const device Point16 *pts = & points[inst.quad.base + edge.i0];
    thread float *dst = & vert.x0;
    float w = widths[inst.iz & kPathIndexMask], cw = max(1.0, w), dw = (w != 0.0) * 0.5 * (cw + 1.0);
    bool skip = (w != 0.0 && (pts->x & 0x4000)) || ((pts + 1)->x == 0xFFFF && (pts + 1)->y == 0xFFFF);
    int i, curve0, curve1, curve2;
    float slx = 0.0, sux = 0.0, sly = 0.0, suy = 0.0, visible = skip ? 0.0 : 1.0;
    if (visible) {
        float tx, ty, ma, mb, mc, md, x, y, px, py, x0, y0, x1, y1, nx, ny, cpx, cpy;
        tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
        ma = m.a * (b.ux - b.lx) / 16383.0, mb = m.b * (b.ux - b.lx) / 16383.0;
        mc = m.c * (b.uy - b.ly) / 32767.0, md = m.d * (b.uy - b.ly) / 32767.0;
        
        curve0 = ((pts->x & 0x8000) >> 14) | ((pts->y & 0x8000) >> 15);
        curve1 = (((pts + 1)->x & 0x8000) >> 14) | (((pts + 1)->y & 0x8000) >> 15);
        x = curve0 == 2 ? (pts - 1)->x & 0x3FFF : 0, y = curve0 == 2 ? (pts - 1)->y & 0x7FFF : 0;
        px = x * ma + y * mc + tx, py = x * mb + y * md + ty;
        x = pts->x & 0x3FFF, y = pts->y & 0x7FFF, pts++;
        x0 = x * ma + y * mc + tx, y0 = x * mb + y * md + ty;
        x = pts->x & 0x3FFF, y = pts->y & 0x7FFF, pts++;
        x1 = x * ma + y * mc + tx, y1 = x * mb + y * md + ty;
        
        dst[0] = slx = sux = x0, dst[1] = sly = suy = y0, dst += 2;
        
        for (i = 0; i < kFastSegments; i++, dst += 4, px = x0, x0 = x1, x1 = nx, py = y0, y0 = y1, y1 = ny, curve0 = curve1, curve1 = curve2, pts++) {
            if (x1 == FLT_MAX)
                dst[0] = FLT_MAX, dst[2] = dst[-2], dst[3] = dst[-1];
            else {
                curve2 = ((pts->x & 0x8000) >> 14) | ((pts->y & 0x8000) >> 15);
                x = pts->x & 0x3FFF, y = pts->y & 0x7FFF;
                nx = x * ma + y * mc + tx, ny = x * mb + y * md + ty;
                nx = x1 == FLT_MAX || (pts->x == 0xFFFF && pts->y == 0xFFFF) ? FLT_MAX : nx;
                nx = w != 0.0 && ((pts - 1)->x & 0x4000) != 0 ? FLT_MAX : nx;
                slx = min(slx, x1), sux = max(sux, x1), sly = min(sly, y1), suy = max(suy, y1);
                if (curve0 == 0)
                    dst[0] = FLT_MAX;
                else {
                    cpx = curve0 == 1 ? 0.25f * (x0 - nx) + x1 : 0.25f * (x1 - px) + x0;
                    cpy = curve0 == 1 ? 0.25f * (y0 - ny) + y1 : 0.25f * (y1 - py) + y0;
                    slx = min(slx, cpx), sux = max(sux, cpx), sly = min(sly, cpy), suy = max(suy, cpy);
                    if (abs((cpx - x0) * (y1 - cpy) - (cpy - y0) * (x1 - cpx)) < 1.0)
                        dst[0] = FLT_MAX;
                    else
                        dst[0] = cpx, dst[1] = cpy;
                }
                dst[2] = x1, dst[3] = y1;
            }
        }
    }
    float ux = select(float(edge.ux), ceil(sux + dw), dw != 0.0), offset = select(0.5, 0.0, dw != 0.0);
    float dx = clamp(select(floor(slx - dw), ux, vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly - dw), ceil(suy + dw), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, tx = offset - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, ty = offset - dy;
    vert.position = float4(x, y, 1.0, visible);
    vert.dw = dw;
    for (dst = & vert.x0, i = 0; i < kFastSegments + 1; i++, dst += 4)
        dst[0] += tx, dst[1] += ty;
    for (dst = & vert.x1, i = 0; i < kFastSegments; i++, dst += 4)
        if (dst[0] != FLT_MAX)
            dst[0] += tx, dst[1] += ty;
    return vert;
}

fragment float4 quad_outlines_fragment_main(QuadMoleculesVertex vt [[stage_in]])
{
    float d = min(
                  min(roundedDistance(vt.x0, vt.y0, vt.x1, vt.y1, vt.x2, vt.y2),
                      roundedDistance(vt.x2, vt.y2, vt.x3, vt.y3, vt.x4, vt.y4)),
                  min(roundedDistance(vt.x4, vt.y4, vt.x5, vt.y5, vt.x6, vt.y6),
                      roundedDistance(vt.x6, vt.y6, vt.x7, vt.y7, vt.x8, vt.y8))
                  );
    return saturate(vt.dw - sqrt(d));
}

fragment float4 quad_molecules_fragment_main(QuadMoleculesVertex vert [[stage_in]])
{
//    return 0.2;
    return quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2)
    + quadraticWinding(vert.x2, vert.y2, vert.x3, vert.y3, vert.x4, vert.y4)
    + quadraticWinding(vert.x4, vert.y4, vert.x5, vert.y5, vert.x6, vert.y6)
    + quadraticWinding(vert.x6, vert.y6, vert.x7, vert.y7, vert.x8, vert.y8);
}

#pragma mark - Fast & Quad Edges

struct EdgesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5;
    float iy0, iy1;
    bool a0, a1;
};

vertex EdgesVertex edges_vertex_main(const device Edge *edges [[buffer(1)]],
                                     const device Segment *segments [[buffer(2)]],
                                     const device Transform *affineTransforms [[buffer(4)]],
                                     const device Instance *instances [[buffer(5)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant bool *useCurves [[buffer(14)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    EdgesVertex vert;
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Cell& cell = inst.quad.cell;
    vert.a0 = edge.ic & Edge::a0, vert.a1 = edge.ic & Edge::a1;
    thread float *dst = & vert.x0;
    thread float *iys = & vert.iy0;
    thread bool *as = & vert.a0;
    const device uint16_t *idxes = & edge.i0;
    float slx = cell.ux, sly = FLT_MAX, suy = -FLT_MAX;
    for (int i = 0; i < 2; i++, dst += 6) {
        float x0, y0, x1, y1, x2, y2;
        if (idxes[i] != kNullIndex) {
            const device Segment& s = segments[inst.quad.base + idxes[i]];
            x0 = dst[0] = s.x0, y0 = dst[1] = s.y0, x2 = dst[4] = s.x1, y2 = dst[5] = s.y1;
            bool pcurve = *useCurves && as_type<uint>(x0) & 2, ncurve = *useCurves && as_type<uint>(x0) & 1;
            if (pcurve) {
                const device Segment& p = segments[inst.quad.base + idxes[i] - 1];
                x1 = x0 + 0.25 * (x2 - p.x0), y1 = y0 + 0.25 * (y2 - p.y0);
            } else if (ncurve) {
                const device Segment& n = segments[inst.quad.base + idxes[i] + 1];
                x1 = x2 + 0.25 * (x0 - n.x1), y1 = y2 + 0.25 * (y0 - n.y1);
            }
            if (!pcurve && !ncurve)
                dst[2] = FLT_MAX;
            else
                dst[2] = x1, dst[3] = y1;
            sly = min(sly, min(y0, y2)), suy = max(suy, max(y0, y2));
            if (dst[2] == FLT_MAX) {
                float m = (x2 - x0) / (y2 - y0), c = x0 - m * y0;
                slx = min(slx, max(min(x0, x2), min(m * clamp(y0, float(cell.ly), float(cell.uy)) + c, m * clamp(y2, float(cell.ly), float(cell.uy)) + c)));
            } else {
                float ay, by, cy, iy, ax, bx, tx, x, y, d, r, t0, t1, sign;  bool mono;
                ay = y2 - y1, by = y1 - y0, mono = abs(ay) < kMonotoneFlatness || abs(by) < kMonotoneFlatness || (ay > 0.0) == (by > 0.0);
                iy = mono ? y2 : y0 - by * by / (ay - by);
                iys[i] = iy, sly = min(sly, iy), suy = max(suy, iy);
                
                sign = as[i] ? iy - y0 : y2 - iy;
                ay -= by, by *= 2.0, ax = x0 + x2 - x1 - x1, bx = 2.0 * (x1 - x0), tx = -bx / ax * 0.5;
                cy = y0 - float(cell.ly), d = by * by - 4.0 * ay * cy, r = sqrt(max(0.0, d));
                t0 = saturate(abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(r, sign)) / ay * 0.5);
                cy = y0 - float(cell.uy), d = by * by - 4.0 * ay * cy, r = sqrt(max(0.0, d));
                t1 = saturate(abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(r, sign)) / ay * 0.5);
                if (t0 != t1)
                    slx = min(slx, min(fma(fma(ax, t0, bx), t0, x0), fma(fma(ax, t1, bx), t1, x0)));
                x = fma(fma(ax, tx, bx), tx, x0), y = fma(fma(ay, tx, by), tx, y0);
                slx = min(slx, tx > 0.0 && tx < 1.0 && y > cell.ly && y < cell.uy ? x : x0);
            }
        } else
            dst[0] = 0.0, dst[1] = 0.0, dst[2] = FLT_MAX, dst[4] = 0.0, dst[5] = 0.0;
    }
    float dx = select(max(floor(slx), float(cell.lx)), float(cell.ux), vid & 1);
    float dy = select(max(floor(sly), float(cell.ly)), min(ceil(suy), float(cell.uy)), vid >> 1);
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, tx = 0.5 - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, ty = 0.5 - dy;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 += tx, vert.y0 += ty, vert.x2 += tx, vert.y2 += ty;
    vert.x3 += tx, vert.y3 += ty, vert.x5 += tx, vert.y5 += ty;
    if (vert.x1 != FLT_MAX)
        vert.x1 += tx, vert.y1 += ty, vert.iy0 += ty;
    if (vert.x4 != FLT_MAX)
        vert.x4 += tx, vert.y4 += ty, vert.iy1 += ty;
    return vert;
}

fragment float4 fast_edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return fastWinding(vert.x0, vert.y0, vert.x2, vert.y2) + fastWinding(vert.x3, vert.y3, vert.x5, vert.y5);
}

fragment float4 quad_edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2, vert.a0, vert.iy0)
        + quadraticWinding(vert.x3, vert.y3, vert.x4, vert.y4, vert.x5, vert.y5, vert.a1, vert.iy1);
}

#pragma mark - Instances

struct InstancesVertex
{
    enum Flags { kPCap = 1 << 0, kNCap = 1 << 1, kIsCurve = 1 << 2, kIsShape = 1 << 3 };
    float4 position [[position]];
    float4 color, clip;
    float u, v, cover, dw, d0, d1, dm, miter0, miter1;
    uint32_t flags;
};

struct Curve {
    float x0, y0, x1, y1, cpx, cpy;
    bool isCurve, pcurve, ncurve;
    void unpack(const device Segment& p, const device Instance& inst, const device Segment& n, bool useCurves) {
        const device Segment& o = inst.outline.s;
        pcurve = useCurves && (inst.iz & Instance::kPCurve) != 0, ncurve = useCurves && (inst.iz & Instance::kNCurve) != 0;
        float ax, bx, ay, by, t, s, cx0, cy0, cx2, cy2, x, y, r, tx, ty;
        cx0 = select(o.x0, p.x0, pcurve), x = select(o.x1, o.x0, pcurve), cx2 = select(n.x1, o.x1, pcurve);
        cy0 = select(o.y0, p.y0, pcurve), y = select(o.y1, o.y0, pcurve), cy2 = select(n.y1, o.y1, pcurve);
        cpx = 2.0 * x - 0.5 * (cx0 + cx2), cpy = 2.0 * y - 0.5 * (cy0 + cy2);
        ax = cx2 - cpx, bx = cpx - cx0, ay = cy2 - cpy, by = cpy - cy0;
        r = rsqrt((ax * ax + ay * ay) / (bx * bx + by * by)), tx = fma(ax, r, bx), ty = fma(ay, r, by);
        isCurve = (pcurve || ncurve) && r < 1e1 && r > 1e-1;
        ax -= bx, bx *= 2.0, ay -= by, by *= 2.0;
        t = -0.5 * (tx * by - ty * bx) / (tx * ay - ty * ax), s = 1.0 - t;
//        s = t = 0.5;
        x = fma(fma(ax, t, bx), t, cx0), y = fma(fma(ay, t, by), t, cy0);
        x0 = select(o.x0, x, pcurve && isCurve), x1 = select(o.x1, x, ncurve && isCurve), cpx = select(s * cx0 + t * cpx, s * cpx + t * cx2, pcurve);
        y0 = select(o.y0, y, pcurve && isCurve), y1 = select(o.y1, y, ncurve && isCurve), cpy = select(s * cy0 + t * cpy, s * cpy + t * cy2, pcurve);
    }
};

vertex void instances_transform_main(
            device Instance *instances [[buffer(1)]],
            device Segment *segments [[buffer(20)]],
            uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Instance& inst = instances[iid];
    if (inst.iz & Instance::kOutlines) {
        segments[iid] = inst.outline.s;
    }
}
vertex InstancesVertex instances_vertex_main(
            const device Colorant *colors [[buffer(0)]],
            const device Instance *instances [[buffer(1)]],
            const device Transform *clips [[buffer(5)]],
            const device float *widths [[buffer(6)]],
            constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
            constant uint *pathCount [[buffer(13)]],
            constant bool *useCurves [[buffer(14)]],
            uint vid [[vertex_id]], uint iid [[instance_id]])
{
    InstancesVertex vert;
    constexpr float err = 1e-3;
    const device Instance& inst = instances[iid];
    uint iz = inst.iz & kPathIndexMask;
    const device Colorant& color = colors[iz];
    float w = widths[iz], cw = max(1.0, w), dw = 0.5 + 0.5 * cw;
    float alpha = color.a * 0.003921568627 * select(1.0, w / cw, w != 0), dx, dy;
    if (inst.iz & Instance::kOutlines) {
        const device Instance& pinst = instances[iid + inst.outline.prev], & ninst = instances[iid + inst.outline.next];
        const device Segment& p = pinst.outline.s, & o = inst.outline.s, & n = ninst.outline.s;
        bool pcap = inst.outline.prev == 0 || p.x1 != o.x0 || p.y1 != o.y0, ncap = inst.outline.next == 0 || n.x0 != o.x1 || n.y0 != o.y1;
        Curve pc, oc, nc;  oc.unpack(p, inst, n, *useCurves);
        pc.unpack(instances[iid + inst.outline.prev + pinst.outline.prev].outline.s, pinst, o, *useCurves);
        nc.unpack(o, ninst, instances[iid + inst.outline.next + ninst.outline.next].outline.s, *useCurves);
        float px, py, nx, ny, ax, bx, ay, by, cx, cy, ro, rp, rn, ow, lcap, rcospo, spo, rcoson, son, vx0, vy0, vx1, vy1;
        float2 vp, vn, _pno, _nno, no, np, nn, tpo, ton;
        px = select(select(pc.x0, pc.cpx, pc.isCurve), p.x0, oc.pcurve), py = select(select(pc.y0, pc.cpy, pc.isCurve), p.y0, oc.pcurve);
        nx = select(select(nc.x1, nc.cpx, nc.isCurve), n.x1, oc.ncurve), ny = select(select(nc.y1, nc.cpy, nc.isCurve), n.y1, oc.ncurve);
//        px = select(pc.x0, p.x0, oc.pcurve), py = select(pc.y0, p.y0, oc.pcurve);
//        nx = select(nc.x1, n.x1, oc.ncurve), ny = select(nc.y1, n.y1, oc.ncurve);
        vp = float2(oc.x0 - px, oc.y0 - py), vn = float2(nx - oc.x1, ny - oc.y1);
        ax = oc.cpx - oc.x1, ay = oc.cpy - oc.y1, bx = oc.cpx - oc.x0, by = oc.cpy - oc.y0, cx = oc.x1 - oc.x0, cy = oc.y1 - oc.y0;
        ro = rsqrt(cx * cx + cy * cy), rp = rsqrt(dot(vp, vp)), rn = rsqrt(dot(vn, vn));
        no = float2(cx, cy) * ro, np = vp * rp, nn = vn * rn;
        _pno = select(no, normalize(float2(bx, by)), oc.isCurve && !oc.pcurve);
        _nno = select(no, normalize(float2(-ax, -ay)), oc.isCurve && !oc.ncurve);
//        _pno = no, _nno = no;
        ow = select(0.0, 0.5 * abs(-no.y * bx + no.x * by), oc.isCurve);
        lcap = select(0.0, 0.41 * dw, oc.isCurve) + select(0.5, dw, inst.iz & (Instance::kSquareCap | Instance::kRoundCap));
        alpha *= float(ro < 1e2);
        pcap |= dot(np, _pno) < -0.99 || rp * dw > 5e2;
        ncap |= dot(_nno, nn) < -0.99 || rn * dw > 5e2;
        np = pcap ? _pno : np, nn = ncap ? _nno : nn;
        tpo = normalize(np + _pno), rcospo = 1.0 / abs(tpo.y * no.y + tpo.x * no.x), spo = rcospo * (dw + ow), vx0 = -tpo.y * spo, vy0 = tpo.x * spo;
        ton = normalize(_nno + nn), rcoson = 1.0 / abs(ton.y * no.y + ton.x * no.x), son = rcoson * (dw + ow), vx1 = -ton.y * son, vy1 = ton.x * son;
        
        float lp, px0, py0, ln, px1, py1, t, dt, dx0, dy0, dx1, dy1;
        lp = select(0.0, lcap, pcap) + err, px0 = oc.x0 - no.x * lp, py0 = oc.y0 - no.y * lp;
        ln = select(0.0, lcap, ncap) + err, px1 = oc.x1 + no.x * ln, py1 = oc.y1 + no.y * ln;
        t = ((px1 - px0) * vy1 - (py1 - py0) * vx1) / (vx0 * vy1 - vy0 * vx1);
        dt = select(t < 0.0 ? 1.0 : min(1.0, t), t > 0.0 ? -1.0 : max(-1.0, t), vid & 1);  // Even is left
        dx = vid & 2 ? fma(vx1, dt, px1) : fma(vx0, dt, px0), dx0 = dx - oc.x0, dx1 = dx - oc.x1;
        dy = vid & 2 ? fma(vy1, dt, py1) : fma(vy0, dt, py0), dy0 = dy - oc.y0, dy1 = dy - oc.y1;
        
        vert.dw = dw;
        if (oc.isCurve) {
            float area = cx * by - cy * bx;
            vert.u = (ax * dy1 - ay * dx1) / area;
            vert.v = (cx * dy0 - cy * dx0) / area;
            vert.d0 = (bx * dx0 + by * dy0) * rsqrt(bx * bx + by * by);
            vert.d1 = (ax * dx1 + ay * dy1) * rsqrt(ax * ax + ay * ay);
        } else
            vert.d0 = no.x * dx0 + no.y * dy0, vert.d1 = -(no.x * dx1 + no.y * dy1), vert.dm = -no.y * dx0 + no.x * dy0;
        
//        vert.miter0 = pcap || rcospo < kMiterLimit ? 1.0 : copysign(1.0, tpo.x * no.y - tpo.y * no.x) * (dx0 * tpo.x + dy0 * tpo.y);
//        vert.miter1 = ncap || rcoson < kMiterLimit ? 1.0 : copysign(1.0, no.x * ton.y - no.y * ton.x) * (dx1 * ton.x + dy1 * ton.y);
        vert.miter0 = oc.pcurve || pcap || rcospo < kMiterLimit ? 1.0 : min(44.0, rcospo) * ((dw - 0.5) - 0.5) + 0.5 + copysign(1.0, tpo.x * no.y - tpo.y * no.x) * (dx0 * -tpo.y + dy0 * tpo.x);
        vert.miter1 = oc.ncurve || ncap || rcoson < kMiterLimit ? 1.0 : min(44.0, rcoson) * ((dw - 0.5) - 0.5) + 0.5 + copysign(1.0, no.x * ton.y - no.y * ton.x) * (dx1 * -ton.y + dy1 * ton.x);

        vert.flags = (inst.iz & ~kPathIndexMask) | InstancesVertex::kIsShape | pcap * InstancesVertex::kPCap | ncap * InstancesVertex::kNCap | oc.isCurve * InstancesVertex::kIsCurve;
    } else {
        const device Cell& cell = inst.quad.cell;
        dx = select(cell.lx, cell.ux, vid & 1);
        dy = select(cell.ly, cell.uy, vid >> 1);
        vert.u = select((dx - (cell.lx - cell.ox)) / *width, FLT_MAX, cell.ox == kNullIndex);
        vert.v = (dy - (cell.ly - cell.oy)) / *height;
        vert.cover = inst.quad.cover;
        vert.flags = inst.iz & ~kPathIndexMask;
    }
    float x = dx / *width * 2.0 - 1.0, y = dy / *height * 2.0 - 1.0;
    float z = (iz * 2 + 1) / float(*pathCount * 2 + 2);
    vert.position = float4(x, y, z, 1.0);
    
    float ma = alpha * 0.003921568627;
    vert.color = float4(color.r * ma, color.g * ma, color.b * ma, alpha);
    vert.clip = distances(clips[iz], dx, dy);
    return vert;
}


fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    float alpha = 1.0;
    if (vert.flags & InstancesVertex::kIsShape) {
        float t, s, a, b, c, d, x2, y2, tx0, tx1, vx, ty0, ty1, vy, dist, sd0, sd1, cap, cap0, cap1;
        if (vert.flags & InstancesVertex::kIsCurve) {
            a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v);
            x2 = b * vert.v - d * vert.u, y2 = vert.u * c - vert.v * a;  // x0 = x2 + d, y0 = y2 - c, x1 = x2 - b, y1 = y2 + a;
            t = (closestT(x2 + d, y2 - c, x2 - b, y2 + a, x2, y2)), s = 1.0 - t;
            tx0 = x2 + s * d + t * -b, tx1 = x2 + s * -b, vx = tx1 - tx0;
            ty0 = y2 + s * -c + t * a, ty1 = y2 + s * a, vy = ty1 - ty0;
            dist = (tx1 * ty0 - ty1 * tx0) * rsqrt(vx * vx + vy * vy) / (a * d - b * c);
        } else
            dist = vert.dm;
    
        alpha = saturate(vert.dw - abs(dist));
        
        cap = vert.flags & Instance::kSquareCap ? vert.dw : 0.5;
        cap0 = select(
                      saturate(cap + vert.d0) * alpha,
                      saturate(vert.dw - sqrt(vert.d0 * vert.d0 + dist * dist)),
                      vert.flags & Instance::kRoundCap);
        cap1 = select(
                      saturate(cap + vert.d1) * alpha,
                      saturate(vert.dw - sqrt(vert.d1 * vert.d1 + dist * dist)),
                      vert.flags & Instance::kRoundCap);
        
        sd0 = vert.flags & InstancesVertex::kPCap ? saturate(vert.d0) : 1.0;
        sd1 = vert.flags & InstancesVertex::kNCap ? saturate(vert.d1) : 1.0;

//        alpha *= saturate(abs(vert.d0)) * saturate(abs(vert.d1)) * saturate(abs(vert.miter0)) * saturate(abs(vert.miter1));
//        sd0 = sd1 = 1;
        alpha = min(alpha, min(saturate(vert.miter0), saturate(vert.miter1)));
        
        alpha = cap0 * (1.0 - sd0) + cap1 * (1.0 - sd1) + (sd0 + sd1 - 1.0) * alpha;
    } else if (vert.u != FLT_MAX) {
        alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
        alpha = vert.flags & Instance::kEvenOdd ? 1.0 - abs(fmod(alpha, 2.0) - 1.0) : min(1.0, alpha);
    }
    return vert.color * alpha * saturate(vert.clip.x) * saturate(vert.clip.z) * saturate(vert.clip.y) * saturate(vert.clip.w);
}
