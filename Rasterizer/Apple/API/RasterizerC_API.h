//
//  RasterizerC_API.h
//  Rasterizer
//
//  Created by Nigel Barber on 26/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#ifndef RasterizerC_API_h
#import <CoreGraphics/CoreGraphics.h>


typedef void *RasterizerPath;

RasterizerPath PathAlloc();
void PathMoveTo(RasterizerPath path, double x, double y);
void PathLineTo(RasterizerPath path, double x, double y);
void PathQuadTo(RasterizerPath path, double x1, double y1, double x2, double y2);
void PathCubicTo(RasterizerPath path, double x1, double y1, double x2, double y2, double x3, double y3);
void PathClose(RasterizerPath path);
void PathAddRect(RasterizerPath path, CGRect rect);
void PathAddEllipse(RasterizerPath path, CGRect rect);
void PathAddCGPath(RasterizerPath path, CGPathRef cgPath);
CGRect PathGetBounds(RasterizerPath path);
void PathFree(RasterizerPath path);


#define RasterizerC_API_h


#endif /* RasterizerC_API_h */
