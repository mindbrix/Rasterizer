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

constexpr sampler s = sampler(coord::normalized, address::clamp_to_zero, mag_filter::linear, min_filter::linear, mip_filter::linear);

struct Paint {
    unsigned char src0, src1, src2, src3;
};

struct Quad {
    short lx, ly, ux, uy, ox, oy;
    unsigned int idx;
};

struct Segment {
    float x0, y0, x1, y1;
};

struct Edge {
    float cover;
    Quad quad;
    Segment segments[4];
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

vertex OpaquesVertex opaques_vertex_main(device Paint *paints [[buffer(0)]], device Quad *quads [[buffer(1)]],
                                         constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                         constant uint *reverse [[buffer(12)]], constant uint *pathCount [[buffer(13)]],
                                         uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Quad& quad = quads[*reverse - 1 - iid];
    float x = select(quad.lx, quad.ux, vid & 1) / *width * 2.0 - 1.0;
    float y = select(quad.ly, quad.uy, vid >> 1) / *height * 2.0 - 1.0;
    float z = (quad.idx * 2 + 2) / float(*pathCount * 2 + 2);
    
    device Paint& paint = paints[quad.idx];
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
    float cover;
    float x0, y0, x1, y1;
    float x2, y2, x3, y3;
    float x4, y4, x5, y5;
    float x6, y6, x7, y7;
};

vertex EdgesVertex edges_vertex_main(device Paint *paints [[buffer(0)]], device Edge *edges [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Edge& edge = edges[iid];
    device Quad& quad = edge.quad;
    device Segment *segments = edge.segments;
    
    float lx = quad.ox, ux = lx + quad.ux - quad.lx;
    float ly = quad.oy, uy = ly + kFatHeight;
    float w = *width * kAccumulateStretch, h = floor(*height / kAccumulateStretch);
    float dx = select(lx, ux, vid & 1), x = dx / w * 2.0 - 1.0;
    float dy = select(ly, uy, vid >> 1), y = dy / h * 2.0 - 1.0;
    float tx = -(quad.lx - quad.ox) + -dx + 0.5, ty = -(quad.ly - quad.oy) + -dy + 0.5;
    
    EdgesVertex vert;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.cover = edge.cover;
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
    float winding = vert.cover;
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
    float2 uv;
    bool even;
};

vertex QuadsVertex quads_vertex_main(device Paint *paints [[buffer(0)]], device Quad *quads [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant uint *pathCount [[buffer(13)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Quad& quad = quads[iid];
    float dx = select(quad.lx, quad.ux, vid & 1), x = dx / *width * 2.0 - 1.0;
    float dy = select(quad.ly, quad.uy, vid >> 1), y = dy / *height * 2.0 - 1.0;
    float z = (quad.idx * 2 + 1) / float(*pathCount * 2 + 2);
    float w = *width * kAccumulateStretch, h = floor(*height / kAccumulateStretch);
    
    device Paint& paint = paints[quad.idx];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0;
    QuadsVertex vert;
    vert.position = float4(x, y, z, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    vert.uv = float2((quad.ox + dx - quad.lx) / w, 1.0 - (quad.oy + dy - quad.ly) / h);
    vert.even = false;
    return vert;
}

fragment float4 quads_fragment_main(QuadsVertex vert [[stage_in]], texture2d<float> accumulation [[texture(0)]])
{
    float winding = accumulation.sample(s, vert.uv).x;
    float alpha = abs(winding);
    return vert.color * (vert.even ? (1.0 - abs(fmod(alpha, 2.0) - 1.0)) : (min(1.0, alpha)));
}

