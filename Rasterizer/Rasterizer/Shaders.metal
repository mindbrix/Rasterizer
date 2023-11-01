//
//  Shaders.metal
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.h"
#import <metal_stdlib>
using namespace metal;

constexpr sampler s = sampler(coord::normalized, address::clamp_to_zero, mag_filter::nearest, min_filter::nearest, mip_filter::linear);
constexpr sampler imgSampler = sampler(filter::linear, mip_filter::linear);

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
    Cell cell;  short cover;  int base, biid;
};
struct Outline {
    Segment s;
    short prev, next;
    float cx, cy;
};
struct Instance {
    enum Type { kPCurve = 1 << 24, kEvenOdd = 1 << 24, kRoundCap = 1 << 25, kEdge = 1 << 26, kNCurve = 1 << 27, kSquareCap = 1 << 28, kOutlines = 1 << 29, kFastEdges = 1 << 30, kMolecule = 1 << 31 };
    uint32_t iz;  union { Quad quad;  Outline outline; };
};
struct Edge {
    uint32_t ic;  enum Flags { a0 = 1 << 31, a1 = 1 << 30, ue0 = 0xF << 26, ue1 = 0xF << 22, kMask = ~(a0 | a1 | ue0 | ue1) };
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

// https://www.shadertoy.com/view/4dsfRS

float sdBezier(float2 p0, float2 p1, float2 p2) {
    // This is to prevent 3 colinear points, but there should be better solution to it.
    p1 = mix(p1 + float2(1e-4), p1, abs(sign(p1 * 2.0 - p0 - p2)));
    
    // Calculate roots.
    float2 va = p1 - p0, vb = p0 - p1 * 2.0 + p2, vd = p0;
    float3 k = float3(3.0 * dot(va, vb), 2.0 * dot(va, va) + dot(vd, vb), dot(vd, va)) / dot(vb, vb);
    
    float a = k.x, b = k.y, c = k.z;
    float p = b - a*a / 3.0, p3 = p*p*p;
    float q = a * (2.0*a*a - 9.0*b) / 27.0 + c;
    float d = q*q + 4.0*p3 / 27.0;
    float offset = -a / 3.0;
    if (d >= 0.0) {
        float z = sqrt(d);
        float2 x = (float2(z, -z) - q) / 2.0;
        float2 uv = sign(x)*pow(abs(x), float2(1.0/3.0));
        float t = saturate(offset + uv.x + uv.y);
        float2 pt = fma(fma(vb, t, 2.0 * va), t, vd);
        return dot(pt, pt);
    }
    float v = acos(-sqrt(-27.0 / p3) * q / 2.0) / 3.0;
    float m = cos(v), n = sin(v)*1.732050808;
    float2 ts = saturate(float2(m + m, -n - m) * sqrt(-p / 3.0) + offset);
    
    float2 pt0 = fma(fma(vb, ts.x, 2.0 * va), ts.x, vd);
    float2 pt1 = fma(fma(vb, ts.y, 2.0 * va), ts.y, vd);
    return min(dot(pt0, pt0), dot(pt1, pt1));
}

float roundedDistance(float x0, float y0, float x1, float y1) {
    float ax = x1 - x0, ay = y1 - y0, t = saturate(-(ax * x0 + ay * y0) / (ax * ax + ay * ay)), x = fma(ax, t, x0), y = fma(ay, t, y0);
    return x * x + y * y;
}
float roundedDistance(float x0, float y0, float x1, float y1, float x2, float y2) {
    if (x1 == FLT_MAX)
        return roundedDistance(x0, y0, x2, y2);
    return sdBezier(float2(x0, y0), float2(x1, y1), float2(x2, y2));
}


float winding(float x0, float y0, float x1, float y1, float w0, float w1) {
    float dx, dy, a0, t, b, f, cover = w1 - w0;
    dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.0 ? w0 : w1) - y0) - dy * (1.0 - x0);
    dx = abs(dx), t = -a0 / fma(dx, cover, dy), dy = abs(dy);
    b = max(dx, dy), f = b / (b - min(dx, dy) * 0.4142135624);
    return saturate((t - 0.5) * f + 0.5) * cover;
}
float fastWinding(float x0, float y0, float x1, float y1) {
    return winding(x0, y0, x1, y1, saturate(y0), saturate(y1));
}
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2, bool a, float iy) {
    if (x1 == FLT_MAX)
        return fastWinding(x0, y0, x2, y2);
    float w0 = saturate(a ? y0 : iy), w1 = saturate(a ? iy : y2), cover = w1 - w0, ay, by, cy, t, s;
    ay = y0 + y2 - y1 - y1, by = 2.0 * (y1 - y0), cy = y0 - 0.5 * (w0 + w1);
    t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(sqrt(max(0.0, by * by - 4.0 * ay * cy)), cover)) / ay * 0.5, s = 1.0 - t;
    return winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w0, w1);
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
        s = 1.0 - t, w += winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w0, w1);
    }
    if (w1 != w2) {
        cy = y0 - 0.5 * (w1 + w2);
        t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(sqrt(max(0.0, by * by - 4.0 * ay * cy)), w2 - w1)) / ay * 0.5;
        s = 1.0 - t, w += winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w1, w2);
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
                                const device Transform *ctms [[buffer(4)]],
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
    int ue1 = (edge.ic & Edge::ue1) >> 22, segcount = ue1 & 0x7, i;
    const device Transform& m = ctms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    bool fast = inst.iz & Instance::kFastEdges;
    thread float *dst = & vert.x0;
    float w = widths[inst.iz & kPathIndexMask], cw = max(1.0, w), dw = (w != 0.0) * 0.5 * (cw + 1.0);
    float tx, ty, scale, ma, mb, mc, md, x16, y16, slx, sux, sly, suy, x0, y0;
    bool skip = false, pcurve;
    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;
    
    const device Point16 *pts = & points[inst.quad.base + (iid - inst.quad.biid) * kFastSegments];
    segcount -= int(w != 0.0 && (ue1 & 0x8) != 0);
    pcurve = pts->x & 0x8000, x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts++;
    x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
    
    if (!fast && pcurve) {
        x16 = 0.5 * (float(pts[-2].x & 0x7FFF) + float(pts->x & 0x7FFF)), y16 = 0.5 * (float(pts[-2].y & 0x7FFF) + float(pts->y & 0x7FFF));
        x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
    }
    *dst++ = slx = sux = x0, *dst++ = sly = suy = y0;

    for (i = 0; i < kFastSegments; i++, dst += 2) {
        skip |= i >= segcount;
        
        pcurve = pts->x & 0x8000, x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts++;
        x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
        if (!fast && pcurve) {
            x16 = 0.5 * (float(pts[-2].x & 0x7FFF) + float(pts->x & 0x7FFF)), y16 = 0.5 * (float(pts[-2].y & 0x7FFF) + float(pts->y & 0x7FFF));
            x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
        }
        dst[0] = select(x0, dst[-2], skip), slx = min(slx, dst[0]), sux = max(sux, dst[0]);
        dst[1] = select(y0, dst[-1], skip), sly = min(sly, dst[1]), suy = max(suy, dst[1]);
    }
    
    float ux = select(float(edge.ux), ceil(sux + dw), dw != 0.0), offset = select(0.5, 0.0, dw != 0.0);
    float dx = clamp(select(floor(slx - dw), ux, vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly - dw), ceil(suy + dw), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, offx = offset - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, offy = offset - dy;
    vert.position = float4(x, y, 1.0, slx == sux && sly == suy ? 0.0 : 1.0);
    for (dst = & vert.x0, i = 0; i < kFastSegments + 1; i++, dst += 2)
        dst[0] += offx, dst[1] += offy;
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

#pragma mark - Quad Curves

struct QuadCurvesVertex
{
    float4 position [[position]];
    float bx, by, u, v;
};

vertex QuadCurvesVertex quad_curves_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Transform *ctms [[buffer(4)]],
                                const device Instance *instances [[buffer(5)]],
                                const device Bounds *bounds [[buffer(7)]],
                                const device Point16 *points [[buffer(8)]],
                                constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                uint vid [[vertex_id]], uint iid [[instance_id]])
{
    QuadCurvesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Transform& m = ctms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Point16 *pts = & points[inst.quad.base + edge.i0];
    const device Cell& cell = inst.quad.cell;
    float offx = cell.ox - cell.lx, offy = cell.oy - cell.ly;
    float dx = 0, dy = 0, visible = 1.0;
//    bool pcurve = pts->x & 0x8000;
//    pts -= int(pcurve);
    
    float tx, ty, scale, ma, mb, mc, md, x16, y16, x0, y0, x1, y1, x2, y2;
    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;

    x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts++;
    x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
    x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts++;
    x1 = x16 * ma + y16 * mc + tx, y1 = x16 * mb + y16 * md + ty;
    x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts++;
    x2 = x16 * ma + y16 * mc + tx, y2 = x16 * mb + y16 * md + ty;
        
    
//    float mx, my;
//    float cpx = 2.0 * x1 - 0.5 * (x0 + x2), cpy = 2.0 * y1 - 0.5 * (y0 + y2);
//    if (pcurve) {
//        mx = x0 * 0.0625 + cpx * 0.375 + x2 * 0.5625;
//        my = y0 * 0.0625 + cpy * 0.375 + y2 * 0.5625;
//        x0 = x1, x1 = mx;
//        y0 = y1, y1 = my;
//    } else {
//        mx = x0 * 0.5625 + cpx * 0.375 + x2 * 0.0625;
//        my = y0 * 0.5625 + cpy * 0.375 + y2 * 0.0625;
//        x2 = x1, x1 = mx;
//        y2 = y1, y1 = my;
//    }
    
    x1 = 2.0 * x1 - 0.5 * (x0 + x2), y1 = 2.0 * y1 - 0.5 * (y0 + y2);
    
    float area = abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1));
    visible = area > 1.0;
    float offset = sqrt(2.0);
    float ax, ay, su, sv, sw;
    ax = x2 - x1, ay = y2 - y1;
    su = 2.0 * offset / (area * rsqrt(ax * ax + ay * ay));
    ax = x0 - x2, ay = y0 - y2;
    sv = offset / (area * rsqrt(ax * ax + ay * ay));
    ax = x1 - x0, ay = y1 - y0;
    sw = 2.0 * offset / (area * rsqrt(ax * ax + ay * ay));
    
    float du = (vid == 0 ? 0 : -su) + (vid == 1 ? 0 : 0.5 * sv) + (vid == 2 ? 0 : 0.5 * sw);
    float dv = (vid == 0 ? 0 : 0.5 * su) + (vid == 1 ? 0 : -sv) + (vid == 2 ? 0 : 0.5 * sw);
    float u = float(vid == 0) + du, v = float(vid == 1) + dv, w = 1.0 - u - v;
    dx = u * x0 + v * x1 + w * x2, dy = u * y0 + v * y1 + w * y2;
    
    vert.position = float4((dx + offx) / *width * 2.0 - 1.0, (dy + offy) / *height * 2.0 - 1.0, 1.0, visible);
    
    float t, e = 0.5;
    t = (dx - float(cell.lx - e)) / float(2.0 * e + cell.ux - cell.lx);
    vert.bx = 2.0 * t - 1.0;
    t = (dy - float(cell.ly - e)) / float(2.0 * e + cell.uy - cell.ly);
    vert.by = 2.0 * t - 1.0;
    vert.u = u, vert.v = v;
    return vert;
}

fragment float4 quad_curves_fragment_main(QuadCurvesVertex vert [[stage_in]])
{
    float fx, fy, a, b, c, d, invdet, x0, y0, x1, y1, x2, y2;
    fx = 1.0 - abs(vert.bx), fy = 1.0 - abs(vert.by);
    a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v);
    invdet = 1.0 / (a * d - b * c), a *= invdet, b *= invdet, c *= invdet, d *= invdet;
    x2 = 0.5 + b * vert.v - d * vert.u, y2 = 0.5 + vert.u * c - vert.v * a;
    x0 = x2 + d, y0 = y2 - c, x1 = x2 - b, y1 = y2 + a;
    return saturate(fx / fwidth(fx)) * saturate(fy / fwidth(fy)) * (fastWinding(x0, y0, x2, y2) + quadraticWinding(x2, y2, x1, y1, x0, y0));
}

#pragma mark - Quad Molecules

struct QuadMoleculesVertex
{
    float4 position [[position]];
    float dw, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8;
};

vertex QuadMoleculesVertex quad_molecules_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Segment *segments [[buffer(2)]],
                                const device Transform *ctms [[buffer(4)]],
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
    int curve0, curve1, curve2, ue1 = (edge.ic & Edge::ue1) >> 22, segcount = ue1 & 0x7, i;
    const device Transform& m = ctms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    thread float *dst = & vert.x0;
    float w = widths[inst.iz & kPathIndexMask], cw = max(1.0, w), dw = (w != 0.0) * 0.5 * (cw + 1.0);
    float tx, ty, scale, ma, mb, mc, md, px, py, x0, y0, x1, y1, nx, ny, cpx, cpy;
    segcount -= int(w != 0.0 && (ue1 & 0x8) != 0);
    float slx = 0.0, sux = 0.0, sly = 0.0, suy = 0.0, visible = segcount == 0 ? 0.0 : 1.0;
    
    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;
    
//    if (kUseQuad16s) {
//        float x0, y0, cpx, cpy, x16, y16;
//        bool skip, end, isCurve;
//
//        const device Point16 *pts = & points[inst.quad.base + (iid - inst.quad.biid) * kQuadPoints];
//        skip = pts->x & 0x8000, x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts += 1;
//        *dst++ = slx = sux = x0 = x16 * ma + y16 * mc + tx,
//        *dst++ = sly = suy = y0 = x16 * mb + y16 * md + ty;
//        for (i = 0; i < kFastSegments; i++, skip |= end) {
//            x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts += 1;
//            cpx = x16 * ma + y16 * mc + tx, cpy = x16 * mb + y16 * md + ty;
//            isCurve = !skip && (cpx != x0 || cpy != y0);
//            *dst++ = isCurve ? cpx : FLT_MAX, *dst++ = isCurve ? cpy : FLT_MAX;
//
//            slx = !isCurve ? slx : min(slx, cpx), sux = !isCurve ? sux : max(sux, cpx);
//            sly = !isCurve ? sly : min(sly, cpy), suy = !isCurve ? suy : max(suy, cpy);
//
//            end = pts->x & 0x8000, x16 = pts->x & 0x7FFF, y16 = pts->y & 0x7FFF, pts += 1;
//
//            x0 = skip ? x0 : x16 * ma + y16 * mc + tx, *dst++ = x0, slx = min(slx, x0), sux = max(sux, x0);
//            y0 = skip ? y0 : x16 * mb + y16 * md + ty, *dst++ = y0, sly = min(sly, y0), suy = max(suy, y0);
//        }
//    } else {
        if (visible) {
            const device Point16 *pts = & points[inst.quad.base + (iid - inst.quad.biid) * kFastSegments];
            float x, y;
            curve0 = ((pts->x & 0x8000) >> 14) | ((pts->y & 0x8000) >> 15);
            curve1 = (((pts + 1)->x & 0x8000) >> 14) | (((pts + 1)->y & 0x8000) >> 15);
            x = curve0 == 2 ? (pts - 1)->x & 0x7FFF : 0, y = curve0 == 2 ? (pts - 1)->y & 0x7FFF : 0;
            px = x * ma + y * mc + tx, py = x * mb + y * md + ty;
            x = pts->x & 0x7FFF, y = pts->y & 0x7FFF, pts++;
            x0 = x * ma + y * mc + tx, y0 = x * mb + y * md + ty;
            x = pts->x & 0x7FFF, y = pts->y & 0x7FFF, pts++;
            x1 = x * ma + y * mc + tx, y1 = x * mb + y * md + ty;
            
            dst[0] = slx = sux = x0, dst[1] = sly = suy = y0, dst += 2;
            
            for (i = 0; i < kFastSegments; i++, dst += 4, px = x0, x0 = x1, x1 = nx, py = y0, y0 = y1, y1 = ny, curve0 = curve1, curve1 = curve2, pts++) {
                if (i >= segcount)
                    dst[0] = FLT_MAX, dst[2] = dst[-2], dst[3] = dst[-1];
                else {
                    curve2 = ((pts->x & 0x8000) >> 14) | ((pts->y & 0x8000) >> 15);
                    x = pts->x & 0x7FFF, y = pts->y & 0x7FFF;
                    nx = x * ma + y * mc + tx, ny = x * mb + y * md + ty;
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
//    }
    
    
    float ux = select(float(edge.ux), ceil(sux + dw), dw != 0.0), offset = select(0.5, 0.0, dw != 0.0);
    float dx = clamp(select(floor(slx - dw), ux, vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly - dw), ceil(suy + dw), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, offx = offset - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, offy = offset - dy;
    vert.position = float4(x, y, 1.0, visible);
    vert.dw = dw;
    for (dst = & vert.x0, i = 0; i < kFastSegments + 1; i++, dst += 4)
        dst[0] += offx, dst[1] += offy;
    for (dst = & vert.x1, i = 0; i < kFastSegments; i++, dst += 4)
        if (dst[0] != FLT_MAX)
            dst[0] += offx, dst[1] += offy;
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
                                     const device Transform *ctms [[buffer(4)]],
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
    uint32_t ids[2] = { ((edge.ic & Edge::ue0) >> 10) + edge.i0, ((edge.ic & Edge::ue1) >> 6) + edge.ux };
    const thread uint32_t *idxes = & ids[0];
    float slx = cell.ux, sly = FLT_MAX, suy = -FLT_MAX;
    for (int i = 0; i < 2; i++, dst += 6) {
        float x0, y0, x1, y1, x2, y2;
        if (idxes[i] != 0xFFFFF) {
            const device Segment& s = segments[inst.quad.base + idxes[i]];
            x0 = dst[0] = s.x0, y0 = dst[1] = s.y0;
            bool ncurve = *useCurves && as_type<uint>(x0) & 1;
            if (ncurve) {
                const device Segment& n = segments[inst.quad.base + idxes[i] + 1];
                x2 = dst[4] = n.x1, y2 = dst[5] = n.y1;
                x1 = dst[2] = 2.f * s.x1 - 0.5f * (x0 + x2), y1 = dst[3] = 2.f * s.y1 - 0.5f * (y0 + y2);
            } else {
                dst[2] = FLT_MAX, x2 = dst[4] = s.x1, y2 = dst[5] = s.y1;
            }
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
    enum Flags { kPCap = 1 << 0, kNCap = 1 << 1, kIsCurve = 1 << 2, kIsShape = 1 << 3, kPCurve = 1 << 4, kNCurve = 1 << 5 };
    float4 position [[position]], clip;
    float s, t, u, v, cover, dw, d0, d1, dm0, dm1, miter0, miter1, alpha;
//    float x0, y0, x1, y1, x2, y2;
    uint32_t iz, flags, slot;
};

vertex InstancesVertex instances_vertex_main(
            const device Instance *instances [[buffer(1)]],
            const device Transform *ctms [[buffer(4)]],
            const device Transform *clips [[buffer(5)]],
            const device float *widths [[buffer(6)]],
            const device uint32_t *slots [[buffer(8)]],
            const device Transform *texctms [[buffer(9)]],
            constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
            constant uint *pathCount [[buffer(13)]],
            constant bool *useCurves [[buffer(14)]],
            uint vid [[vertex_id]], uint iid [[instance_id]])
{
    InstancesVertex vert;
    constexpr float err = 1e-3;
    const device Instance& inst = instances[iid];
    uint iz = inst.iz & kPathIndexMask;
    float w = widths[iz], cw = max(1.0, w), dw = 0.5 + 0.5 * cw;
    float alpha = select(1.0, w / cw, w != 0), dx, dy;
    if (inst.iz & Instance::kOutlines) {
        float x0, y0, x1, y1, cpx, cpy, px, py, nx, ny, pcpx, pcpy, ncpx, ncpy;
        float2 no, np, nn;
        bool pcap, ncap;

        const device Instance & pinst = instances[iid + inst.outline.prev], & ninst = instances[iid + inst.outline.next];
        const device Segment& p = pinst.outline.s, & o = inst.outline.s, & n = ninst.outline.s;
        pcap = inst.outline.prev == 0 || p.x1 != o.x0 || p.y1 != o.y0, ncap = inst.outline.next == 0 || n.x0 != o.x1 || n.y0 != o.y1;
        x0 = o.x0, y0 = o.y0, x1 = o.x1, y1 = o.y1;
        cpx = inst.outline.cx, cpy = inst.outline.cy;
        px = p.x0, py = p.y0, pcpx = pinst.outline.cx, pcpy = pinst.outline.cy;
        nx = n.x1, ny = n.y1, ncpx = ninst.outline.cx, ncpy = ninst.outline.cy;

        bool greedy = true;
        bool isCurve = *useCurves && cpx != FLT_MAX;
        bool useTangents = greedy ? *useCurves : isCurve, pcurve = useTangents && pcpx != FLT_MAX, ncurve = useTangents && ncpx != FLT_MAX;
        np = normalize({ x0 - (pcurve ? pcpx : px), y0 - (pcurve ? pcpy : py) });
        nn = normalize({ (ncurve ? ncpx : nx) - x1, (ncurve ? ncpy : ny) - y1 });
        
        float ax, bx, ay, by, cx, cy, ro, ow, lcap, rcospo, spo, rcoson, son, vx0, vy0, vx1, vy1;
        float2 tpo, ton;
        ax = cpx - x1, ay = cpy - y1, bx = cpx - x0, by = cpy - y0, cx = x1 - x0, cy = y1 - y0;
        ro = rsqrt(cx * cx + cy * cy), no = float2(cx, cy) * ro;
        
        ow = select(0.0, 0.5 * abs(-no.y * bx + no.x * by), isCurve);
        lcap = select(0.0, 0.41 * dw, isCurve) + select(0.5, dw, inst.iz & (Instance::kSquareCap | Instance::kRoundCap));
        alpha *= float(ro < 1e2);
                
        pcap |= dot(np, no) < -0.94;
        ncap |= dot(no, nn) < -0.94;
        np = pcap ? no : np, nn = ncap ? no : nn;
        
        float2 pno, nno;
        pno = (greedy ? isCurve : pcurve) ? normalize(float2(bx, by)) : no;
        nno = (greedy ? isCurve : ncurve) ? normalize(-float2(ax, ay)) : no;
        
        tpo = normalize(np + pno), rcospo = 1.0 / abs(dot(no, tpo)), spo = rcospo * (dw + ow), vx0 = -tpo.y * spo, vy0 = tpo.x * spo;
        ton = normalize(nno + nn), rcoson = 1.0 / abs(dot(no, ton)), son = rcoson * (dw + ow), vx1 = -ton.y * son, vy1 = ton.x * son;
        
        float lp, px0, py0, ln, px1, py1, t, dt, dx0, dy0, dx1, dy1;
        lp = select(0.0, lcap, pcap) + err, px0 = x0 - no.x * lp, py0 = y0 - no.y * lp;
        ln = select(0.0, lcap, ncap) + err, px1 = x1 + no.x * ln, py1 = y1 + no.y * ln;
        t = ((px1 - px0) * vy1 - (py1 - py0) * vx1) / (vx0 * vy1 - vy0 * vx1);
        dt = select(t < 0.0 ? 1.0 : min(1.0, t), t > 0.0 ? -1.0 : max(-1.0, t), vid & 1);  // Even is left
        dx = vid & 2 ? fma(vx1, dt, px1) : fma(vx0, dt, px0), dx0 = dx - x0, dx1 = dx - x1;
        dy = vid & 2 ? fma(vy1, dt, py1) : fma(vy0, dt, py0), dy0 = dy - y0, dy1 = dy - y1;
        
        vert.dw = dw;
        if (isCurve) {
            float area = cx * by - cy * bx, rlb = rsqrt(bx * bx + by * by), rla = rsqrt(ax * ax + ay * ay);
            vert.u = (ax * dy1 - ay * dx1) / area;
            vert.v = (cx * dy0 - cy * dx0) / area;
            vert.d0 = (bx * dx0 + by * dy0) * rlb;
            vert.dm0 = (-by * dx0 + bx * dy0) * rlb;
            
            vert.d1 = (ax * dx1 + ay * dy1) * rla;
            vert.dm1 = (-ay * dx1 + ax * dy1) * rla;
            
//            vert.x0 = x0 - dx, vert.y0 = y0 - dy, vert.x1 = cpx - dx, vert.y1 = cpy - dy, vert.x2 = x1 - dx, vert.y2 = y1 - dy;
        } else
            vert.d0 = no.x * dx0 + no.y * dy0, vert.d1 = -(no.x * dx1 + no.y * dy1), vert.dm0 = vert.dm1 = -no.y * dx0 + no.x * dy0;
        
        vert.miter0 = pcap || rcospo < kMiterLimit ? 1.0 : min(44.0, rcospo) * ((dw - 0.5) - 0.5) + 0.5 + copysign(1.0, tpo.x * no.y - tpo.y * no.x) * (dx0 * -tpo.y + dy0 * tpo.x);
        vert.miter1 = ncap || rcoson < kMiterLimit ? 1.0 : min(44.0, rcoson) * ((dw - 0.5) - 0.5) + 0.5 + copysign(1.0, no.x * ton.y - no.y * ton.x) * (dx1 * -ton.y + dy1 * ton.x);

        vert.flags = (inst.iz & ~kPathIndexMask) | InstancesVertex::kIsShape | pcap * InstancesVertex::kPCap | ncap * InstancesVertex::kNCap | isCurve * InstancesVertex::kIsCurve | pcurve * InstancesVertex::kPCurve | ncurve * InstancesVertex::kNCurve;
    } else {
        const device Cell& cell = inst.quad.cell;
        dx = select(cell.lx, cell.ux, vid & 1);
        dy = select(cell.ly, cell.uy, vid >> 1);
        vert.u = select((dx - (cell.lx - cell.ox)) / *width, FLT_MAX, cell.ox == kNullIndex);
        vert.v = (dy - (cell.ly - cell.oy)) / *height;
        vert.cover = inst.quad.cover;
        vert.flags = inst.iz & ~kPathIndexMask;
    }
    bool noImage = slots[iz] == 0;
    const device Transform& tm = texctms[iz];
    vert.s = noImage ? FLT_MAX : dx * tm.a + dy * tm.c + tm.tx, vert.t = noImage ? FLT_MAX : 1.0 - (dx * tm.b + dy * tm.d + tm.ty);
    vert.slot = slots[iz];
    float x = dx / *width * 2.0 - 1.0, y = dy / *height * 2.0 - 1.0;
    float z = (iz * 2 + 1) / float(*pathCount * 2 + 2);
    vert.position = float4(x, y, z, 1.0);
    vert.clip = distances(clips[iz], dx, dy);
    vert.alpha = alpha;
    vert.iz = iz;
    return vert;
}

fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]],
                                        const device Colorant *colors [[buffer(0)]],
                                        texture2d<float> accumulation [[texture(0)]],
                                        const array<texture2d<float>, kTextureSlotsSize> images [[texture(2)]]
)
{
    float alpha = 1.0;
    if (vert.flags & InstancesVertex::kIsShape) {
        float a, b, c, d, x2, y2, sqdist, sd0, sd1, cap, cap0, cap1;
        bool isCurve = vert.flags & InstancesVertex::kIsCurve;
        bool squareCap = vert.flags & Instance::kSquareCap, roundCap = vert.flags & Instance::kRoundCap;
        bool pcap = vert.flags & InstancesVertex::kPCap, ncap = vert.flags & InstancesVertex::kNCap;
        bool pcurve = vert.flags & InstancesVertex::kPCurve, ncurve = vert.flags & InstancesVertex::kNCurve;
        
        cap = squareCap ? vert.dw : 0.5;
        
        if (isCurve) {
//            float x0, y0, x1, y1;
//            x0 = vert.x0, y0 = vert.y0, x1 = vert.x1, y1 = vert.y1, x2 = vert.x2, y2 = vert.y2;
            a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v);
            float invdet = 1.0 / (a * d - b * c);
            a *= invdet, b *= invdet, c *= invdet, d *= invdet;
            x2 = b * vert.v - d * vert.u, y2 = vert.u * c - vert.v * a;
            float x0 = x2 + d, y0 = y2 - c, x1 = x2 - b, y1 = y2 + a;
            sqdist = sdBezier(float2(x0, y0), float2(x1, y1), float2(x2, y2));
        } else {
            float dx = vert.d0 - clamp(vert.d0, 0.0, vert.d0 + vert.d1);
            sqdist = dx * dx + vert.dm0 * vert.dm0;
        }
            
        alpha = saturate(vert.dw - sqrt(sqdist));
        cap0 = (pcap ? saturate(cap + vert.d0) : 1.0) * saturate(vert.dw - abs(vert.dm0));
        cap1 = (ncap ? saturate(cap + vert.d1) : 1.0) * saturate(vert.dw - abs(vert.dm1));
        
        bool f0 = pcap ? !roundCap : !isCurve || !pcurve;
        bool f1 = ncap ? !roundCap : !isCurve || !ncurve;
        sd0 = f0 ? saturate(vert.d0) : 1.0;
        sd1 = f1 ? saturate(vert.d1) : 1.0;

        alpha = min(alpha, min(saturate(vert.miter0), saturate(vert.miter1)));

        alpha = cap0 * (1.0 - sd0) + cap1 * (1.0 - sd1) + (sd0 + sd1 - 1.0) * alpha;
    } else if (vert.u != FLT_MAX) {
        alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
        alpha = vert.flags & Instance::kEvenOdd ? 1.0 - abs(fmod(alpha, 2.0) - 1.0) : min(1.0, alpha);
    }
    Colorant color = colors[vert.iz];
    if (vert.s != FLT_MAX) {
        float4 tex = images[vert.slot].sample(imgSampler, float2(vert.s, vert.t));
        color.b = tex.z * 255.0, color.g = tex.y * 255.0, color.r = tex.x * 255.0, color.a = tex.w * 255.0;
    }
    float ma = 0.003921568627 * alpha * vert.alpha * saturate(vert.clip.x) * saturate(vert.clip.z) * saturate(vert.clip.y) * saturate(vert.clip.w);
    return { color.r * ma, color.g * ma, color.b * ma, color.a * ma };
}
