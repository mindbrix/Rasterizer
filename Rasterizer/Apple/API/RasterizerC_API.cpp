//
//  RasterizerC_API.cpp
//  Rasterizer
//
//  Created by Nigel Barber on 26/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <stdio.h>
#import <assert.h>
#import <float.h>
#import "RasterizerC_API.h"
#import "RasterizerCG.hpp"


RasterizerPath PathAlloc() {
    return new Ra::Path();
}
void PathMoveTo(RasterizerPath path, double x, double y) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->moveTo(x, y);
}
void PathLineTo(RasterizerPath path, double x, double y) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->lineTo(x, y);
}
void PathQuadTo(RasterizerPath path, double x1, double y1, double x2, double y2) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->quadTo(x1, y1, x2, y2);
}
void PathCubicTo(RasterizerPath path, double x1, double y1, double x2, double y2, double x3, double y3) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->cubicTo(x1, y1, x2, y2, x3, y3);
}
void PathClose(RasterizerPath path) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->close();
}
void PathAddRect(RasterizerPath path, CGRect rect) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->addBounds(RaCG::BoundsFromCGRect(rect));
}
void PathAddEllipse(RasterizerPath path, CGRect rect) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    g->addEllipse(RaCG::BoundsFromCGRect(rect));
}
void PathAddCGPath(RasterizerPath path, CGPathRef cgPath) {
    Ra::Path p = *(Ra::Path *)path;
    RaCG::writeCGPathToPath(cgPath, p);
}
CGRect PathGetBounds(RasterizerPath path) {
    Ra::Geometry *g = ((Ra::Path *)path)->ptr;
    return RaCG::CGRectFromBounds(g->bounds);
}
void PathFree(RasterizerPath path) {
    delete (Ra::Path *)path;
}
