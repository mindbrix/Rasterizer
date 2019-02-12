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

struct AffineTransform {
    float a, b, c, d, tx, ty;
};

struct Colorant {
    uint8_t src0, src1, src2, src3;
    AffineTransform ctm;
};

struct Cell {
    short lx, ly, ux, uy, ox, oy;
};

struct SuperCell {
    Cell cell;
    float cover;
    short iy, count;
    uint32_t idx, begin;
};

struct Quad {
    enum Type { kNull = 0, kRect, kCircle, kCell, kSolidCell, kShapes, kOutlines };
    union {
        SuperCell super;
        AffineTransform unit;
    };
    uint32_t iz;
};

struct Segment {
    float x0, y0, x1, y1;
};

struct Edge {
    Cell cell;
    Segment segments[kSegmentsCount];
};

float4 distances(AffineTransform ctm, float dx, float dy) {
    float det, rlab, rlcd, vx, vy;
    float4 d;
    det = ctm.a * ctm.d - ctm.b * ctm.c;
    rlab = copysign(rsqrt(ctm.a * ctm.a + ctm.b * ctm.b), det);
    rlcd = copysign(rsqrt(ctm.c * ctm.c + ctm.d * ctm.d), det);
    vx = ctm.tx - dx, vy = ctm.ty - dy;
    d.x = (vx * ctm.b - vy * ctm.a) * rlab + 0.5;
    vx = (ctm.tx + ctm.a) - dx, vy = (ctm.ty + ctm.b) - dy;
    d.y = (vx * ctm.d - vy * ctm.c) * rlcd + 0.5;
    vx = (ctm.tx + ctm.a + ctm.c) - dx, vy = (ctm.ty + ctm.b + ctm.d) - dy;
    d.z = (vx * -ctm.b - vy * -ctm.a) * rlab + 0.5;
    vx = (ctm.tx + ctm.c) - dx, vy = (ctm.ty + ctm.d) - dy;
    d.w = (vx * -ctm.d - vy * -ctm.c) * rlcd + 0.5;
    return d;
}

float edgeWinding(float x0, float y0, float x1, float y1) {
    float sy0 = saturate(y0), sy1 = saturate(y1);
    float coverage = sy1 - sy0;
    
    if (coverage == 0.0 || saturate(x0) + saturate(x1) == 0.0)
        return coverage;
    
    float dxdy = (x1 - x0) / (y1 - y0);
    float sx0 = fma(sy0 - y0, dxdy, x0), sx1 = fma(sy1 - y0, dxdy, x0);
    float minx = min(sx0, sx1), range = abs(sx1 - sx0);
    float t0 = saturate(-minx / range), t1 = saturate((1.0 - minx) / range);
    float area = (saturate(sx0) + saturate(sx1)) * 0.5;
    return coverage * (t1 - ((t1 - t0) * area));
}

#pragma mark - Opaques

struct OpaquesVertex
{
    float4 position [[position]];
    float4 color;
};

vertex OpaquesVertex opaques_vertex_main(device Colorant *paints [[buffer(0)]], device Quad *quads [[buffer(1)]],
                                         constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                         constant uint *reverse [[buffer(12)]], constant uint *pathCount [[buffer(13)]],
                                         uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Quad& quad = quads[*reverse - 1 - iid];
    device Cell& cell = quad.super.cell;
    float x = select(cell.lx, cell.ux, vid & 1) / *width * 2.0 - 1.0;
    float y = select(cell.ly, cell.uy, vid >> 1) / *height * 2.0 - 1.0;
    float z = ((quad.iz & 0xFFFFFF) * 2 + 2) / float(*pathCount * 2 + 2);
    
    device Colorant& paint = paints[(quad.iz & 0xFFFFFF)];
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

#pragma mark - Edges

struct EdgesVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3;
};

vertex EdgesVertex edges_vertex_main(device Edge *edges [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Edge& edge = edges[iid];
    device Cell& cell = edge.cell;
    device Segment *segments = edge.segments;
    
    float slx = FLT_MAX, sly = FLT_MAX, suy = -FLT_MAX;
    for (int i = 0; i < kSegmentsCount; i++) {
        device Segment& s = segments[i];
        if (s.y0 != s.y1) {
            slx = min(slx, min(s.x0, s.x1));
            sly = min(sly, min(s.y0, s.y1));
            suy = max(suy, max(s.y0, s.y1));
        }
    }
    slx = max(floor(slx), float(cell.lx)), sly = floor(sly), suy = ceil(suy);
    float lx = cell.ox + slx - cell.lx, ux = cell.ox + cell.ux - cell.lx;
    float ly = cell.oy + sly - cell.ly, uy = cell.oy + suy - cell.ly;
    float dx = select(lx, ux, vid & 1), x = dx / *width * 2.0 - 1.0;
    float dy = select(ly, uy, vid >> 1), y = dy / *height * 2.0 - 1.0;
    float tx = -(cell.lx - cell.ox) - (dx - 0.5), ty = -(cell.ly - cell.oy) - (dy - 0.5);
    
    EdgesVertex vert;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 = segments[0].x0 + tx, vert.y0 = segments[0].y0 + ty;
    vert.x1 = segments[0].x1 + tx, vert.y1 = segments[0].y1 + ty;
    vert.x2 = segments[1].x0 + tx, vert.y2 = segments[1].y0 + ty;
    vert.x3 = segments[1].x1 + tx, vert.y3 = segments[1].y1 + ty;
    return vert;
}

fragment float4 edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    float winding = 0;
    winding += edgeWinding(vert.x0, vert.y0, vert.x1, vert.y1);
    winding += edgeWinding(vert.x2, vert.y2, vert.x3, vert.y3);
    return float4(winding);
}


#pragma mark - Quads

struct QuadsVertex
{
    float4 position [[position]];
    float4 color;
    float4 clip;
    float u, v;
    float cover;
    bool even, solid;
};

vertex QuadsVertex quads_vertex_main(device Colorant *paints [[buffer(0)]], device Quad *quads [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant uint *pathCount [[buffer(13)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Quad& quad = quads[iid];
    uint32_t type = quad.iz >> 24;
    
    float z = ((quad.iz & 0xFFFFFF) * 2 + 1) / float(*pathCount * 2 + 2);
    device Colorant& paint = paints[(quad.iz & 0xFFFFFF)];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0;
    
    QuadsVertex vert;
    device Cell& cell = quad.super.cell;
    float dx = select(cell.lx, cell.ux, vid & 1), u = dx / *width, du = (cell.lx - cell.ox) / *width, x = u * 2.0 - 1.0;
    float dy = select(cell.ly, cell.uy, vid >> 1), v = dy / *height, dv = (cell.ly - cell.oy) / *height, y = v * 2.0 - 1.0;
    vert.position = float4(x, y, z, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    vert.clip = distances(paint.ctm, dx, dy);
    vert.u = u - du, vert.v = v - dv;
    vert.cover = quad.super.cover;
    vert.even = false;
    vert.solid = type == Quad::kSolidCell;
    return vert;
}

fragment float4 quads_fragment_main(QuadsVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    float alpha = 1.0;
    if (!vert.solid) {
        alpha = abs(vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x);
        alpha = vert.even ? (1.0 - abs(fmod(alpha, 2.0) - 1.0)) : (min(1.0, alpha));
    }
    return vert.color * alpha * saturate(vert.clip.x) * saturate(vert.clip.y) * saturate(vert.clip.z) * saturate(vert.clip.w);
}

#pragma mark - Shapes

struct ShapesVertex
{
    float4 position [[position]];
    float4 color;
    float4 clip, shape;
    bool circle;
};

vertex ShapesVertex shapes_vertex_main(device Colorant *paints [[buffer(0)]], device Quad *quads [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant uint *pathCount [[buffer(13)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Quad& quad = quads[iid];
    AffineTransform ctm = quad.unit;
    if (quad.iz >> 24 == Quad::kOutlines) {
        float dx = ctm.c - ctm.a, dy = ctm.d - ctm.b, rl = rsqrt(dx * dx + dy * dy), nx = dx * rl, ny = dy * rl;
        ctm = { ny, -nx, dx, dy, ctm.a - ny * 0.5f, ctm.b + nx * 0.5f };
    }
    float area = min(1.0, 0.5 * abs(ctm.d * ctm.a - ctm.b * ctm.c));
    float rlab = rsqrt(ctm.a * ctm.a + ctm.b * ctm.b), rlcd = rsqrt(ctm.c * ctm.c + ctm.d * ctm.d);
    float cosine = min(1.0, (ctm.a * ctm.c + ctm.b * ctm.d) * rlab * rlcd);
    float dilation = (1.0 + 0.5 * (rsqrt(area) - 1.0)) * 0.7071067812 * rsqrt(1.0 - cosine * cosine);
    float tx = dilation * rlab, ty = dilation * rlcd;
    float ix = vid & 1 ? 1.0 + tx : -tx, iy = vid >> 1 ? 1.0 + ty : -ty;
    float dx = ix * ctm.a + iy * ctm.c + ctm.tx, u = dx / *width, x = u * 2.0 - 1.0;
    float dy = ix * ctm.b + iy * ctm.d + ctm.ty, v = dy / *height, y = v * 2.0 - 1.0;
    
    float z = ((quad.iz & 0xFFFFFF) * 2 + 1) / float(*pathCount * 2 + 2);
    device Colorant& paint = paints[(quad.iz & 0xFFFFFF)];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0 * area;
    
    ShapesVertex vert;
    vert.position = float4(x, y, z, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    vert.clip = distances(paint.ctm, dx, dy);
    vert.shape = distances(ctm, dx, dy);
    vert.circle = quad.iz >> 24 == Quad::kCircle;
    return vert;
}

fragment float4 shapes_fragment_main(ShapesVertex vert [[stage_in]])
{
    float r, x, y, shape;
    r = max(1.0, (min(vert.shape.x + vert.shape.z, vert.shape.y + vert.shape.w)) * 0.5 * float(vert.circle));
    x = r - min(r, min(vert.shape.x, vert.shape.z)), y = r - min(r, min(vert.shape.y, vert.shape.w));
    shape = saturate(r - sqrt(x * x + y * y));
    return vert.color * shape * saturate(vert.clip.x) * saturate(vert.clip.y) * saturate(vert.clip.z) * saturate(vert.clip.w);
}
