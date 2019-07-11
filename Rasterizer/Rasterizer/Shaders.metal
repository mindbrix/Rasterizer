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
    float a, b, c, d, tx, ty;
};

struct Colorant {
    uint8_t src0, src1, src2, src3;
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
    int iy, begin, base;
};
struct Outline {
    Segment s;
    float width;
    short prev, next;
};
struct Instance {
    enum Type { kRect = 1 << 24, kCircle = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kShapes = 1 << 28, kOutlines = 1 << 29, kOpaque = 1 << 30, kMolecule = 1 << 31 };
    union { Quad quad;  Transform unit;  Outline outline; };
    uint32_t iz;
};
struct EdgeCell {
    Cell cell;
    uint32_t im, base;
};
struct Edge {
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
    return { d0 + 0.5, d1 + 0.5, 0.5 - (d0 - det * rlab), 0.5 - (d1 - det * rlcd) };
}

float winding(float x0, float y0, float x1, float y1) {
    float w0, w1, cover, dx, dy, a0;
    w0 = saturate(y0), w1 = saturate(y1), cover = w1 - w0;
    if (cover == 0.0 || (x0 <= 0.0 && x1 <= 0.0))
        return cover;
    dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.0 ? w0 : w1) - y0) - dy * (1.0 - x0);
    return saturate(-a0 / (abs(dx) * cover + dy)) * cover;
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
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7;
};

vertex FastEdgesVertex fast_edges_vertex_main(const device Edge *edges [[buffer(1)]], const device Segment *segments [[buffer(2)]],
                                     const device EdgeCell *edgeCells [[buffer(3)]], const device Transform *affineTransforms [[buffer(4)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    FastEdgesVertex vert;
    
    const device Edge& edge = edges[iid];
    const device EdgeCell& edgeCell = edgeCells[edge.ic];
    const device Cell& cell = edgeCell.cell;
    const device Transform& m = affineTransforms[edgeCell.im];
    const device Segment *s = & segments[edgeCell.base + edge.i0];
    thread float *dst = & vert.x0;
    dst[0] = m.a * s->x0 - m.b * s->y0 + m.tx, dst[1] = m.b * s->x0 + m.a * s->y0 + m.ty;
    dst[2] = m.a * s->x1 - m.b * s->y1 + m.tx, dst[3] = m.b * s->x1 + m.a * s->y1 + m.ty;
    float slx = min(dst[0], dst[2]), sly = min(dst[1], dst[3]), suy = max(dst[1], dst[3]);
    s++, dst += 4;
    for (int i = 1; i < kFastSegments; i++, s++, dst += 4) {
        if (i + edge.i0 < edge.i1) {
            dst[0] = m.a * s->x0 - m.b * s->y0 + m.tx, dst[1] = m.b * s->x0 + m.a * s->y0 + m.ty;
            dst[2] = m.a * s->x1 - m.b * s->y1 + m.tx, dst[3] = m.b * s->x1 + m.a * s->y1 + m.ty;
            slx = min(slx, min(dst[0], dst[2])), sly = min(sly, min(dst[1], dst[3])), suy = max(suy, max(dst[1], dst[3]));
        } else
            dst[0] = dst[1] = dst[2] = dst[3] = 0.0;
    }
    float ox = clamp(select(floor(slx), float(cell.ux), vid & 1), float(cell.lx), float(cell.ux));
    float oy = clamp(select(floor(sly), ceil(suy), vid >> 1), float(cell.ly), float(cell.uy));
    float dx = cell.ox - cell.lx + ox, x = dx / *width * 2.0 - 1.0, tx = 0.5 - ox;
    float dy = cell.oy - cell.ly + oy, y = dy / *height * 2.0 - 1.0, ty = 0.5 - oy;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 += tx, vert.y0 += ty, vert.x1 += tx, vert.y1 += ty, vert.x2 += tx, vert.y2 += ty, vert.x3 += tx, vert.y3 += ty;
    vert.x4 += tx, vert.y4 += ty, vert.x5 += tx, vert.y5 += ty, vert.x6 += tx, vert.y6 += ty, vert.x7 += tx, vert.y7 += ty;
    return vert;
}

fragment float4 fast_edges_fragment_main(FastEdgesVertex vert [[stage_in]])
{
    return winding(vert.x0, vert.y0, vert.x1, vert.y1) + winding(vert.x2, vert.y2, vert.x3, vert.y3)
         + winding(vert.x4, vert.y4, vert.x5, vert.y5) + winding(vert.x6, vert.y6, vert.x7, vert.y7);
}

#pragma mark - Edges

struct EdgesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3;
};

vertex EdgesVertex edges_vertex_main(const device Edge *edges [[buffer(1)]], const device Segment *segments [[buffer(2)]],
                                     const device EdgeCell *edgeCells [[buffer(3)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    EdgesVertex vert;
    const device Edge& edge = edges[iid];
    const device EdgeCell& edgeCell = edgeCells[edge.ic];
    const device Cell& cell = edgeCell.cell;
    const device Segment& s0 = segments[edgeCell.base + edge.i0];
    const device Segment& s1 = segments[edgeCell.base + edge.i1];
    vert.x0 = s0.x0, vert.y0 = s0.y0, vert.x1 = s0.x1, vert.y1 = s0.y1;
    float slx = min(vert.x0, vert.x1);
    float sly = min(vert.y0, vert.y1);
    float suy = max(vert.y0, vert.y1);
    
    if (edge.i1 == kNullIndex)
        vert.x2 = vert.y2 = vert.x3 = vert.y3 = 0.0;
    else {
        vert.x2 = s1.x0, vert.y2 = s1.y0, vert.x3 = s1.x1, vert.y3 = s1.y1;
        slx = min(slx, min(vert.x2, vert.x3));
        sly = min(sly, min(vert.y2, vert.y3));
        suy = max(suy, max(vert.y2, vert.y3));
    }
    float ox = select(floor(slx), float(cell.ux), vid & 1);
    float oy = select(floor(sly), ceil(suy), vid >> 1);
    float dx = cell.ox - cell.lx + ox, x = dx / *width * 2.0 - 1.0, tx = 0.5 - ox;
    float dy = cell.oy - cell.ly + oy, y = dy / *height * 2.0 - 1.0, ty = 0.5 - oy;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 += tx, vert.y0 += ty, vert.x1 += tx, vert.y1 += ty, vert.x2 += tx, vert.y2 += ty, vert.x3 += tx, vert.y3 += ty;

    return vert;
}

fragment float4 edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return winding(vert.x0, vert.y0, vert.x1, vert.y1) + winding(vert.x2, vert.y2, vert.x3, vert.y3);
}

#pragma mark - Instances

struct InstancesVertex
{
    float4 position [[position]];
    float4 color;
    float4 clip, shape;
    float u, v;
    float cover;
    bool isShape, even, solid, circle;
};

vertex InstancesVertex instances_vertex_main(const device Colorant *paints [[buffer(0)]], const device Instance *instances [[buffer(1)]],
                                       const device Transform *affineTransforms [[buffer(4)]], const device Transform *clips [[buffer(5)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant uint *pathCount [[buffer(13)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    InstancesVertex vert;
    vert.isShape = true;
    constexpr float err = 1e-2;
    const device Instance& inst = instances[iid];
    float area = 1.0, visible = 1.0, dx, dy;
    if (inst.iz & Instance::kOutlines) {
        Transform m = { 1, 0, 0, 1, 0, 0 };
        const device Segment& o = inst.outline.s;
        const device Segment& p = instances[iid + inst.outline.prev].outline.s;
        const device Segment& n = instances[iid + inst.outline.next].outline.s;
        const float dw = 1.0 + inst.outline.width;
        float x0 = m.a * o.x0 + m.c * o.y0 + m.tx, y0 = m.b * o.x0 + m.d * o.y0 + m.ty;
        float x1 = m.a * o.x1 + m.c * o.y1 + m.tx, y1 = m.b * o.x1 + m.d * o.y1 + m.ty;
        float px = m.a * p.x0 + m.c * p.y0 + m.tx, py = m.b * p.x0 + m.d * p.y0 + m.ty;
        float nx = m.a * n.x1 + m.c * n.y1 + m.tx, ny = m.b * n.x1 + m.d * n.y1 + m.ty;
        float2 vo = float2(x1 - x0, y1 - y0), vp = float2(x0 - px, y0 - py), vn = float2(nx - x1, ny - y1);
        float ro = rsqrt(dot(vo, vo)), rp = rsqrt(dot(vp, vp)), rn = rsqrt(dot(vn, vn));
        float2 no = vo * ro, np = vp * rp, nn = vn * rn;
        np = dot(np, no) < -0.939692620785908 || rp > 1e2 || o.x0 != p.x1 || o.y0 != p.y1 ? no : np;
        nn = dot(no, nn) < -0.939692620785908 || rn > 1e2 || o.x1 != n.x0 || o.y1 != n.y0 ? no : nn;
        
        float2 tpo = normalize(np + no), ton = normalize(no + nn);
//        tpo = dot(np, no) < -0.866025403784439 ? float2(tpo.y, -tpo.x) : tpo;
//        ton = dot(no, nn) < -0.866025403784439 ? float2(ton.y, -ton.x) : ton;
        float spo = 0.5 * dw / (tpo.y * np.y + tpo.x * np.x);
        float son = 0.5 * dw / (ton.y * no.y + ton.x * no.x);
        float vx0 = -tpo.y * spo, vy0 = tpo.x * spo, vx1 = -ton.y * son, vy1 = ton.x * son;
        float t = (vo.x * vy1 - vo.y * vx1) / (vx0 * vy1 - vy0 * vx1);
        float ix = vx0 * t + x0 - no.y * copysign(err, t), iy = vy0 * t + y0 + no.x * copysign(err, t);
        // -y, x
        float sgn = vid & 1 ? -1.0 : 1.0;
        dx = select(x0 + vx0 * sgn, x1 + vx1 * sgn, vid >> 1);
        dy = select(y0 + vy0 * sgn, y1 + vy1 * sgn, vid >> 1);
        bool left = t > 0.0 && t < 1.0, right = t < 0.0 && t > -1.0, crossed = ((vid & 1) == 0 && left) || ((vid & 1) && right);
        dx = select(dx, ix, crossed);
        dy = select(dy, iy, crossed);
        visible = float(o.x0 != FLT_MAX && ro < 1e2);
        vert.shape = float4(1e6, vid & 1 ? dw : 0.0, 1e6, vid & 1 ? 0.0 : dw);
    } else if (inst.iz & Instance::kCircle || inst.iz & Instance::kRect) {
        const device Transform& m = affineTransforms[inst.iz & kPathIndexMask];
        Transform ctm = {
            inst.unit.a * m.a + inst.unit.b * m.c, inst.unit.a * m.b + inst.unit.b * m.d,
            inst.unit.c * m.a + inst.unit.d * m.c, inst.unit.c * m.b + inst.unit.d * m.d,
            inst.unit.tx * m.a + inst.unit.ty * m.c + m.tx, inst.unit.tx * m.b + inst.unit.ty * m.d + m.ty };
        area = min(1.0, 0.5 * abs(ctm.d * ctm.a - ctm.b * ctm.c));
        float rlab = rsqrt(ctm.a * ctm.a + ctm.b * ctm.b), rlcd = rsqrt(ctm.c * ctm.c + ctm.d * ctm.d);
        float cosine = min(1.0, (ctm.a * ctm.c + ctm.b * ctm.d) * rlab * rlcd);
        float dilation = (1.0 + 0.5 * (rsqrt(area) - 1.0)) * 0.7071067812 * rsqrt(1.0 - cosine * cosine);
        float tx = dilation * rlab, ty = dilation * rlcd;
        float ix = vid & 1 ? 1.0 + tx : -tx, iy = vid >> 1 ? 1.0 + ty : -ty;
        dx = ix * ctm.a + iy * ctm.c + ctm.tx, dy = ix * ctm.b + iy * ctm.d + ctm.ty;
        vert.shape = distances(ctm, dx, dy);
    } else {
        const device Cell& cell = inst.quad.cell;
        dx = select(cell.lx, cell.ux, vid & 1);
        dy = select(cell.ly, cell.uy, vid >> 1);
        vert.u = (dx - (cell.lx - cell.ox)) / *width, vert.v = (dy - (cell.ly - cell.oy)) / *height;
        vert.cover = inst.quad.cover;
        vert.even = false;
        vert.solid = inst.iz & Instance::kSolidCell;
        vert.isShape = false;
    }
    float x = dx / *width * 2.0 - 1.0, y = dy / *height * 2.0 - 1.0;
    float z = ((inst.iz & kPathIndexMask) * 2 + 1) / float(*pathCount * 2 + 2);
    const device Colorant& paint = paints[(inst.iz & kPathIndexMask)];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0 * sqrt(area);
    
    vert.position = float4(x * visible, y * visible, z * visible, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    vert.clip = distances(clips[inst.iz & kPathIndexMask], dx, dy);
    vert.circle = inst.iz & Instance::kCircle;
    return vert;
}

fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    if (vert.isShape) {
        float r = max(1.0, (min(vert.shape.x + vert.shape.z, vert.shape.y + vert.shape.w)) * 0.5 * float(vert.circle));
        float x = r - min(r, min(vert.shape.x, vert.shape.z)), y = r - min(r, min(vert.shape.y, vert.shape.w));
        float shape = saturate(r - sqrt(x * x + y * y));
        return vert.color * shape * saturate(vert.clip.x) * saturate(vert.clip.y) * saturate(vert.clip.z) * saturate(vert.clip.w);
    } else {
        float alpha = 1.0;
        if (!vert.solid) {
            alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
            alpha = vert.even ? (1.0 - abs(fmod(alpha, 2.0) - 1.0)) : (min(1.0, alpha));
        }
        return vert.color * alpha * saturate(vert.clip.x) * saturate(vert.clip.y) * saturate(vert.clip.z) * saturate(vert.clip.w);
    }
}
