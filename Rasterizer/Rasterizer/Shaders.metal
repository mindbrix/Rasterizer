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
    short prev, next;
};
struct Instance {
    enum Type { kEvenOdd = 1 << 24, kRounded = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kEndCap = 1 << 28, kOutlines = 1 << 29, kPoints = 1 << 30, kMolecule = 1 << 31 };
    union { Quad quad;  Outline outline; };
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
    return { 0.5 + d0, 0.5 + d1, 0.5 - d0 + det * rlab, 0.5 - d1 + det * rlcd };
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
    float cover, r;
    bool isShape, even, sampled, isCurve;
};

vertex InstancesVertex instances_vertex_main(
            const device Colorant *paints [[buffer(0)]], const device Instance *instances [[buffer(1)]],
            const device Transform *affineTransforms [[buffer(4)]], const device Transform *clips [[buffer(5)]],
            const device float *widths [[buffer(6)]],
            constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
            constant uint *pathCount [[buffer(13)]],
            uint vid [[vertex_id]], uint iid [[instance_id]])
{
    InstancesVertex vert;
    constexpr float err = 1.0 / 32.0;
    const device Instance& inst = instances[iid];
    const device Transform& m = affineTransforms[inst.iz & kPathIndexMask];
    float f = 1.0, visible = 1.0, dx, dy;
    if (inst.iz & Instance::kOutlines) {
        const device Segment& o = inst.outline.s;
        const device Segment& p = instances[iid + inst.outline.prev].outline.s;
        const device Segment& n = instances[iid + inst.outline.next].outline.s;
        float x0 = m.a * o.x0 + m.c * o.y0 + m.tx, y0 = m.b * o.x0 + m.d * o.y0 + m.ty;
        float x1 = m.a * o.x1 + m.c * o.y1 + m.tx, y1 = m.b * o.x1 + m.d * o.y1 + m.ty;
        float px = m.a * p.x0 + m.c * p.y0 + m.tx, py = m.b * p.x0 + m.d * p.y0 + m.ty;
        float nx = m.a * n.x1 + m.c * n.y1 + m.tx, ny = m.b * n.x1 + m.d * n.y1 + m.ty;
        bool points = inst.iz & Instance::kPoints;
        bool pcap = points || inst.outline.prev == 0, ncap = points || inst.outline.next == 0;
        float2 vo = float2(x1 - x0, y1 - y0);
        float2 vp = select(float2(x0 - px, y0 - py), -vo, pcap);
        float2 vn = select(float2(nx - x1, ny - y1), vo, ncap);
        float lo = sqrt(dot(vo, vo)), rp = rsqrt(dot(vp, vp)), rn = rsqrt(dot(vn, vn));
        float2 no = vo / lo, np = vp * rp, nn = vn * rn;
        
        bool pcurve = inst.outline.prev == -1 && (as_type<uint>(o.x0) & 1) == 1 && (as_type<uint>(p.x0) & 3) == (as_type<uint>(o.x0) & 3);
        bool ncurve = inst.outline.next == 1 && (as_type<uint>(o.x0) & 1) == 1 && (as_type<uint>(o.x0) & 3) == (as_type<uint>(n.x0) & 3);
        vert.isCurve = pcurve || ncurve;
        float cpx, cpy, area;
        if (pcurve)
            cpx = 0.5 * x1 + (x0 - 0.25 * (px + x1)), cpy = 0.5 * y1 + (y0 - 0.25 * (py + y1));
        else
            cpx = 0.5 * x0 + (x1 - 0.25 * (x0 + nx)), cpy = 0.5 * y0 + (y1 - 0.25 * (y0 + ny));
        area = (x1 - x0) * (cpy - y0) - (y1 - y0) * (cpx - x0);
        vert.isCurve &= abs(area) > 1.0;
        
        const float ow = vert.isCurve ? 0.5 * abs(-no.y * (cpx - x0) + no.x * (cpy - y0)) : 0.0, width = points ? lo : widths[inst.iz & kPathIndexMask], cw = max(1.0, width), dw = 1.0 + 2.0 * ow + cw;
        f = width / cw;
        const float endCap = (inst.iz & Instance::kEndCap) == 0 ? 0.5 : 0.5 * dw;
        pcap |= dot(np, no) < -0.5 || rp * dw > 1e3;
        ncap |= dot(no, nn) < -0.5 || rn * dw > 1e3;
        np = pcap ? no : np, nn = ncap ? no : nn;
        float2 tpo = normalize(np + no), ton = normalize(no + nn);
        float spo = 0.5 * dw / (tpo.y * np.y + tpo.x * np.x);
        float son = 0.5 * dw / (ton.y * no.y + ton.x * no.x);
        float vx0 = -tpo.y * spo, vy0 = tpo.x * spo, vx1 = -ton.y * son, vy1 = ton.x * son;
        float lp = endCap * float(pcap), ln = endCap * float(ncap);
        float epx = lp * no.x, epy = lp * no.y;
        float t = ((vo.x + epx) * vy1 - (vo.y + epy) * vx1) / (vx0 * vy1 - vy0 * vx1);
        // -y, x
        bool isRight = vid & 1, isUp = vid & 2, crossed = (!isRight && t > 0.0 && t < 1.0) || (isRight && t < 0.0 && t > -1.0);
        float sgn = isRight ? -1.0 : 1.0, ex = err * no.x * float(crossed), ey = err * no.y * float(crossed);
        dx = select(x0 + vx0 * sgn - lp * no.x - ex, x1 + vx1 * sgn + ln * no.x + ex, isUp);
        dy = select(y0 + vy0 * sgn - lp * no.y - ey, y1 + vy1 * sgn + ln * no.y + ey, isUp);
        float ix = vx0 * t + x0 - epx - no.y * copysign(err, t), iy = vy0 * t + y0 - epy + no.x * copysign(err, t);
        dx = select(dx, ix, crossed), dy = select(dy, iy, crossed);
        float tl = t < 0.0 ? 1.0 : min(1.0, t), tr = t > 0.0 ? -1.0 : max(-1.0, t), dt = vid & 1 ? tr : tl;
        visible = float(o.x0 != FLT_MAX && lo > 1e-2);
        vert.shape = float4(pcap ? (isUp ? lo + lp + ln : 0.0) : 1e6, 0.5 * dw * (1.0 - dt) - ow, ncap ? (isUp ? 0.0 : lo + lp + ln) : 1e6, 0.5 * dw * (1.0 + dt) - ow);
        vert.r = (inst.iz & Instance::kRounded) == 0 ? 1.0 : 0.5 * dw;
        vert.u = ((cpx - x1) * (dy - y1) - (cpy - y1) * (dx - x1)) / area;
        vert.v = ((x1 - x0) * (dy - y0) - (y1 - y0) * (dx - x0)) / area;
        vert.isShape = true;
    } else {
        const device Cell& cell = inst.quad.cell;
        dx = select(cell.lx, cell.ux, vid & 1);
        dy = select(cell.ly, cell.uy, vid >> 1);
        vert.u = (dx - (cell.lx - cell.ox)) / *width, vert.v = (dy - (cell.ly - cell.oy)) / *height;
        vert.cover = inst.quad.cover;
        vert.isShape = false;
        vert.even = inst.iz & Instance::kEvenOdd;
        vert.sampled = (inst.iz & Instance::kSolidCell) == 0;
    }
    float x = dx / *width * 2.0 - 1.0, y = dy / *height * 2.0 - 1.0;
    float z = ((inst.iz & kPathIndexMask) * 2 + 1) / float(*pathCount * 2 + 2);
    const device Colorant& paint = paints[(inst.iz & kPathIndexMask)];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0 * f;
    
    vert.position = float4(x * visible, y * visible, z * visible, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    vert.clip = distances(clips[inst.iz & kPathIndexMask], dx, dy);
    return vert;
}

fragment float4 instances_fragment_main(InstancesVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    float alpha = 1.0, x, y;
    if (vert.isShape) {
        if (vert.r == 1.0)
            alpha = (saturate(vert.shape.x) - (1.0 - saturate(vert.shape.z))) * (saturate(vert.shape.y) - (1.0 - saturate(vert.shape.w)));
        else {
            x = max(0.0, vert.r - min(vert.shape.x, vert.shape.z)), y = max(0.0, vert.r - min(vert.shape.y, vert.shape.w));
            alpha = saturate(vert.r - sqrt(x * x + y * y));
        }
        if (vert.isCurve) {
            float a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v), invdet = 1.0 / (a * d - b * c);
            a *= invdet, b *= invdet, c *= invdet, d *= invdet;
            float x2 = b * vert.v - d * vert.u, y2 = vert.u * c - vert.v * a;
            float x0 = x2 + d, y0 = y2 - c;
            float x1 = x2 - b, y1 = y2 + a;
            
//            float yt1 = max(1e-9, saturate((ax * (x1 - x0) + ay * (y1 - y0)) / (ax * ax + ay * ay)));
//            float discriminant = sqrt(fma(yt1, fma(-2.0, t, yt1), t));
//            t = saturate(t / (yt1 + discriminant));
            
            float ax = x2 - x0, ay = y2 - y0, t = saturate(-(ax * x0 + ay * y0) / (ax * ax + ay * ay)), s = 1.0 - t;
            float tx0 = s * x0 + t * x1, tx1 = s * x1 + t * x2;
            float ty0 = s * y0 + t * y1, ty1 = s * y1 + t * y2;
            float vx = tx1 - tx0, vy = ty1 - ty0;
            alpha = saturate(0.5 * (vert.shape.y + vert.shape.w) - abs(tx1 * ty0 - ty1 * tx0) * rsqrt(vx * vx + vy * vy));
        }
    } else if (vert.sampled) {
        alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
        alpha = vert.even ? 1.0 - abs(fmod(alpha, 2.0) - 1.0) : min(1.0, alpha);
    }
    return vert.color * alpha * saturate(vert.clip.x) * saturate(vert.clip.z) * saturate(vert.clip.y) * saturate(vert.clip.w);
}
