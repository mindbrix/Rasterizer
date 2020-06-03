//
//  Rasterizer3D.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 21/05/2020.
//  Copyright © 2020 @mindbrix. All rights reserved.
//

// http://www.songho.ca/opengl/gl_transform.html
//
struct Transform3D {
    static Transform3D Translation(float tx, float ty, float tz) {
        return {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            tx, ty, tz, 1.f
        };
    }
    static Transform3D RotateAroundY(float theta) {
        return {
            cosf(theta), 0.f, -sinf(theta), 0.f,
            0.f, 1.f, 0.f, 0.f,
            sinf(theta), 0.f, cosf(theta), 0.f,
            0.f, 0.f, 0.f, 1.f
        };
    }
    static Transform3D Projection(float r, float t, float n, float f) {
        return {
            n / r, 0.f, 0.f, 0.f,
            0.f, n / t, 0.f, 0.f,
            0.f, 0.f, -(f + n) / (f - n), -1.f,
            0.f, 0.f, -2.f * f * n / (f - n), 0.f
        };
    }
    static Transform3D Transform(Ra::Transform m) {
        return {
            m.a, m.b, 0.f, 0.f,
            m.c, m.d, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            m.tx, m.ty, 0.f, 1.f
        };
    }
    inline Transform3D concat(const Transform3D& n) const {
        return {
            m0 * n.m0 + m4 * n.m1 + m8 * n.m2 + m12 * n.m3,
            m1 * n.m0 + m5 * n.m1 + m9 * n.m2 + m13 * n.m3,
            m2 * n.m0 + m6 * n.m1 + m10 * n.m2 + m14 * n.m3,
            m3 * n.m0 + m7 * n.m1 + m11 * n.m2 + m15 * n.m3,
            m0 * n.m4 + m4 * n.m5 + m8 * n.m6 + m12 * n.m7,
            m1 * n.m4 + m5 * n.m5 + m9 * n.m6 + m13 * n.m7,
            m2 * n.m4 + m6 * n.m5 + m10 * n.m6 + m14 * n.m7,
            m3 * n.m4 + m7 * n.m5 + m11 * n.m6 + m15 * n.m7,
            m0 * n.m8 + m4 * n.m9 + m8 * n.m10 + m12 * n.m11,
            m1 * n.m8 + m5 * n.m9 + m9 * n.m10 + m13 * n.m11,
            m2 * n.m8 + m6 * n.m9 + m10 * n.m10 + m14 * n.m11,
            m3 * n.m8 + m7 * n.m9 + m11 * n.m10 + m15 * n.m11,
            m0 * n.m12 + m4 * n.m13 + m8 * n.m14 + m12 * n.m15,
            m1 * n.m12 + m5 * n.m13 + m9 * n.m14 + m13 * n.m15,
            m2 * n.m12 + m6 * n.m13 + m10 * n.m14 + m14 * n.m15,
            m3 * n.m12 + m7 * n.m13 + m11 * n.m14 + m15 * n.m15
        };
    }
    inline void map2DTo3D(float x, float y, float sx, float sy, float& ox, float& oy) {
        float w = m3 * x + m7 * y + m15;
        ox = (m0 * x + m4 * y + m12) / w * sx + sx;
        oy = (m1 * x + m5 * y + m13) / w * sy + sy;
    }
    float m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15;
};

static Ra::Scene create3DScene(Ra::Scene scene) {
    Ra::Scene scene3D;
    Ra::Bounds bounds = scene.bounds();
    float w = bounds.ux - bounds.lx, h = bounds.uy - bounds.ly, dim = w > h ? w : h;
    Transform3D projection = Transform3D::Projection(w / 2, h / 2, dim, dim * 2);
    Transform3D view = Transform3D::Translation(0, 0, -dim / 2);
    Transform3D model = Transform3D::RotateAroundY(M_PI / 8).concat(Transform3D::Translation(-w / 2, -h / 2, 0));
    Transform3D mvp = projection.concat(view).concat(model);
    
    float x0, y0, x1, y1, x2, y2, sx = w / 2, sy = h / 2;
    for (int i = 0; i < scene.count; i++) {
        Ra::Path& path = scene.paths[i], path3D;
        Transform3D mat = mvp.concat(Transform3D::Transform(scene.ctms[i]));
        for (size_t index = 0; index < path->typesSize; ) {
            float *p = path->points.base + index * 2;
            switch (path->types[index]) {
                case Ra::Geometry::kMove:
                    mat.map2DTo3D(p[0], p[1], sx, sy, x0, y0);
                    path3D->moveTo(x0, y0);
                    index++;
                    break;
                case Ra::Geometry::kLine:
                    mat.map2DTo3D(p[0], p[1], sx, sy, x0, y0);
                    path3D->lineTo(x0, y0);
                    index++;
                    break;
                case Ra::Geometry::kQuadratic:
                    mat.map2DTo3D(p[0], p[1], sx, sy, x0, y0);
                    mat.map2DTo3D(p[2], p[3], sx, sy, x1, y1);
                    path3D->quadTo(x0, y0, x1, y1);
                    index += 2;
                    break;
                case Ra::Geometry::kCubic:
                    mat.map2DTo3D(p[0], p[1], sx, sy, x0, y0);
                    mat.map2DTo3D(p[2], p[3], sx, sy, x1, y1);
                    mat.map2DTo3D(p[4], p[5], sx, sy, x2, y2);
                    path3D->cubicTo(x0, y0, x1, y1, x2, y2);
                    index += 3;
                    break;
                case Ra::Geometry::kClose:
                    path3D->close();
                    index++;
                    break;
            }
        }
        scene3D.addPath(path3D, Ra::Transform(), scene.colors[i], scene.widths[i], scene.flags[i]);
    }
    return scene3D;
}
