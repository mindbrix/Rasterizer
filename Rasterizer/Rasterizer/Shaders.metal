//
//  Shaders.metal
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#include <metal_stdlib>
using namespace metal;

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
};

vertex EdgesVertex edges_vertex_main(device Paint *paints [[buffer(0)]], device Edge *edges [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Edge& edge = edges[iid];
    device Quad& quad = edge.quad;
    float lx = quad.ox, ux = lx + quad.ux - quad.lx;
    float ly = quad.oy, uy = ly + quad.uy - quad.ly;
    float x = select(lx, ux, vid & 1) / *width * 2.0 - 1.0;
    float y = select(ly, uy, vid >> 1) / *height * 2.0 - 1.0;
    
    EdgesVertex vert;
    vert.position = float4(x, y, 1.0, 1.0);
    vert.cover = edge.cover;
    return vert;
}

fragment float4 edges_fragment_main(EdgesVertex vert [[stage_in]])
{
    return float4(1.0);
}


#pragma mark - Quads

struct QuadsVertex
{
    float4 position [[position]];
    float4 color;
};

vertex QuadsVertex quads_vertex_main(device Paint *paints [[buffer(0)]], device Quad *quads [[buffer(1)]],
                                     constant float *width [[buffer(10)]], constant float *height [[buffer(11)]],
                                     constant uint *pathCount [[buffer(13)]],
                                     uint vid [[vertex_id]], uint iid [[instance_id]])
{
    device Quad& quad = quads[iid];
    float x = select(quad.lx, quad.ux, vid & 1) / *width * 2.0 - 1.0;
    float y = select(quad.ly, quad.uy, vid >> 1) / *height * 2.0 - 1.0;
    float z = (quad.idx * 2 + 1) / float(*pathCount * 2 + 2);
    
    device Paint& paint = paints[quad.idx];
    float r = paint.src2 / 255.0, g = paint.src1 / 255.0, b = paint.src0 / 255.0, a = paint.src3 / 255.0;
    QuadsVertex vert;
    vert.position = float4(x, y, z, 1.0);
    vert.color = float4(r * a, g * a, b * a, a);
    return vert;
}

fragment float4 quads_fragment_main(QuadsVertex vert [[stage_in]])
{
    return vert.color;
}

