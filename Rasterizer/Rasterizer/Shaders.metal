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
    enum Flags { isCurve = 0x8000, kMask = ~isCurve };
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
    uint32_t ic;  enum Flags { isClose = 1 << 31, a1 = 1 << 30, ue0 = 0xF << 26, ue1 = 0xF << 22, kMask = ~(isClose | a1 | ue0 | ue1) };
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
    vb = p1 - p0, va = p2 - p1, va -= vb, vb *= 2.0;
    if (d >= 0.0) {
        float z = sqrt(d);
        float2 x = 0.5 * (float2(z, -z) - q);
        float2 uv = sign(x)*pow(abs(x), float2(1.0/3.0));
        float t = saturate(uv.x + uv.y - a);
        float2 pt = fma(fma(va, t, vb), t, p0);
        return dot(pt, pt);
    }
    float v = acos(-sqrt(-27.0 / p3) * q / 2.0) / 3.0;
    float m = cos(v), n = sin(v)*1.732050808;
    float2 ts = saturate(float2(m + m, -n - m) * sqrt(-p / 3.0) - a);
    float2 pt0 = fma(fma(va, ts.x, vb), ts.x, p0);
    float2 pt1 = fma(fma(va, ts.y, vb), ts.y, p0);
    return min(dot(pt0, pt0), dot(pt1, pt1));
}

float roundedDistance(float x0, float y0, float x1, float y1) {
    float ax = x1 - x0, ay = y1 - y0, t = saturate(-(ax * x0 + ay * y0) / (ax * ax + ay * ay)), x = fma(ax, t, x0), y = fma(ay, t, y0);
    return x * x + y * y;
}
float roundedDistance(float x0, float y0, float x1, float y1, float x2, float y2) {
    if (abs(x1 - 0.5 * (x0 + x2)) < 1e-3)
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
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2) {
    float w0 = saturate(y0), w2 = saturate(y2);
    if (max(x0, max(x1, x2)) <= 0.0)
        return w2 - w0;
    float w = 0.0, ay = y2 - y1, by = y1 - y0, cy, t, s, w1;
    w1 = saturate(ay * by >= 0.0 ? y2 : y0 - by * by / (ay - by)), ay -= by, by *= 2.0;
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
    const device Point16 *pts = & points[inst.quad.base + (iid - inst.quad.biid) * kFastSegments];
    thread float *dst = & vert.x0;
    float w = widths[inst.iz & kPathIndexMask], cw = max(1.0, w), dw = (w != 0.0) * 0.5 * (cw + 1.0);
    float tx, ty, scale, ma, mb, mc, md, x16, y16, slx, sux, sly, suy, x0, y0;
    bool skip = false;
    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;
    
    segcount -= int(w != 0.0 && (ue1 & 0x8) != 0);
    x16 = pts->x & Point16::kMask, y16 = pts->y & Point16::kMask, pts++;
    x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
    
    *dst++ = slx = sux = x0, *dst++ = sly = suy = y0;

    for (i = 0; i < kFastSegments; i++, dst += 2) {
        skip |= i >= segcount;
        
        x16 = pts->x & Point16::kMask, y16 = pts->y & Point16::kMask, pts++;
        x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
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


#pragma mark - Quad Molecules

struct QuadMoleculesVertex
{
    float4 position [[position]];
    float dw, x0, y0, x1, y1, x2, y2;
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
    const device Transform& m = ctms[inst.iz & kPathIndexMask];
    const device Bounds& b = bounds[inst.iz & kPathIndexMask];
    const device Cell& cell = inst.quad.cell;
    const device Point16 *p = & points[inst.quad.base + ((edge.ic & Edge::ue0) >> 10) + edge.i0];
    float visible = edge.ic & Edge::isClose ? 0.0 : 1.0;
    float w = widths[inst.iz & kPathIndexMask], cw = max(1.0, w), dw = (w != 0.0) * 0.5 * (cw + 1.0);
    float offset = select(0.5, 0.0, dw != 0.0);
    float tx, ty, scale, ma, mb, mc, md, x16, y16, x0, y0, x1, y1, x2, y2, slx, sux, sly, suy;

    tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
    scale = max(b.ux - b.lx, b.uy - b.ly) / kMoleculesRange;
    ma = m.a * scale, mb = m.b * scale, mc = m.c * scale, md = m.d * scale;
    
    x16 = p->x & Point16::kMask, y16 = p->y & Point16::kMask;
    x0 = x16 * ma + y16 * mc + tx, y0 = x16 * mb + y16 * md + ty;
    x16 = (p + 1)->x & Point16::kMask, y16 = (p + 1)->y & Point16::kMask;
    x1 = x16 * ma + y16 * mc + tx, y1 = x16 * mb + y16 * md + ty;
    if (p->x & Point16::isCurve) {
        x16 = (p + 2)->x & Point16::kMask, y16 = (p + 2)->y & Point16::kMask;
        x2 = x16 * ma + y16 * mc + tx, y2 = x16 * mb + y16 * md + ty;
        x1 = 2.f * x1 - 0.5f * (x0 + x2), y1 = 2.f * y1 - 0.5f * (y0 + y2);
    } else {
        x2 = x1, x1 = 0.5 * (x0 + x2);
        y2 = y1, y1 = 0.5 * (y0 + y2);
    }
    slx = min(x0, x2), slx = min(x1, slx);
    sly = min(y0, y2), sly = min(y1, sly);
    sux = max(x0, x2), sux = max(x1, sux);
    suy = max(y0, y2), suy = max(y1, suy);
    
    sux = select(float(edge.ux), sux, dw != 0.0);
    float dx = clamp(select(floor(slx - dw), ceil(sux + dw), vid & 1), float(cell.lx), float(cell.ux));
    float dy = clamp(select(floor(sly - dw), ceil(suy + dw), vid >> 1), float(cell.ly), float(cell.uy));
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0, offx = offset - dx;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0, offy = offset - dy;
    
    vert.position = float4(x, y, 1.0, visible);
    vert.dw = dw;
    vert.x0 = x0 + offx, vert.y0 = y0 + offy;
    vert.x1 = x1 + offx, vert.y1 = y1 + offy;
    vert.x2 = x2 + offx, vert.y2 = y2 + offy;
    
    return vert;
}

fragment float4 quad_outlines_fragment_main(QuadMoleculesVertex vt [[stage_in]])
{
    float d = roundedDistance(vt.x0, vt.y0, vt.x1, vt.y1, vt.x2, vt.y2);
    return saturate(vt.dw - sqrt(d));
}

fragment float4 quad_molecules_fragment_main(QuadMoleculesVertex vert [[stage_in]])
{
    return quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2);
//    return 0.2;
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
                                     constant bool *useCurves [[buffer(14)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    EdgesVertex vert;
    const device Edge& edge = edges[iid];
    const device Instance& inst = instances[edge.ic & Edge::kMask];
    const device Cell& cell = inst.quad.cell;
    thread float *dst = & vert.x0;
    uint32_t ids[2] = { ((edge.ic & Edge::ue0) >> 10) + edge.i0, ((edge.ic & Edge::ue1) >> 6) + edge.ux };
    const thread uint32_t *idxes = & ids[0];
    float slx = cell.ux, sly = FLT_MAX, suy = -FLT_MAX;
    float visible = 1.0;
    float x0, y0, x1, y1, x2, y2;
    
    for (int i = 0; i < 2; i++, dst += 6) {
        if (idxes[i] != 0xFFFFF) {
            const device Segment& s = segments[inst.quad.base + idxes[i]];
            x0 = dst[0] = s.x0, y0 = dst[1] = s.y0;
            bool ncurve = *useCurves && as_type<uint>(x0) & 1;
            if (ncurve) {
                const device Segment& n = segments[inst.quad.base + idxes[i] + 1];
                x2 = dst[4] = n.x1, y2 = dst[5] = n.y1;
                x1 = dst[2] = s.x1, y1 = dst[3] = s.y1;
                
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
                x2 = dst[4] = s.x1, y2 = dst[5] = s.y1;
                dst[2] = 0.5 * (x0 + x2), dst[3] = 0.5 * (y0 + y2);
                
                float m = (x2 - x0) / (y2 - y0), c = x0 - m * y0;
                slx = min(slx, max(min(x0, x2), min(m * clamp(y0, float(cell.ly), float(cell.uy)) + c, m * clamp(y2, float(cell.ly), float(cell.uy)) + c)));
            }
            sly = min(sly, min(y0, y2)), suy = max(suy, max(y0, y2));
        } else {
            dst[0] = 0.0, dst[1] = 0.0, dst[2] = 0.0, dst[3] = 0.0, dst[4] = 0.0, dst[5] = 0.0;
        }
    }
    float dx = select(max(floor(slx), float(cell.lx)), float(cell.ux), vid & 1), tx = 0.5 - dx;
    float dy = select(max(floor(sly), float(cell.ly)), min(ceil(suy), float(cell.uy)), vid >> 1), ty = 0.5 - dy;
    float x = (cell.ox - cell.lx + dx) / *width * 2.0 - 1.0;
    float y = (cell.oy - cell.ly + dy) / *height * 2.0 - 1.0;
    vert.position = float4(x, y, 1.0, visible);
    vert.x0 += tx, vert.y0 += ty, vert.x1 += tx, vert.y1 += ty, vert.x2 += tx, vert.y2 += ty;
    vert.x3 += tx, vert.y3 += ty, vert.x4 += tx, vert.y4 += ty, vert.x5 += tx, vert.y5 += ty;

    return vert;
}

fragment float4 fast_edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return fastWinding(vert.x0, vert.y0, vert.x2, vert.y2) + fastWinding(vert.x3, vert.y3, vert.x5, vert.y5);
}

fragment float4 quad_edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2)
            + quadraticWinding(vert.x3, vert.y3, vert.x4, vert.y4, vert.x5, vert.y5);
}

#pragma mark - Instances

struct InstancesVertex
{
    enum Flags { kPCap = 1 << 0, kNCap = 1 << 1, kIsCurve = 1 << 2, kIsShape = 1 << 3, kPCurve = 1 << 4, kNCurve = 1 << 5 };
    float4 position [[position]], clip;
    float u, v, cover, dw, d0, d1, dm0, dm1, miter0, miter1, alpha;
//    float x0, y0, x1, y1, x2, y2;
    uint32_t iz, flags;
};

vertex InstancesVertex instances_vertex_main(
            const device Instance *instances [[buffer(1)]],
            const device Transform *ctms [[buffer(4)]],
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
    float w = widths[iz], cw = max(1.0, w), dw = 0.5 * (1.0 + cw);
    float alpha = select(1.0, w / cw, w != 0), dx, dy;
    if (inst.iz & Instance::kOutlines) {
        const bool roundCap = inst.iz & Instance::kRoundCap;
        const bool squareCap = inst.iz & Instance::kSquareCap;
        const device Instance & pinst = instances[iid + inst.outline.prev], & ninst = instances[iid + inst.outline.next];
        const device Segment& p = pinst.outline.s, & o = inst.outline.s, & n = ninst.outline.s;
        
        float x0, y0, x1, y1, cpx, cpy;
        float ax, bx, ay, by, cx, cy, ow, lcap, vx0, vy0, vx1, vy1;
        float px0, py0, nx1, ny1, cos0, cos1, s0, s1;
        float2 no, p0, n0, p1, n1, tan0, tan1;
        bool pcap = inst.outline.prev == 0 || p.x1 != o.x0 || p.y1 != o.y0;
        bool ncap = inst.outline.next == 0 || n.x0 != o.x1 || n.y0 != o.y1;
        x0 = o.x0, y0 = o.y0, x1 = o.x1, y1 = o.y1;
        cpx = inst.outline.cx, cpy = inst.outline.cy;
        ax = cpx - x1, ay = cpy - y1, bx = cpx - x0, by = cpy - y0;
        cx = x1 - x0, cy = y1 - y0;
        no = normalize({ cx, cy });
        alpha *= (cx * cx + cy * cy > 1e-3);
        
        const bool isCurve = *useCurves && cpx != FLT_MAX;
        const bool pcurve = *useCurves && pinst.outline.cx != FLT_MAX, ncurve = *useCurves && ninst.outline.cx != FLT_MAX;
        const bool f0 = pcap ? !roundCap : !isCurve || !pcurve;
        const bool f1 = ncap ? !roundCap : !isCurve || !ncurve;
        
        px0 = x0 - (pcurve ? pinst.outline.cx : p.x0);
        py0 = y0 - (pcurve ? pinst.outline.cy : p.y0);
        p0 = normalize({ px0, py0 });
        n0 = normalize({ (isCurve ? cpx : x1) - x0, (isCurve ? cpy : y1) - y0 });
        p1 = normalize({ x1 - (isCurve ? cpx : x0), y1 - (isCurve ? cpy : y0) });
        nx1 = (ncurve ? ninst.outline.cx : n.x1) - x1;
        ny1 = (ncurve ? ninst.outline.cy : n.y1) - y1;
        n1 = normalize({ nx1, ny1 });
        
        ow = select(0.0, 0.5 * abs(-no.y * bx + no.x * by), isCurve);
        lcap = select(0.0, 0.41 * dw, isCurve) + select(0.5, dw, squareCap || roundCap);
        
        pcap = pcap || (px0 * px0 + py0 * py0) < 1e-3 || dot(p0, n0) < -0.866025403784439;
        tan0 = pcap ? no : normalize(p0 + n0);
        cos0 = abs(dot(no, tan0));
        s0 = (dw + ow) / max(1e-1, cos0);
        vx0 = s0 * -tan0.y;
        vy0 = s0 * tan0.x;
        
        ncap = ncap || (nx1 * nx1 + ny1 * ny1) < 1e-3 || dot(p1, n1) < -0.866025403784439;
        tan1 = ncap ? no : normalize(p1 + n1);
        cos1 = abs(dot(no, tan1));
        s1 = (dw + ow) / max(1e-1, cos1);
        vx1 = s1 * -tan1.y;
        vy1 = s1 * tan1.x;
        
        float lp, cx0, cy0, ln, cx1, cy1, t, dt, dx0, dy0, dx1, dy1;
        lp = select(0.0, lcap, pcap) + err, cx0 = x0 - no.x * lp, cy0 = y0 - no.y * lp;
        ln = select(0.0, lcap, ncap) + err, cx1 = x1 + no.x * ln, cy1 = y1 + no.y * ln;
        t = ((cx1 - cx0) * vy1 - (cy1 - cy0) * vx1) / (vx0 * vy1 - vy0 * vx1);
        dt = select(t < 0.0 ? 1.0 : min(1.0, t), t > 0.0 ? -1.0 : max(-1.0, t), vid & 1);  // Even is left
        dx = vid & 2 ? fma(vx1, dt, cx1) : fma(vx0, dt, cx0), dx0 = dx - x0, dx1 = dx - x1;
        dy = vid & 2 ? fma(vy1, dt, cy1) : fma(vy0, dt, cy0), dy0 = dy - y0, dy1 = dy - y1;
        
        if (isCurve) {
            float area = cx * by - cy * bx;
            vert.u = (ax * dy1 - ay * dx1) / area;
            vert.v = (cx * dy0 - cy * dx0) / area;
//            vert.x0 = x0 - dx, vert.y0 = y0 - dy, vert.x1 = cpx - dx, vert.y1 = cpy - dy, vert.x2 = x1 - dx, vert.y2 = y1 - dy;
        }
        vert.dw = dw;
        vert.d0 = n0.x * dx0 + n0.y * dy0;
        vert.dm0 = -n0.y * dx0 + n0.x * dy0;
        vert.d1 = -(p1.x * dx1 + p1.y * dy1);
        vert.dm1 = -(-p1.y * dx1 + p1.x * dy1);
        
        vert.miter0 = 1.0;// pcap || rcospo < kMiterLimit ? 1.0 : min(44.0, rcospo) * ((dw - 0.5) - 0.5) + 0.5 + copysign(1.0, tpo.x * no.y - tpo.y * no.x) * (dx0 * -tpo.y + dy0 * tpo.x);
        vert.miter1 = 1.0;// ncap || rcoson < kMiterLimit ? 1.0 : min(44.0, rcoson) * ((dw - 0.5) - 0.5) + 0.5 + copysign(1.0, no.x * ton.y - no.y * ton.x) * (dx1 * -ton.y + dy1 * ton.x);

        vert.flags = (inst.iz & ~kPathIndexMask) | InstancesVertex::kIsShape | pcap * InstancesVertex::kPCap | ncap * InstancesVertex::kNCap | isCurve * InstancesVertex::kIsCurve | f0 * InstancesVertex::kPCurve | f1 * InstancesVertex::kNCurve;
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
    vert.clip = distances(clips[iz], dx, dy);
    vert.alpha = alpha;
    vert.iz = iz;
    return vert;
}

fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]],
                                        const device Colorant *colors [[buffer(0)]],
                                        texture2d<float> accumulation [[texture(0)]]
)
{
    float alpha = 1.0;
    if (vert.flags & InstancesVertex::kIsShape) {
        float a, b, c, d, x2, y2, sqdist, sd0, sd1, cap, cap0, cap1;
        bool isCurve = vert.flags & InstancesVertex::kIsCurve;
        bool squareCap = vert.flags & Instance::kSquareCap;
        bool pcap = vert.flags & InstancesVertex::kPCap, ncap = vert.flags & InstancesVertex::kNCap;
        bool f0 = vert.flags & InstancesVertex::kPCurve, f1 = vert.flags & InstancesVertex::kNCurve;
        
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
        
        cap = squareCap ? vert.dw : 0.5;
        cap0 = (pcap ? saturate(cap + vert.d0) : 1.0) * saturate(vert.dw - abs(vert.dm0));
        cap1 = (ncap ? saturate(cap + vert.d1) : 1.0) * saturate(vert.dw - abs(vert.dm1));
        
        sd0 = f0 ? saturate(vert.d0) : 1.0;
        sd1 = f1 ? saturate(vert.d1) : 1.0;

        alpha = min(alpha, min(saturate(vert.miter0), saturate(vert.miter1)));

        alpha = cap0 * (1.0 - sd0) + cap1 * (1.0 - sd1) + (sd0 + sd1 - 1.0) * alpha;
//        alpha = 0.25;
    } else
    if (vert.u != FLT_MAX) {
        alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
        alpha = vert.flags & Instance::kEvenOdd ? 1.0 - abs(fmod(alpha, 2.0) - 1.0) : min(1.0, alpha);
    }
    Colorant color = colors[vert.iz];
    float ma = 0.003921568627 * alpha * vert.alpha * saturate(vert.clip.x) * saturate(vert.clip.z) * saturate(vert.clip.y) * saturate(vert.clip.w);
    return { color.r * ma, color.g * ma, color.b * ma, color.a * ma };
}
