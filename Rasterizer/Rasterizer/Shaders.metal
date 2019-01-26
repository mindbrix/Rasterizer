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
    enum Type { kNull = 0, kRect, kCircle };
    uint8_t src0, src1, src2, src3;
    AffineTransform ctm;
    int type;
};

struct Quad {
    short lx, ly, ux, uy, ox, oy;
    unsigned int idx;
    float cover;
};

struct Segment {
    float x0, y0, x1, y1;
};

struct Edge {
    Quad quad;
    Segment segments[kSegmentsCount];
};

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
    float x = select(quad.lx, quad.ux, vid & 1) / *width * 2.0 - 1.0;
    float y = select(quad.ly, quad.uy, vid >> 1) / *height * 2.0 - 1.0;
    float z = (quad.idx * 2 + 2) / float(*pathCount * 2 + 2);
    
    device Colorant& paint = paints[quad.idx];
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
    float x0, y0, x1, y1;
    float x2, y2, x3, y3;
    float x4, y4, x5, y5;
    float x6, y6, x7, y7;
};

vertex EdgesVertex edges_vertex_main(device Edge *edges [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Edge& edge = edges[iid];
    device Quad& quad = edge.quad;
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
    slx = max(floor(slx), float(quad.lx)), sly = floor(sly), suy = ceil(suy);
    float lx = quad.ox + slx - quad.lx, ux = quad.ox + quad.ux - quad.lx;
    float ly = quad.oy + sly - quad.ly, uy = quad.oy + suy - quad.ly;
    float dx = select(lx, ux, vid & 1), x = dx / *width * 2.0 - 1.0;
    float dy = select(ly, uy, vid >> 1), y = dy / *height * 2.0 - 1.0;
    float tx = -(quad.lx - quad.ox) - (dx - 0.5), ty = -(quad.ly - quad.oy) - (dy - 0.5);
    
    EdgesVertex vert;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.x0 = segments[0].x0 + tx, vert.y0 = segments[0].y0 + ty;
    vert.x1 = segments[0].x1 + tx, vert.y1 = segments[0].y1 + ty;
    vert.x2 = segments[1].x0 + tx, vert.y2 = segments[1].y0 + ty;
    vert.x3 = segments[1].x1 + tx, vert.y3 = segments[1].y1 + ty;
    vert.x4 = segments[2].x0 + tx, vert.y4 = segments[2].y0 + ty;
    vert.x5 = segments[2].x1 + tx, vert.y5 = segments[2].y1 + ty;
    vert.x6 = segments[3].x0 + tx, vert.y6 = segments[3].y0 + ty;
    vert.x7 = segments[3].x1 + tx, vert.y7 = segments[3].y1 + ty;
    return vert;
}

fragment float4 edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    float winding = 0;
    winding += edgeWinding(vert.x0, vert.y0, vert.x1, vert.y1);
    winding += edgeWinding(vert.x2, vert.y2, vert.x3, vert.y3);
    winding += edgeWinding(vert.x4, vert.y4, vert.x5, vert.y5);
    winding += edgeWinding(vert.x6, vert.y6, vert.x7, vert.y7);
    return float4(winding);
}


#pragma mark - Quads

struct QuadsVertex
{
    float4 position [[position]];
    float4 color;
    float d0, d1, d2, d3;
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
    float dx = select(quad.lx, quad.ux, vid & 1), u = dx / *width, du = (quad.lx - quad.ox) / *width, x = u * 2.0 - 1.0;
    float dy = select(quad.ly, quad.uy, vid >> 1), v = dy / *height, dv = (quad.ly - quad.oy) / *height, y = v * 2.0 - 1.0;
    float z = (quad.idx * 2 + 1) / float(*pathCount * 2 + 2);
    bool solid = quad.ox == kSolidQuad;
    
    device Colorant& paint = paints[quad.idx];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0;

    QuadsVertex vert;
    vert.position = float4(x, y, z, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    
    device AffineTransform& ctm = paint.ctm;
    if (ctm.a == 0.0 && ctm.b == 0.0)
        vert.d0 = vert.d1 = vert.d2 = vert.d3 = 1.0;
    else {
        float rlab, rlcd, det, vx, vy;
        rlab = rsqrt(ctm.a * ctm.a + ctm.b * ctm.b), rlcd = rsqrt(ctm.c * ctm.c + ctm.d * ctm.d);
        det = ctm.a * ctm.d - ctm.b * ctm.c;
        vx = ctm.tx - dx, vy = ctm.ty - dy;
        vert.d0 = (vx * ctm.b - vy * ctm.a) * copysign(rlab, det) + 0.5;
        vx = (ctm.tx + ctm.a) - dx, vy = (ctm.ty + ctm.b) - dy;
        vert.d1 = (vx * ctm.d - vy * ctm.c) * copysign(rlcd, det) + 0.5;
        vx = (ctm.tx + ctm.a + ctm.c) - dx, vy = (ctm.ty + ctm.b + ctm.d) - dy;
        vert.d2 = (vx * -ctm.b - vy * -ctm.a) * copysign(rlab, det) + 0.5;
        vx = (ctm.tx + ctm.c) - dx, vy = (ctm.ty + ctm.d) - dy;
        vert.d3 = (vx * -ctm.d - vy * -ctm.c) * copysign(rlcd, det) + 0.5;
        vert.u = u - du, vert.v = v - dv;
    }
    vert.cover = quad.cover;
    vert.even = false;
    vert.solid = solid;
    return vert;
}

fragment float4 quads_fragment_main(QuadsVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    float clip = saturate(vert.d0) * saturate(vert.d1) * saturate(vert.d2) * saturate(vert.d3);
    float winding = vert.solid ? 1.0 : vert.cover + accumulation.sample(s, float2(vert.u, 1.0 - vert.v)).x;
    float alpha = abs(winding);
    return vert.color * clip * (vert.even ? (1.0 - abs(fmod(alpha, 2.0) - 1.0)) : (min(1.0, alpha)));
}

#pragma mark - Shapes

struct ShapesVertex
{
    float4 position [[position]];
    float4 color;
    float u, v;
    float d0, d1, d2, d3;
    uint type;
};

vertex ShapesVertex shapes_vertex_main(device Colorant *shapes [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Colorant& shape = shapes[iid];
    device AffineTransform& ctm = shape.ctm;
    float rlab = rsqrt(ctm.a * ctm.a + ctm.b * ctm.b), rlcd = rsqrt(ctm.c * ctm.c + ctm.d * ctm.d);
    float cosine = min(1.0, (ctm.a * ctm.c + ctm.b * ctm.d) * rlab * rlcd);
    float dilation = 0.7071067812 * rsqrt(1.0 - cosine * cosine);
    float tx = dilation * rlab, ty = dilation * rlcd;
    float ix = vid & 1 ? 1.0 + tx : -tx, iy = vid >> 1 ? 1.0 + ty : -ty;
    float dx = ix * ctm.a + iy * ctm.c + ctm.tx, u = dx / *width, x = u * 2.0 - 1.0;
    float dy = ix * ctm.b + iy * ctm.d + ctm.ty, v = dy / *height, y = v * 2.0 - 1.0;
    float r = shape.src2 / 255.0, g = shape.src1 / 255.0, b = shape.src0 / 255.0, a = shape.src3 / 255.0;
    float det = ctm.a * ctm.d - ctm.b * ctm.c;
    float vx, vy;
    ShapesVertex vert;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    vert.u = ix, vert.v = iy;
    vx = ctm.tx - dx, vy = ctm.ty - dy;
    vert.d0 = (vx * ctm.b - vy * ctm.a) * copysign(rlab, det) + 0.5;
    vx = (ctm.tx + ctm.a) - dx, vy = (ctm.ty + ctm.b) - dy;
    vert.d1 = (vx * ctm.d - vy * ctm.c) * copysign(rlcd, det) + 0.5;
    vx = (ctm.tx + ctm.a + ctm.c) - dx, vy = (ctm.ty + ctm.b + ctm.d) - dy;
    vert.d2 = (vx * -ctm.b - vy * -ctm.a) * copysign(rlab, det) + 0.5;
    vx = (ctm.tx + ctm.c) - dx, vy = (ctm.ty + ctm.d) - dy;
    vert.d3 = (vx * -ctm.d - vy * -ctm.c) * copysign(rlcd, det) + 0.5;
    vert.type = shape.type;
    return vert;
}

fragment float4 shapes_fragment_main(ShapesVertex vert [[stage_in]])
{
    switch (vert.type) {
        case Colorant::kRect:
            return vert.color * saturate(vert.d0) * saturate(vert.d1) * saturate(vert.d2) * saturate(vert.d3);
        case Colorant::kCircle: {
            float d = vert.d0 + vert.d2, r = d * 0.5, x = r - min(vert.d0, vert.d2), y = r - min(vert.d1, vert.d3);
            return vert.color * saturate(r - sqrt(x * x + y * y));
        }
    }
    return { 1.0, 0.0, 0.0, 1.0 };
}
