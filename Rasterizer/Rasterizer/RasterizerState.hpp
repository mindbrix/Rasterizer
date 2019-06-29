//
//  RasterizerState.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 20/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "RasterizerWinding.hpp"

struct RasterizerState {
    struct Event {
        enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
        enum Type { kNull = 0, kMouseMove, kMouseUp, kMouseDown, kFlags, kKeyDown, kKeyUp, kMagnify, kRotate, kTranslate };
        
        Event() {}
        Event(double time, Type type, float x, float y) : time(time), type(type), x(x), y(y), keyCode(0) {}
        Event(double time, Type type, int keyCode) : time(time), type(type), x(0.f), y(0.f), keyCode(keyCode) {}
        Event(double time, Type type, size_t flags) : time(time), type(type), x(0.f), y(0.f), keyCode(0), flags(flags) {}
        
        double time;
        Type type;
        float x, y;
        int keyCode;
        size_t flags;
    };
    void update(float s, float w, float h) {
        scale = s, view = Rasterizer::Transform(s, 0.f, 0.f, s, 0.f, 0.f).concat(ctm);
        bounds = Rasterizer::Bounds(0.f, 0.f, w, h), device = Rasterizer::Bounds(0.f, 0.f, ceilf(s * w), ceilf(h * s));
    }
    bool writeEvent(Event e) {
        bool written = e.type != Event::kKeyDown || (e.type == Event::kKeyDown && (e.keyCode == 8 || e.keyCode == 31 || e.keyCode == 35 || e.keyCode == 36));
        if (written)
            events.emplace_back(e);
        return written;
    }
    bool readEvents(float s, float w, float h, double time, Rasterizer::SceneList& list) {
        bool redraw = false;
        float sine, cosine;
        update(s, w, h);
        for (Event& e : events) {
            switch(e.type) {
                case Event::kMouseMove:
                    x = e.x, y = e.y;
                    redraw = mouseMove;
                    break;
                case Event::kMouseUp:
                    mouseDown = false;
                    break;
                case Event::kMouseDown:
                    mouseDown = true;
                    break;
                case Event::kFlags:
                    flags = e.flags;
                    break;
                case Event::kKeyDown:
                    keyDown = true, keyCode = e.keyCode;
                    redraw = true;
                    if (e.keyCode == 8)
                        useClip = !useClip;
                    else if (e.keyCode == 31)
                        useOutline = !useOutline;
                    else if (e.keyCode == 35)
                        mouseMove = !mouseMove;
                    else if (e.keyCode == 36) {
//                        if (user.lx == FLT_MAX)
                            ctm = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
//                        else
//                            ctm = { (bounds.ux - bounds.lx) / (user.ux - user.lx), 0.f, 0.f, (bounds.uy - bounds.ly) / (user.uy - user.ly), 0.f, 0.f };
                    }
                    break;
                case Event::kKeyUp:
                    keyDown = true, keyCode = e.keyCode;
                    break;
                case Event::kMagnify:
                    ctm = ctm.concat(Rasterizer::Transform(e.x, 0.f, 0.f, e.x, 0.f, 0.f), 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
                    redraw = true;
                    break;
                case Event::kRotate:
                    __sincosf(e.x, & sine, & cosine);
                    ctm = ctm.concat(Rasterizer::Transform(cosine, sine, -sine, cosine, 0.f, 0.f), 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
                    redraw = true;
                    break;
                case Event::kTranslate:
                    ctm.tx += e.x, ctm.ty += e.y;
                    redraw = true;
                    break;
                case Event::kNull:
                    assert(0);
            }
        }
        view = Rasterizer::Transform(s, 0.f, 0.f, s, 0.f, 0.f).concat(ctm);
        index = INT_MAX;
        if (mouseMove) {
            Rasterizer::SceneList visibles;
            size_t pathsCount = list.writeVisibles(view, device, visibles);
            if (pathsCount) {
                Rasterizer::Range indices = RasterizerWinding::indicesForPoint(visibles, false, view, device, scale * x, scale * y);
                if (indices.begin != INT_MAX) {
                    int idx = 0;
                    for (int j = 0; j < indices.begin; j++)
                        idx += visibles.scenes[j].ref->paths.size();
                        index = idx + indices.end;
                }
            }
        }
        events.resize(0);
        return redraw;
    }
    bool keyDown = false, mouseDown = false, mouseMove = false, useClip = false, useOutline = false;
    float x, y, scale;
    int keyCode = 0;
    size_t index = INT_MAX, flags = 0;
    std::vector<Event> events;
    Rasterizer::Transform ctm = { 1.f, 0.f, 0.f, 1.0, 0.f, 0.f }, view;
    Rasterizer::Bounds bounds, device, user = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
};
