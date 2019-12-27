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
    uint8_t src0, src1, src2, src3;
};

struct Point {
    uint16_t x, y;
};
struct Segment {
    float x0, y0, x1, y1;
};

struct Cell {
    uint16_t lx, ly, ux, uy, ox, oy;
};

struct Quad {
    Cell cell;
    short cover;
    uint16_t count;
    int iy, begin, base, idx;
};
struct Outline {
    Segment s;
    short prev, next;
};
struct SceneInstance {
    uint32_t idx, iz;
};
struct Instance {
    enum Type { kEvenOdd = 1 << 24, kRounded = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kEndCap = 1 << 28, kOutlines = 1 << 29,    kMolecule = 1 << 31 };
    union { Quad quad;  Outline outline; };
    uint32_t iz;
};
struct EdgeCell {
    Cell cell;
    uint32_t iz, base;
};
struct Edge {
    enum Flags { a0 = 1 << 31, a1 = 1 << 30, kMask = ~(a0 | a1) };
    uint32_t ic;
    uint16_t i0, i1;
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

float winding(float x0, float y0, float x1, float y1, float w0, float w1) {
    float cover, dx, dy, a0, t, a, b;
    cover = w1 - w0, dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.0 ? w0 : w1) - y0) - dy * (1.0 - x0);
    dx = abs(dx), t = -a0 / fma(dx, cover, dy), dy = abs(dy);
    a = min(dx, dy) * 0.4142135624, b = max(dx, dy);
    return saturate((t - 0.5) * b / (b - a) + 0.5) * cover;
}
float winding(float x0, float y0, float x1, float y1) {
    float w0 = saturate(y0), w1 = saturate(y1), cover = w1 - w0;
    if (cover == 0.0 || (x0 <= 0.0 && x1 <= 0.0))
        return cover;
    return winding(x0, y0, x1, y1, w0, w1);
}
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2, bool a, float iy) {
    if (x1 == FLT_MAX)
        return winding(x0, y0, x2, y2);
    float w0 = saturate(a ? y0 : iy), w1 = saturate(a ? iy : y2), w = w1 - w0;
    if (w && (x0 > 0.0 || x1 > 0.0 || x2 > 0.0)) {
        float ay, by, cy, t, s;
        ay = y0 + y2 - y1 - y1, by = 2.0 * (y1 - y0), cy = y0 - 0.5 * (w0 + w1);
        t = abs(ay) < kQuadraticFlatness ? -cy / by : (-by + copysign(sqrt(max(0.0, by * by - 4.0 * ay * cy)), w)) / ay * 0.5;
        s = 1.0 - t, w = winding(s * x0 + t * x1, s * y0 + t * y1, s * x1 + t * x2, s * y1 + t * y2, w0, w1);
    }
    return w;
}
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2) {
    if (x1 == FLT_MAX)
        return winding(x0, y0, x2, y2);
    float w0 = saturate(y0), w2 = saturate(y2);
    if (x0 <= 0.0 && x1 <= 0.0 && x2 <= 0.0)
        return w2 - w0;
    float w = 0.0, ay, by, cy, t, s, w1;  bool mono;
    ay = y2 - y1, by = y1 - y0, mono = abs(ay) < kMonotoneFlatness || abs(by) < kMonotoneFlatness || (ay > 0.0) == (by > 0.0);
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

vertex OpaquesVertex opaques_vertex_main(const device Colorant *paints [[buffer(0)]], const device Instance *instances [[buffer(1)]],
                                         constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                         constant uint *reverse [[buffer(12)]], constant uint *pathCount [[buffer(13)]],
                                         uint vid [[vertex_id]], uint iid [[instance_id]])
{
    const device Instance& inst = instances[*reverse - 1 - iid];
    const device Cell& cell = inst.quad.cell;
    float x = select(cell.lx, cell.ux, vid & 1) / *width * 2.0 - 1.0;
    float y = select(cell.ly, cell.uy, vid >> 1) / *height * 2.0 - 1.0;
    float z = ((inst.iz & kPathIndexMask) * 2 + 2) / float(*pathCount * 2 + 2);
    
    const device Colorant& paint = paints[(inst.iz & kPathIndexMask)];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0;
    
    OpaquesVertex vert;
    vert.position = float4(x, y, z, 1.0);
    vert.color = float4(r, g, b, 1.0);
    return vert;
}

fragment float4 opaques_fragment_main(OpaquesVertex vert [[stage_in]])
{
    return vert.color;
}

#pragma mark - Fast Edges

struct FastEdgesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8;
};

vertex FastEdgesVertex fast_edges_vertex_main(const device Edge *edges [[buffer(1)]],
                                const device Segment *segments [[buffer(2)]],
                                const device EdgeCell *edgeCells [[buffer(3)]],
                                const device Transform *affineTransforms [[buffer(4)]],
                                const device Bounds *bounds [[buffer(7)]],
                                const device Point *points [[buffer(8)]],
                                constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                constant bool *useCurves [[buffer(14)]],
                                uint vid [[vertex_id]], uint iid [[instance_id]])
{
    FastEdgesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device EdgeCell& edgeCell = edgeCells[edge.ic];
    const device Cell& cell = edgeCell.cell;
    const device Transform& m = affineTransforms[edgeCell.iz];
    const device Segment *s = & segments[edgeCell.base + edge.i0];
    thread float *dst = & vert.x0;
    int i; float slx, sly, suy, visible = 1.0;
    if (1) {
        const device Point *pt = & points[edgeCell.base + edge.i0];
        int curve;
        if ((pt + 1)->x == 0xFFFF && (pt + 1)->y == 0xFFFF) {
            visible = 0.0;
            for (dst = & vert.x0, i = 0; i <= kFastSegments; i++, dst += 4)
                dst[0] = dst[1] = 0.0;
            for (dst = & vert.x1, i = 0; i < kFastSegments; i++, dst += 4)
                dst[0] = FLT_MAX;
            slx = sly = suy = 0.0;
        } else {
            const device Bounds& b = bounds[edgeCell.iz];
            float w, h, tx, ty, x, y, x0, y0, x1, y1, px, py, nx, ny, cpx, cpy;
            w = (b.ux - b.lx) / 32767.0, h = (b.uy - b.ly) / 32767.0;
            tx = b.lx * m.a + b.ly * m.c + m.tx, ty = b.lx * m.b + b.ly * m.d + m.ty;
            x = pt->x & 0x7FFF, y = pt->y & 0x7FFF;
            slx = dst[0] = x0 = x * w * m.a + y * h * m.c + tx;
            sly = suy = dst[1] = y0 = x * w * m.b + y * h * m.d + ty;
            for (pt++, dst += 2, i = 0; i < kFastSegments; i++, pt++, dst += 4, x0 = x1, y0 = y1) {
                if (x1 == FLT_MAX || (pt->x == 0xFFFF && pt->y == 0xFFFF))
                    x1 = dst[0] = FLT_MAX, dst[2] = dst[-2], dst[3] = dst[-1];
                else {
                    x = pt->x & 0x7FFF, y = pt->y & 0x7FFF;
                    dst[2] = x1 = x * w * m.a + y * h * m.c + tx, dst[3] = y1 = x * w * m.b + y * h * m.d + ty;
                    slx = min(slx, dst[2]), sly = min(sly, dst[3]), suy = max(suy, dst[3]);
                    curve = (((pt - 1)->x & 0x8000) >> 14) | (((pt - 1)->y & 0x8000) >> 15);
                    if (!*useCurves || curve == 0)
                        dst[0] = FLT_MAX;
                    else {
                        if (curve == 1) {
                            x = (pt + 1)->x & 0x7FFF, y = (pt + 1)->y & 0x7FFF;
                            nx = x * w * m.a + y * h * m.c + tx, ny = x * w * m.b + y * h * m.d + ty;
                            cpx = 0.25f * (x0 - nx) + x1, cpy = 0.25f * (y0 - ny) + y1;
                        } else {
                            x = (pt - 2)->x & 0x7FFF, y = (pt - 2)->y & 0x7FFF;
                            px = x * w * m.a + y * h * m.c + tx, py = x * w * m.b + y * h * m.d + ty;
                            cpx = 0.25f * (x1 - px) + x0, cpy = 0.25f * (y1 - py) + y0;
                        }
                        slx = min(slx, cpx), sly = min(sly, cpy), suy = max(suy, cpy);
                        if (abs((cpx - x0) * (y1 - cpy) - (cpy - y0) * (x1 - cpx)) < 1.0)
                            dst[0] = FLT_MAX;
                        else
                            dst[0] = cpx, dst[1] = cpy;
                    }
                }
            }
        }
    } else {
        dst = & vert.x0;
        slx = dst[0] = m.a * s->x0 - m.b * s->y0 + m.tx;
        sly = suy = dst[1] = m.b * s->x0 + m.a * s->y0 + m.ty;
        float x0, y0, x1, y1, x2, y2, cpx, cpy;
        for (dst += 4, i = 0; i < kFastSegments; i++, s++, dst += 4) {
            if (s->x0 != FLT_MAX) {
                dst[0] = m.a * s->x1 - m.b * s->y1 + m.tx, dst[1] = m.b * s->x1 + m.a * s->y1 + m.ty;
                slx = min(slx, dst[0]), sly = min(sly, dst[1]), suy = max(suy, dst[1]);
                uint curve = as_type<uint>(s->x0) & 3;
                if (!*useCurves || curve == 0)
                    dst[-2] = FLT_MAX;
                else {
                    if (curve == 1) {
                        x0 = dst[-4], y0 = dst[-3], x1 = dst[0], y1 = dst[1];
                        x2 = m.a * (s + 1)->x1 - m.b * (s + 1)->y1 + m.tx, y2 = m.b * (s + 1)->x1 + m.a * (s + 1)->y1 + m.ty;
                        cpx = 0.25f * (x0 - x2) + x1, cpy = 0.25f * (y0 - y2) + y1;
                    } else {
                        x0 = m.a * (s - 1)->x0 - m.b * (s - 1)->y0 + m.tx, y0 = m.b * (s - 1)->x0 + m.a * (s - 1)->y0 + m.ty;
                        x1 = dst[-4], y1 = dst[-3], x2 = dst[0], y2 = dst[1];
                        cpx = 0.25f * (x2 - x0) + x1, cpy = 0.25f * (y2 - y0) + y1;
                    }
                    slx = min(slx, cpx), sly = min(sly, cpy), suy = max(suy, cpy);
                    if (abs((cpx - x0) * (y1 - cpy) - (cpy - y0) * (x1 - cpx)) < 1.0)
                        dst[-2] = FLT_MAX;
                    else
                        dst[-2] = cpx, dst[-1] = cpy;
                }
                
            } else
                dst[0] = dst[-4], dst[1] = dst[-3], dst[-2] = FLT_MAX;
        }
    }
    float ox = clamp(select(floor(slx), float(edge.i1), vid & 1), float(cell.lx), float(edge.i1));
    float oy = clamp(select(floor(sly), ceil(suy), vid >> 1), float(cell.ly), float(cell.uy));
    float dx = cell.ox - cell.lx + ox, x = dx / *width * 2.0 - 1.0, tx = 0.5 - ox;
    float dy = cell.oy - cell.ly + oy, y = dy / *height * 2.0 - 1.0, ty = 0.5 - oy;
    vert.position = float4(x, y, 1.0, visible);
    for (dst = & vert.x0, i = 0; i <= kFastSegments; i++, dst += 4)
        dst[0] += tx, dst[1] += ty;
    for (dst = & vert.x1, i = 0; i < kFastSegments; i++, dst += 4)
        if (dst[0] != FLT_MAX)
            dst[0] += tx, dst[1] += ty;
    return vert;
}

fragment float4 fast_edges_fragment_main(FastEdgesVertex vert [[stage_in]])
{
    return quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2)
    + quadraticWinding(vert.x2, vert.y2, vert.x3, vert.y3, vert.x4, vert.y4)
    + quadraticWinding(vert.x4, vert.y4, vert.x5, vert.y5, vert.x6, vert.y6)
    + quadraticWinding(vert.x6, vert.y6, vert.x7, vert.y7, vert.x8, vert.y8);
}

#pragma mark - Edges

struct EdgesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5;
    float iy0, iy1;
    bool a0, a1;
};

vertex EdgesVertex edges_vertex_main(const device Edge *edges [[buffer(1)]],
                                     const device Segment *segments [[buffer(2)]],
                                     const device EdgeCell *edgeCells [[buffer(3)]],
                                     const device Transform *affineTransforms [[buffer(4)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant bool *useCurves [[buffer(14)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    EdgesVertex vert;
    const device Edge& edge = edges[iid];
    const device EdgeCell& edgeCell = edgeCells[edge.ic & Edge::kMask];
    const device Cell& cell = edgeCell.cell;
    vert.a0 = edge.ic & Edge::a0, vert.a1 = edge.ic & Edge::a1;
    thread float *dst = & vert.x0;
    thread float *iys = & vert.iy0;
    thread bool *as = & vert.a0;
    const device uint16_t *idxes = & edge.i0;
    float slx = cell.ux, sly = FLT_MAX, suy = -FLT_MAX;
    for (int i = 0; i < 2; i++, dst += 6) {
        float x0, y0, x1, y1, x2, y2;
        if (idxes[i] != kNullIndex) {
            const device Segment& s = segments[edgeCell.base + idxes[i]];
            x0 = dst[0] = s.x0, y0 = dst[1] = s.y0, x2 = dst[4] = s.x1, y2 = dst[5] = s.y1;
            bool pcurve = *useCurves && as_type<uint>(s.x0) & 2, ncurve = *useCurves && as_type<uint>(s.x0) & 1;
            if (pcurve) {
                const device Segment& p = segments[edgeCell.base + idxes[i] - 1];
                x1 = x0 + 0.25 * (x2 - p.x0), y1 = y0 + 0.25 * (y2 - p.y0);
            } else if (ncurve) {
                const device Segment& n = segments[edgeCell.base + idxes[i] + 1];
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
    float ox = select(max(floor(slx), float(cell.lx)), float(cell.ux), vid & 1);
    float oy = select(max(floor(sly), float(cell.ly)), min(ceil(suy), float(cell.uy)), vid >> 1);
    float dx = cell.ox - cell.lx + ox, x = dx / *width * 2.0 - 1.0, tx = 0.5 - ox;
    float dy = cell.oy - cell.ly + oy, y = dy / *height * 2.0 - 1.0, ty = 0.5 - oy;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 += tx, vert.y0 += ty, vert.x2 += tx, vert.y2 += ty;
    vert.x3 += tx, vert.y3 += ty, vert.x5 += tx, vert.y5 += ty;
    if (vert.x1 != FLT_MAX)
        vert.x1 += tx, vert.y1 += ty, vert.iy0 += ty;
    if (vert.x4 != FLT_MAX)
        vert.x4 += tx, vert.y4 += ty, vert.iy1 += ty;
    return vert;
}

fragment float4 edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return 0.0 + quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2, vert.a0, vert.iy0)
        + quadraticWinding(vert.x3, vert.y3, vert.x4, vert.y4, vert.x5, vert.y5, vert.a1, vert.iy1);
}

#pragma mark - Instances

struct InstancesVertex
{
    enum Flags { kPCap = 1 << 0, kNCap = 1 << 1, kIsCurve = 1 << 2, kIsShape = 1 << 3 };
    float4 position [[position]];
    float4 color, clip;
    float u, v, cover, dw, d0, d1, dm;
    uint32_t flags;
};

vertex InstancesVertex instances_vertex_main(
            const device Colorant *paints [[buffer(0)]],
            const device Instance *instances [[buffer(1)]],
            const device Transform *affineTransforms [[buffer(4)]],
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
    const device Colorant& paint = paints[iz];
    float alpha = paint.src3 * 0.003921568627, visible = 1.0, dx, dy;
    if (inst.iz & Instance::kOutlines) {
        const device Transform& m = affineTransforms[iz];
        bool pcap = inst.outline.prev == 0, ncap = inst.outline.next == 0;
        const device Segment& o = inst.outline.s;
        const device Segment& p = instances[iid + inst.outline.prev].outline.s;
        const device Segment& n = instances[iid + inst.outline.next].outline.s;
        float x0 = m.a * o.x0 + m.c * o.y0 + m.tx, y0 = m.b * o.x0 + m.d * o.y0 + m.ty;
        float x1 = m.a * o.x1 + m.c * o.y1 + m.tx, y1 = m.b * o.x1 + m.d * o.y1 + m.ty;
        float px = m.a * p.x0 + m.c * p.y0 + m.tx, py = m.b * p.x0 + m.d * p.y0 + m.ty;
        float nx = m.a * n.x1 + m.c * n.y1 + m.tx, ny = m.b * n.x1 + m.d * n.y1 + m.ty;
        
        bool pcurve = (as_type<uint>(o.x0) & 2) != 0, ncurve = (as_type<uint>(o.x0) & 1) != 0;
        float cpx, cpy, ax, ay, bx, by, cx, cy, area;
        ax = x1 - x0, ay = y1 - y0;
        if (pcurve)
            cpx = 0.25 * (x1 - px) + x0, cpy = 0.25 * (y1 - py) + y0;
        else
            cpx = 0.25 * (x0 - nx) + x1, cpy = 0.25 * (y0 - ny) + y1;
        bx = cpx - x0, by = cpy - y0, cx = cpx - x1, cy = cpy - y1;
        area = ax * by - ay * bx;
        bool isCurve = *useCurves && (pcurve || ncurve) && abs(area) > 1.0;
        
        float2 vp = float2(x0 - px, y0 - py), vn = float2(nx - x1, ny - y1);
        float lo = sqrt(ax * ax + ay * ay), rp = rsqrt(dot(vp, vp)), rn = rsqrt(dot(vn, vn));
        float2 no = float2(ax, ay) / lo, np = vp * rp, nn = vn * rn;
        visible = float(lo > 1e-2);
        
        float width = widths[iz], cw = max(1.0, width), dw = 0.5 + 0.5 * cw, ew = (isCurve && (pcap || ncap)) * 0.41 * dw, ow = isCurve ? max(ew, 0.5 * abs(-no.y * bx + no.x * by)) : 0.0, endCap = ((inst.iz & Instance::kEndCap) == 0 ? dw : ew + dw);
        alpha *= width / cw;
        
        pcap |= dot(np, no) < -0.86 || rp * dw > 5e2;
        ncap |= dot(no, nn) < -0.86 || rn * dw > 5e2;
        np = pcap ? no : np, nn = ncap ? no : nn;
        float2 tpo = normalize(np + no), ton = normalize(no + nn);
        float spo = (dw + ow) / (tpo.y * np.y + tpo.x * np.x);
        float son = (dw + ow) / (ton.y * no.y + ton.x * no.x);
        float vx0 = -tpo.y * spo, vy0 = tpo.x * spo, vx1 = -ton.y * son, vy1 = ton.x * son;
        
        float lp = endCap * float(pcap) + err, ln = endCap * float(ncap) + err;
        float px0 = x0 - no.x * lp, py0 = y0 - no.y * lp;
        float px1 = x1 + no.x * ln, py1 = y1 + no.y * ln;
        float t = ((px1 - px0) * vy1 - (py1 - py0) * vx1) / (vx0 * vy1 - vy0 * vx1);
        float tl = t < 0.0 ? 1.0 : min(1.0, t), tr = t > 0.0 ? -1.0 : max(-1.0, t), dt = vid & 1 ? tr : tl;
        dx = vid & 2 ? fma(vx1, dt, px1) : fma(vx0, dt, px0);
        dy = vid & 2 ? fma(vy1, dt, py1) : fma(vy0, dt, py0);
        
        vert.dw = dw;
        float dx0 = dx - x0, dy0 = dy - y0, dx1 = dx - x1, dy1 = dy - y1;
        if (isCurve) {
            vert.u = (cx * dy1 - cy * dx1) / area;
            vert.v = (ax * dy0 - ay * dx0) / area;
            vert.d0 = rsqrt(bx * bx + by * by) * (bx * dx0 + by * dy0);
            vert.d1 = rsqrt(cx * cx + cy * cy) * (cx * dx1 + cy * dy1);
            float mx = 0.25 * x0 + 0.5 * cpx + 0.25 * x1, my = 0.25 * y0 + 0.5 * cpy + 0.25 * y1;
            vert.dm = no.x * (dx - mx) + no.y * (dy - my);
        } else
            vert.d0 = no.x * dx0 + no.y * dy0, vert.d1 = -(no.x * dx1 + no.y * dy1), vert.dm = -no.y * dx0 + no.x * dy0;
        vert.flags = (inst.iz & ~kPathIndexMask) | InstancesVertex::kIsShape | pcap * InstancesVertex::kPCap | ncap * InstancesVertex::kNCap | isCurve * InstancesVertex::kIsCurve;
    } else {
        const device Cell& cell = inst.quad.cell;
        dx = select(cell.lx, cell.ux, vid & 1);
        dy = select(cell.ly, cell.uy, vid >> 1);
        vert.u = (dx - (cell.lx - cell.ox)) / *width, vert.v = (dy - (cell.ly - cell.oy)) / *height;
        vert.cover = inst.quad.cover;
        vert.flags = inst.iz & ~kPathIndexMask;
    }
    float x = dx / *width * 2.0 - 1.0, y = dy / *height * 2.0 - 1.0;
    float z = (iz * 2 + 1) / float(*pathCount * 2 + 2);
    vert.position = float4(x, y, z, visible);
    
    float ma = alpha * 0.003921568627;
    vert.color = float4(paint.src2 * ma, paint.src1 * ma, paint.src0 * ma, alpha);
    vert.clip = distances(clips[iz], dx, dy);
    return vert;
}


fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    float alpha = 1.0;
    if (vert.flags & InstancesVertex::kIsShape) {
        float tl, tu, t, s, a, b, c, d, x2, y2, tx0, tx1, ty0, ty1, vx, vy, dist, sd0, sd1, cap, cap0, cap1;
        if (vert.flags & InstancesVertex::kIsCurve) {
            a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v);
            x2 = b * vert.v - d * vert.u, y2 = vert.u * c - vert.v * a;
            // x0 = x2 + d, y0 = y2 - c, x1 = x2 - b, y1 = y2 + a;

            tl = 0.5 - 0.5 * (-vert.dm / (max(0.0, vert.d0) - vert.dm));
            tu = 0.5 + 0.5 * (vert.dm / (max(0.0, vert.d1) + vert.dm));
            t = vert.dm < 0.0 ? tl : tu, s = 1.0 - t;
            
            tx0 = x2 + s * d + t * -b, tx1 = x2 + s * -b;
            ty0 = y2 + s * -c + t * a, ty1 = y2 + s * a;
            vx = tx1 - tx0, vy = ty1 - ty0;
            dist = (tx1 * ty0 - ty1 * tx0) * rsqrt(vx * vx + vy * vy) / (a * d - b * c);
        } else
            dist = vert.dm;
    
        alpha = saturate(vert.dw - abs(dist));
        
        cap = vert.flags & Instance::kEndCap ? vert.dw : 0.5;
        cap0 = select(
                      saturate(cap + vert.d0) * alpha,
                      saturate(vert.dw - sqrt(vert.d0 * vert.d0 + dist * dist)),
                      vert.flags & Instance::kRounded);
        cap1 = select(
                      saturate(cap + vert.d1) * alpha,
                      saturate(vert.dw - sqrt(vert.d1 * vert.d1 + dist * dist)),
                      vert.flags & Instance::kRounded);
        
        sd0 = vert.flags & InstancesVertex::kPCap ? saturate(vert.d0) : 1.0;
        sd1 = vert.flags & InstancesVertex::kNCap ? saturate(vert.d1) : 1.0;
        
        alpha = cap0 * (1.0 - sd0) + cap1 * (1.0 - sd1) + (sd0 - (1.0 - sd1)) * alpha;
    } else if ((vert.flags & Instance::kSolidCell) == 0) {
        alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
        alpha = vert.flags & Instance::kEvenOdd ? 1.0 - abs(fmod(alpha, 2.0) - 1.0) : min(1.0, alpha);
    }
    return vert.color * alpha * saturate(vert.clip.x) * saturate(vert.clip.z) * saturate(vert.clip.y) * saturate(vert.clip.w);
}
