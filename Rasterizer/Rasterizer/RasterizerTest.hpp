//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "RasterizerState.hpp"
#import <time.h>

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

struct RasterizerTest {
    void addTestScenes(Ra::Bounds b, RasterizerFont& font, Ra::SceneList& list) {
        Ra::Scene scene;
        Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255);
        if (0) {
            float w = 10, r = w;
            for (int i = 0; i < 20; i++, r += w) {
                Ra::Path circle;  circle.ref->addEllipse(Ra::Bounds(-r, -r, r, r));
                scene.addPath(circle, Ra::Transform(), i & 1 ? black : red, w, 0);
            }
        }
        if (0) {
            Ra::Bounds bounds(0, 0, 100, 100);
            Ra::Path rectPath; rectPath->addEllipse(bounds), rectPath->addEllipse(bounds.inset(20, 20));
            Ra::Path bbPath;  bbPath.ref->addEllipse(bounds);
            Ra::Path quadPath;  quadPath.ref->moveTo(100, 100), quadPath.ref->quadTo(0, 100, 00, 00);//, quadPath.ref->quadTo(100, 0, 0, 0);
            Ra::Path endsPath;  endsPath.ref->moveTo(0, 0), endsPath.ref->lineTo(0, 100);//, endsPath.ref->lineTo(1e-2, 100);//, endsPath.ref->quadTo(50, 110, 100, 100);
            
            if (1) {
                scene.addPath(rectPath, Ra::Transform(), black, 0.f, Ra::Scene::kFillEvenOdd);
//                scene.addPath(quadPath, Ra::Transform(), black, 0.f, 0);
            }
            else {
                float w = 10;
                scene.addPath(quadPath, Ra::Transform(), black, w, 0);
                scene.addPath(quadPath, Ra::Transform(1, 0, 0, 1, 0, w * 20), black, w, Ra::Scene::kOutlineEndCap);
                scene.addPath(quadPath, Ra::Transform(1, 0, 0, 1, 0, w * 40), black, w, Ra::Scene::kOutlineEndCap | Ra::Scene::kOutlineRounded);
                scene.addPath(endsPath, Ra::Transform(1, 0, 0, 1, 0, w * 60), black, w, Ra::Scene::kOutlineEndCap);// | Ra::Scene::kOutlineRounded);
            }
        }
        if (0) {
            float w = 400, inset = 1;
            Ra::Bounds bounds(0, 0, w, w);
            Ra::Path rectPath;  rectPath.ref->addBounds(bounds), rectPath.ref->addBounds(bounds.inset(20, 20));
            Ra::Path ellipsePath;
            for (int i = 0; i < 100; i++)
                ellipsePath.ref->addEllipse(bounds.inset(i * inset, i * inset));
            scene.addPath(ellipsePath, Ra::Transform(), black, 0.f, Ra::Scene::kFillEvenOdd);
            
        }
        if (0)
            scene.addPath(createPhyllotaxisPath(100), Ra::Transform(), black, 0.f, Ra::Scene::kFillEvenOdd);
        if (0)
            writePhyllotaxisToScene(100000, scene);
        list.addScene(scene);
        
        if (0) {
            float phi = 0.5f * (sqrtf(5.f) - 1.f);
            float size = 20.f, width = size * phi;
            list.addScene(createGridScene(10000, size, size * phi, width != 0.f, black));
        }
        if (0) {
            createConcentrichronScene(Ra::Bounds(0, 0, b.ux - b.lx, b.uy - b.ly), font, concentrichron.empty());
            bounds = b;
            writeList(list.empty());
        }
        if (0 && list.scenes.size()) {
            Ra::SceneList list3D;
            for (int i = 0; i < list.scenes.size(); i++)
                list3D.addScene(create3DScene(list.scenes[i]));
            list.empty().addList(list3D);
        }
        if (0 && list.scenes.size())
            src.empty().addList(list), dst.empty().addList(list, Ra::Scene::kCloneCTMs | Ra::Scene::kCloneWidths | Ra::Scene::kCloneColors | Ra::Scene::kCloneFlags);
    }
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
                float *p = & path->points[0] + index * 2;
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
    static void writeConcentrichronList(Ra::SceneList& src, Ra::Bounds b, Ra::SceneList& list) {
        const float monthdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        time_t t = time(NULL);
        struct tm *lt = localtime(& t);
        struct timeval tv;  gettimeofday(& tv, NULL);
        int year = lt->tm_year + 1900;
        bool isLeapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        float fsec = (lt->tm_sec + tv.tv_usec / 1e6f) / 60.f, fmin = (lt->tm_min + fsec) / 60.f, fhour = (lt->tm_hour + fmin) / 24.f;
        float fday = (lt->tm_wday + fhour) / 7.f, fdate = (lt->tm_mday - 1 + fhour) / 31.f;
        float daysthismonth = monthdays[lt->tm_mon] + (lt->tm_mon == 1 && isLeapYear ? 1.f : 0.f);
        float fmonth = (lt->tm_mon + (lt->tm_mday - 1) / daysthismonth) / 12.f;
        float fyear = (lt->tm_year - 120 + (lt->tm_yday / (isLeapYear ? 365.f : 364.f))) / 10.f;
        float ftimes[8] = { 0, fyear, fmonth, fdate, fday, fhour, fmin, fsec };
        
        for (int i = 0; i < src.scenes.size(); i++)
            if (i > 0 && i < 8)
                list.addScene(src.scenes[i], 0, Ra::Transform().concat(Ra::Transform::rst(ftimes[i] * 2.f * M_PI), 0.5f * (b.lx + b.ux), 0.5f * (b.ly + b.uy)));
            else
                list.addScene(src.scenes[i]);
    }
    static void createConcentrichronScene(Ra::Bounds b, RasterizerFont& font, Ra::SceneList& list) {
        const char *days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
        const char *dates[31] = { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "11th", "12th", "13th", "14th", "15th", "16th", "17th", "18th", "19th", "20th", "21st", "22nd", "23rd", "24th", "25th", "26th", "27th", "28th", "29th", "30th", "31st" };
        const char *months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        const char *years[10] = { "2020", "2021", "2022", "2023", "2024", "2025", "2026", "2027", "2028", "2029" };
        const char **labels[8] = { NULL, years, months, dates, days, NULL, NULL, NULL };
        const int divisions[8] = { 0, 10, 12, 31, 7, 24, 60, 60 };
        const Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255), grey0(245, 245, 245, 255), grey1(250, 250, 250, 255);
        const float strokeWidth = 0.5f, arcWidth = 2.f;
        float w = b.ux - b.lx, h = b.uy - b.ly, dim = w < h ? w : h, inset = dim / 30.f;
        float cx = 0.5f * (b.lx + b.ux), cy = 0.5f * (b.ly + b.uy);
        Ra::Bounds outer = b.inset(0.5f * (w - dim + strokeWidth), 0.5f * (h - dim + strokeWidth));
        Ra::Scene background;
        for (int i = 1; i < 8; i++) {
            Ra::Bounds inner = outer.inset(inset * i, inset * i);
            Ra::Path bgPath;  bgPath->addEllipse(inner.inset(-inset * 0.5f, -inset * 0.5f));
            background.addPath(bgPath, Ra::Transform(), i % 2 ? grey0 : grey1, inset, 0);
        }
        Ra::Path ellipsePath; ellipsePath->addEllipse(outer);
        background.addPath(ellipsePath, Ra::Transform(), black, strokeWidth, 0);
        list.addScene(background);
        for (int i = 1; i < 8; i++) {
            Ra::Scene ring;
            Ra::Bounds inner = outer.inset(inset * i, inset * i);
            float step = 2.f * M_PI / divisions[i], theta0 = 0.5f * M_PI;
            float r0 = 0.5f * (inner.ux - inner.lx), r1 = r0 + (divisions[i] == 60 ? 0.25f : 0.5f) * inset;
            Ra::Path ellipsePath; ellipsePath->addEllipse(inner);
            ring.addPath(ellipsePath, Ra::Transform(), black, strokeWidth, 0);
            
            Ra::Path path;
            for (int j = 0; j < divisions[i]; j++) {
                float theta = theta0 + j * step;
                path->moveTo(cx + r0 * cosf(theta), cy + r0 * sinf(theta));
                path->lineTo(cx + r1 * cosf(theta), cy + r1 * sinf(theta));
            }
            ring.addPath(path, Ra::Transform(), black, strokeWidth, 0);
            
            if (!font.isEmpty()) {
                float r = r0 + 0.25f * inset, da, a0;
                Ra::Row<char> str;
                for (int j = 0; j < divisions[i]; j++) {
                    if (labels[i])
                        str = str.empty() + labels[i][j];
                    else
                        str = str.empty() + j;
                    Ra::Scene glyphs;  Ra::Bounds gb = RasterizerFont::writeGlyphs(font, inset * 0.666f, black, b, false, false, false, str.base, glyphs);
                    da = (gb.ux - gb.lx) / r, a0 = theta0 + j * -step - 0.5f * (step - da);
                    
                    if (0) {
                        Ra::Path arcPath;  arcPath->addArc(cx, cy, r0 + 0.5f * (strokeWidth + arcWidth), a0 - da, a0);
                        ring.addPath(arcPath, Ra::Transform(), red, arcWidth, 0);
                    }
                    RasterizerFont::writeGlyphsOnArc(glyphs, cx, cy, r, a0, ring);
                }
            }
            list.addScene(ring);
        }
        Ra::Scene line;
        Ra::Path linePath;  linePath->moveTo(cx, outer.uy - inset * 7.f), linePath->lineTo(cx, outer.uy);
        line.addPath(linePath, Ra::Transform(), red, 1.f, 0);
        list.addScene(line);
    }
    static Ra::Scene createGridScene(size_t count, float size, float width, bool outline, Ra::Colorant color) {
        Ra::Scene scene;
        size_t dim = ceilf(sqrtf(count)), i;
        for (i = 0; i < count; i++) {
            float lx = size * (i % dim), ly = size * (i / dim);
            Ra::Path path;
            if (outline)
                path.ref->moveTo(lx, ly + 0.5f * width), path.ref->lineTo(lx + width, ly + 0.5f * width);
            else
                path.ref->addBounds(Ra::Bounds(lx, ly, lx + width, ly + width));
            scene.addPath(path, Ra::Transform(), color, width, 0);
        }
        return scene;
    }
    static Ra::Path createPhyllotaxisPath(size_t count) {
        Ra::Path path;
        path.ref->moveTo(0.f, 0.f);
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            path.ref->lineTo(s * vx - 0.5f, s * vy - 0.5f);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
        return path;
    }
    static void writePhyllotaxisToScene(size_t count, Ra::Scene& scene) {
        Ra::Colorant color(0, 0, 0, 255);
        Ra::Path line;
        line.ref->moveTo(0, 0), line.ref->lineTo(0, 1);
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            scene.addPath(line, Ra::Transform(1.f + t, 0.f, 0.f, 1.f + t, s * vx - 0.5f, s * vy - 0.5f), color, 1.f, 0);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
    }
    
    bool readEvents(RasterizerState& state) {
        const float kScaleMin = 0.f, kScaleMax = 1.2, kTxMin = 0.f, kTxMax = 100.f;
        double time = 0.5 * state.time, ftime = time - floor(time);
        float t = sinf(M_PI * float(ftime)), s = 1.f - t;
        float scale = s * kScaleMin + t * kScaleMax;
        float jt, tx, ty, cx, cy;
        if (src.pathsCount)
            for (Ra::Scene *ss = & src.scenes[0], *ds = & dst.scenes[0], *end = ss + src.scenes.size(); ss < end; ss++, ds++) {
                int offset = ss->count * t;
                for (int j = 0; j < ss->count; j++) {
                    jt = float(j) / float(ss->count);
                    if (0) {
                        tx = s * kTxMin + t * kTxMax, ty = tx;
                        Ra::Transform rst = Ra::Transform::rst(kTau * ((j & 1 ? s : t) + jt), scale, scale);
                        Ra::Transform m = Ra::Transform(1.f, 0.f, 0.f, 1.f, tx, ty).concat(ss->ctms[j]);
                        Ra::Bounds b = Ra::Bounds(ss->paths[j]->bounds.unit(m));
                        cx = 0.5f * (b.lx + b.ux), cy = 0.5f * (b.ly + b.uy);
                        ds->ctms[j] = m.concat(rst, cx, cy);
                    }
                    if (0) {
                        ds->widths[j] = scale * ss->widths[j];
                    }
                    if (0) {
                        ds->colors[j] = ss->colors[(j + offset) % ss->count];
                    }
                    if (0) {
                        ds->flags[j] = (ss->flags[j] & ~Ra::Scene::kInvisible) | (j == offset ? 0 : Ra::Scene::kInvisible);
                    }
                }
            }
        return concentrichron.pathsCount || src.pathsCount;
    }
    void writeList(Ra::SceneList& list) {
        if (concentrichron.pathsCount)
            writeConcentrichronList(concentrichron, bounds, list);
        else if(src.pathsCount)
            list.addList(dst);
    }
    size_t refCount = 0;
    Ra::Bounds bounds;
    Ra::SceneList concentrichron, src, dst;
};
