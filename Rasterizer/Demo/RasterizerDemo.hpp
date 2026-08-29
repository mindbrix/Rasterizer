//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "Rasterizer.hpp"
#import "RasterizerPDF.hpp"
#import "RasterizerSVG.hpp"
#import "Concentrichron.hpp"
#import "RasterizerWinding.hpp"
#import "RasterizerCoreText.hpp"


struct RasterizerDemo {
    constexpr static float kHudWidth = 240, kHudHeight = 240, kHudInset = 20, kHudBorder = 0.5;
    constexpr static size_t kHudItemCount = 11;
    
    enum KeyCode { kA = 0, kB = 11, kC = 8, kF = 3, kG = 5, kH = 4, kI = 34, kL = 37, kO = 31, kP = 35, kS = 1, kT = 17, k1 = 18, k0 = 29, kMinus = 27, kPlus = 24 };
    enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
    
    struct HudItem {
        HudItem(char const *key, char const *text, char const *alt = nullptr) : key(key), text(text), alt(alt) {}
        char const *key, *text, *alt;
    };
    
    HudItem hudItems[kHudItemCount] = {
        HudItem("0", "Rasterizer", "Core Graphics"),
        HudItem("B", "Clips"),
        HudItem("C", "Curves"),
        HudItem("F", "Fit bounds"),
        HudItem("G", "Glyph grid"),
        HudItem("H", "HUD"),
        HudItem("I", "Opaques"),
        HudItem("O", "Outlines"),
        HudItem("P", "Path mouseover"),
        HudItem("S", "PDF screenshot"),
        HudItem("T", "Time"),
    };
    
#pragma mark - Event handlers

    void onPaste(const char *string, Ra::Bounds bounds) {
        this->bounds = bounds;
        
        showGlyphGrid = false;
        showTime = false;
        setPastedString(string);
    }
    void onFlags(size_t keyFlags) {
        flags = keyFlags;
        redraw = true;
    }
    bool onKeyDown(unsigned short keyCode, size_t keyFlags) {
        if (keyFlags)
            return false;
        
        bool keyUsed = false;
        if (keyCode == KeyCode::kB)
            params.useClips = !params.useClips, keyUsed = true, clearHUD();
        else if (keyCode == KeyCode::kC)
            params.useCurves = !params.useCurves, keyUsed = true, clearHUD();
        else if (keyCode == KeyCode::kF) {
            Ra::Transform fit = bounds.fitTransform(list.bounds());
            ctm = memcmp(& ctm, & fit, sizeof(ctm)) == 0 ? Ra::Transform() : fit;
            keyUsed = true;
        } else if (keyCode == KeyCode::kH)
            showHud = !showHud, keyUsed = true;
        else if (keyCode == KeyCode::kI)
            params.showOpaques = !params.showOpaques, keyUsed = true, clearHUD();
        else if (keyCode == KeyCode::kO)
            params.showOutlines = !params.showOutlines, keyUsed = true, clearHUD();
        else if (keyCode == KeyCode::kP)
            mouseMove = !mouseMove, indices = mouseMove ? indices : RaWnd::IndexPair(), keyUsed = true, clearHUD();
        else if (keyCode == KeyCode::kL)
            locked = locked.i0 != INT_MAX ? RaWnd::IndexPair() : indices, keyUsed = true;
        else if (keyCode == KeyCode::kS)
            list.ctm = ctm, RaCG::screenGrabToPDF(list, bounds), keyUsed = true;
        else if (keyCode == KeyCode::kT) {
            showGlyphGrid = false;
            showTime = !showTime;
            setPastedString(nullptr);
            keyUsed = true, clearHUD();
        } else if (keyCode == KeyCode::kG) {
            showTime = false;
            showGlyphGrid = !showGlyphGrid;
            setPastedString(nullptr);
            keyUsed = true, clearHUD();
        } else if (keyCode == KeyCode::kMinus) {
            if (pageIndex > 0) {
                pageIndex--;
                document = Ra::SceneList();
                keyUsed = true;
            }
            
        } else if (keyCode == KeyCode::kPlus) {
            if (pageIndex < pageCount - 1) {
                pageIndex++;
                document = Ra::SceneList();
                keyUsed = true;
            }
        }
        redraw = keyUsed;
        
        return keyUsed;
    }
    void onKeyUp(unsigned short keyCode) {
    }
    void onMouseMove(float x, float y) {
        mx = x, my = y;
        if (mouseMove)
            redraw = true;
    }
    void onMouseDown(float x, float y) {
        mouseDown = true;
    }
    void onMouseUp(float x, float y) {
        mouseDown = false;
    }
    void onMagnify(float s) {
        float cx = (flags & Flags::kShift) ? mx : bounds.cx();
        float cy = (flags & Flags::kShift) ? my : bounds.cy();
        ctm = ctm.concatAroundCenter(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f), cx, cy);
        redraw = true;
    }
    void onRotate(float a) {
        float cx = (flags & Flags::kShift) ? mx : bounds.cx();
        float cy = (flags & Flags::kShift) ? my : bounds.cy();
        float sine, cosine;  __sincosf(a, & sine, & cosine);
        ctm = ctm.concatAroundCenter(Ra::Transform(cosine, sine, - sine, cosine, 0, 0), cx, cy);
        redraw = true;
    }
    void onDrag(float dx, float dy) {
        ctm.tx += dx, ctm.ty += dy;
        mx += dx, my += dy;
        redraw = true;
    }
    void onTranslate(float dx, float dy) {
        ctm.tx += dx, ctm.ty += dy;
        redraw = true;
    }
    
   
#pragma mark - Properties
    
    void clearHUD() {
        hud = Ra::SceneRef();
    }
    Ra::SceneRef getHUD(Ra::Bounds hudBounds) {
        Ra::SceneRef hud;

        float padding = 0.666 * hudBounds.height() / (kHudItemCount + 2);
        Ra::Bounds text = hudBounds.inset(padding, 0.666 * padding);
        
        Ra::Path bgPath;  bgPath->addBounds(hudBounds.inset(0.5 * kHudBorder, 0.5 * kHudBorder)), bgPath->close();
        hud->addPath(bgPath, Ra::Transform(), bgColor, 0, 0);
        
        float lineHeight = text.height() / kHudItemCount, uy;
        float fontSize = lineHeight * lineHeight / RaCT::lineHeightFor(fontName.addr, lineHeight);
        
        for (size_t i = 0; i < kHudItemCount; i++) {
            HudItem& item = hudItems[i];
            uy = text.uy - i * lineHeight;
            Ra::Color color = textColor;
            if (  (*item.key == '0')
                || (*item.key == 'B' && params.useClips)
                || (*item.key == 'G' && showGlyphGrid)
                || (*item.key == 'I' && params.showOpaques)
                || (*item.key == 'O' && params.showOutlines)
                || (*item.key == 'P' && mouseMove)
                || (*item.key == 'T' && showTime)
                || (*item.key == 'C' && params.useCurves))
                color = activeColor;
            RaCT::addCStringToSceneInRect(item.key, fontName.addr, fontSize, textColor, Ra::Bounds(text.lx, hudBounds.ly, text.ux, uy), Ra::Transform(), Ra::Bounds(), hud);
            char const *label = item.text;
            if (*item.key == '0' && !gpu)
                label = item.alt;
            RaCT::addCStringToSceneInRect(label, fontName.addr, fontSize, color, Ra::Bounds(text.lx + 2 * fontSize, hudBounds.ly, text.ux, uy), Ra::Transform(), Ra::Bounds(), hud);
        }
        hud->addPath(bgPath, Ra::Transform(), textColor, kHudBorder, 0);
        return hud;
    }
    Ra::SceneList getDrawList(double time, float w, float h) {
        bounds = Ra::Bounds(0.f, 0.f, w, h);
        redraw = false;
        clock = time * timeScale;
        list = Ra::SceneList();
        if (pastedString.size) {
            if (pasted.pathsCount == 0) {
                Ra::SceneRef glyphs;
                RaCT::addCStringToSceneInRect(pastedString.addr, fontName.addr, fontSize, textColor, bounds, Ra::Transform(), Ra::Bounds(), glyphs);
                pasted.addScene(glyphs);
            }
            list.addList(pasted);
        } else if (showGlyphGrid) {
            if (text.pathsCount == 0) {
                text.addScene(RaCT::writeGlyphGrid(fontName.addr, fontSize, textColor));
            }
            list.addList(text);
        } else if (showTime) {
            list.addList(concentrichron.writeList(fontName.addr));
        } else if (svgUrl.size) {
            if (document.pathsCount == 0) {
                Ra::SceneRef scene;
                Ra::Transform m = RaSVG::addSvgToScene(svgUrl.addr, scene);
                document.addScene(scene, m);
                fit = true;
            }
            list.addList(document);
        } else if (pdfUrl.size) {
            if (document.pathsCount == 0) {
                Ra::SceneRef scene;
                Ra::Transform m = RaPDF::addPdfPageToScene(pdfUrl.addr, pageIndex, scene);
                document.addScene(scene, m);
                fit = true;
            }
            list.addList(document);
        }
        if (fit)
            ctm = bounds.fitTransform(list.bounds()), fit = false;
        Ra::SceneList draw = list;
        draw.ctm = ctm, draw.params = params;
        if (mouseMove) {
            indices = RasterizerWinding::indicesForPoint(draw, bounds, mx, my);
            if (indices.i0 != INT_MAX) {
                size_t i0 = indices.i0, i1 = indices.i1;
                const Ra::SceneRef& scene = list.scenes[i0];
                
                Ra::Draw& _draw = scene->draws[i1];
                Ra::Color red(0, 0, 255, 64);
                if (mouseDown) {
                    _draw.width = -8;
                    _draw.paint = Ra::Paint(0, 0, 255, 64);
                    scene->needPrepare = true;
                } else {
                    Ra::SceneRef mouseScene;
                    mouseScene->addPath(_draw.path, _draw.ctm, red, _draw.width, _draw.flags, & _draw.clip, & _draw.clipPath);
                    draw.addScene(mouseScene, list.ctms[i0], list.clips[i0]);
                }
            }
        }
        if (showHud) {
            Ra::Bounds hudBounds = Ra::Bounds(0, 0, kHudWidth, kHudHeight);
            if (hud->count() == 0)
                hud = getHUD(hudBounds);
            Ra::Transform m = Ra::Transform(1, 0, 0, 1, kHudInset, bounds.uy - kHudInset - kHudHeight).concat(ctm.invert());
            draw.addScene(hud, m, hudBounds);
        }
        return draw;
    }
    bool getShouldRedraw() const {
        return redraw || showTime;
    }
    void setFont(const char *url, const char *name, float size) {
        fontSize = size;
        if (name)
            strcpy(fontName.resize(strlen(name) + 1), name);
        concentrichron.resetFace();
        pasted = Ra::SceneList();
        text = Ra::SceneList();
        clearHUD();
        redraw = true;
    }
    void setPastedString(const char *string) {
        if (string)
            strcpy((char *)pastedString.resize(strlen(string) + 1), string);
        else
            pastedString = Ra::Memory<char>();
        pasted = Ra::SceneList();
        redraw = true;
    }
    void setPdfUrl(const char *url) {
        if (url)
            strcpy((char *)pdfUrl.resize(strlen(url) + 1), url);
        else
            pdfUrl = Ra::Memory<char>();
        pageCount = url ? RaPDF::getPageCount(url) : 0;
        redraw = true;
    }
    void setSvgUrl(const char *url) {
        if (url)
            strcpy((char *)svgUrl.resize(strlen(url) + 1), url);
        else
            svgUrl = Ra::Memory<char>();
        redraw = true;
    }
    void setUseGPU(bool useGPU) {
        gpu = useGPU;
        redraw = true, clearHUD();
    }
    
    Ra::Color textColor = Ra::Color(0, 0, 0, 255), activeColor = Ra::Color(0, 0, 255, 255), bgColor = Ra::Color(255, 255, 255, 192);
    float fontSize = 14;
    Concentrichron concentrichron;
    Ra::SceneList list, document, pasted, text;
    Ra::SceneRef hud;
    Ra::Memory<char> pastedString, fontName, pdfUrl, svgUrl;
    bool showGlyphGrid = false, showTime = false, showHud = true;
    size_t pageCount, pageIndex;
    
    Ra::Transform ctm;
    Ra::Bounds bounds;

    Ra::Params params;
    bool gpu = true, redraw = false, fit = false, mouseDown = false, mouseMove = false;
    double clock = 0.0, timeScale = 0.333;
    float mx, my;
    RaWnd::IndexPair indices = RaWnd::IndexPair(), locked = RaWnd::IndexPair();
    size_t flags = 0;
};
