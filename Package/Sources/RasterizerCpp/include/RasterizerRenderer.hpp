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

struct RasterizerRenderer {
    
    void renderList(const Ra::SceneList& list, float scale, float w, float h, Ra::Buffer *buffer, CGColorSpaceRef destSpace) {
//        matchColors(list, destSpace);
        
        Ra::Bounds device(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
        Ra::Transform view = list.ctm.concat(Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f));
        
        buffer->params = list.params;
        buffer->prepare(list);
         
        size_t divisions[kContextCount + 1], *pdivs = divisions;
        writeBalancedWeightDivisions(list, pdivs);
        dispatch_apply(kContextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            contexts[i].drawList(list, device, view, pdivs[i], pdivs[i + 1], buffer);
        });
        size_t begins[kContextCount], *pbegins = begins, size;
        size = Ra::resizeBuffer(list, contexts, kContextCount, pbegins, *buffer);
        dispatch_apply(kContextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            Ra::writeContextToBuffer(list, contexts + i, pbegins[i], *buffer);
        });
        for (int i = 0; i < kContextCount; i++)
            for (int j = 0; j < contexts[i].entries.size(); j++)
                *(buffer->entries.alloc(1)) = contexts[i].entries[j];
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.back().end;
        assert(size >= end);
        
        converter.matchColors((Ra::Colorant *)(buffer->base + buffer->colors), buffer->pathsCount, destSpace);
    }
    
    void matchColors(const Ra::SceneList& list, CGColorSpaceRef destSpace) {
        for (size_t i = 0; i < list._scenes.size(); i++) {
            auto scene = list._scenes[i].ptr;
            if (scene->srcColors.memory->addr == scene->colors.memory->addr) {
                size_t size = scene->srcColors.size();
                scene->colors = Ra::Vector<Ra::Colorant>();
                Ra::Colorant *color = scene->colors.resize(size);
                memcpy(color, & scene->srcColors[0], size * sizeof(Ra::Colorant));
            }
        }
    }
    
    void writeBalancedWeightDivisions(const Ra::SceneList& list, size_t *divisions) {
        size_t total = 0, count, si, i, iz, target;
        for (int j = 0; j < list._scenes.size(); j++)
            total += list._scenes[j]->weight;
        if (total == 0)
            memset(divisions, 0, (kContextCount + 1) * sizeof(*divisions));
        else {
            divisions[0] = 0, divisions[kContextCount] = list.pathsCount;
            auto scene = & list._scenes[0];
            for (count = si = iz = 0, i = 1; i < kContextCount; i++) {
                for (target = total * i / kContextCount; count < target; iz++, si++) {
                    if (si == (scene->ptr)->count)
                        scene++, si = 0;
                    count += (scene->ptr)->paths[si]->types.end;
                }
                divisions[i] = iz;
            }
        }
    }
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    
    RaCG::Converter converter;
    
    static const int kContextCount = 8;
    Ra::Context contexts[kContextCount];
 };


 typedef RasterizerRenderer RaR;
