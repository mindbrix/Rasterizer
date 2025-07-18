//
//  Concentrichron.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import <time.h>

struct Concentrichron {
    Ra::SceneList face;
    
    void resetFace() {
        face.empty();
    }
    
    Ra::SceneList writeList(RasterizerFont& font) {
        Ra::Bounds bounds(0, 0, 800, 600);
        if (face.scenes.size() == 0)
            face = makeFace(bounds, font);
        return setTime(face, bounds);
    }
    
    static Ra::SceneList setTime(Ra::SceneList& face, Ra::Bounds b) {
        Ra::SceneList list;
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
        float ftimes[8] = { 0, fyear, fmonth, fdate, fday, fhour, fmin, fsec }, cosine, sine;
        
        for (int i = 0; i < face.scenes.size(); i++)
            if (i > 0 && i < 8) {
                __sincosf(ftimes[i] * 2.f * M_PI, & sine, & cosine);
                list.addScene(face.scenes[i], Ra::Transform().preconcat(Ra::Transform(cosine, sine, -sine, cosine, 0, 0), b.cx(), b.cy()));
            } else
                list.addScene(face.scenes[i]);
        return list;
    }
    static float normalizeRadians(float a) { return fmodf(a >= 0.f ? a : (kTau - (fmodf(-a, kTau))), kTau); }
    
    static void addArcToPath(Ra::Path& p, float x, float y, float r, float a0, float a1) {
        float da, f, ax, ay, bx, by, sx, sy, ex, ey;
        a0 = normalizeRadians(a0), a1 = normalizeRadians(a1);
        da = a1 > a0 ? a1 - a0 : a1 - a0 + kTau, f = 4.f * tanf(da * 0.25f) / 3.f;
        ax = r * cosf(a0), ay = r * sinf(a0), bx = r * cosf(a1), by = r * sinf(a1);
        sx = x + ax, sy = y + ay, ex = x + bx, ey = y + by;
        p->moveTo(sx, sx), p->cubicTo(sx - f * ay, sy + f * ax, ex + f * by, ey - f * bx, ex, ey);
    }
    
    static Ra::SceneList makeFace(Ra::Bounds b, RasterizerFont& font) {
        Ra::SceneList list;
        const char *days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
        const char *dates[31] = { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "11th", "12th", "13th", "14th", "15th", "16th", "17th", "18th", "19th", "20th", "21st", "22nd", "23rd", "24th", "25th", "26th", "27th", "28th", "29th", "30th", "31st" };
        const char *months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        const char *years[10] = { "2020", "2021", "2022", "2023", "2024", "2025", "2026", "2027", "2028", "2029" };
        const char **labels[8] = { NULL, years, months, dates, days, NULL, NULL, NULL };
        const int divisions[8] = { 0, 10, 12, 31, 7, 24, 60, 60 };
        const Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255), grey0(245, 245, 245, 255), grey1(250, 250, 250, 255);
        const float strokeWidth = 0.5f;
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
                char strbuf[32];
                for (int j = 0; j < divisions[i]; j++) {
                    if (labels[i]) {
                        str.empty();
                        strcpy(str.alloc(strlen(labels[i][j]) + 1), labels[i][j]);
                    } else {
                        str.empty();
                        bzero(strbuf, sizeof(strbuf)), snprintf(strbuf, 32, "%d", j);
                        strcpy(str.alloc(strlen(strbuf) + 1), strbuf);
                    }
                    Ra::Scene glyphs;  Ra::Bounds gb = font.layoutGlyphs(inset * 0.666f, 0.f, black, b, false, false, false, str.base, glyphs);
                    da = (gb.ux - gb.lx) / r, a0 = theta0 + j * -step - 0.5f * (step - da);
                    
                    RasterizerFont::layoutGlyphsOnArc(glyphs, cx, cy, r, a0, ring);
                }
            }
            list.addScene(ring);
        }
        Ra::Scene line;
        Ra::Path linePath;  linePath->moveTo(cx, outer.uy - inset * 7.f), linePath->lineTo(cx, outer.uy);
        line.addPath(linePath, Ra::Transform(), red, 1.f, 0);
        list.addScene(line);
        return list;
    }
};
