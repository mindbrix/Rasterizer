//
//  RasterizerRenderer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 10/05/2020.
//  Copyright © 2020 @mindbrix. All rights reserved.
//


struct RasterizerRenderer {
     void renderList(Ra::SceneList& list, Ra::Bounds device, Ra::Bounds deviceClip, Ra::Transform view, Ra::Buffer *buffer) {
        assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
        if (list.pathsCount == 0)
            return;
         buffer->prepare(list);
         buffer->device = device;
         buffer->clip = deviceClip;
         
        size_t divisions[kQueueCount + 1], *pdivs = divisions;
        writeDivisions(list, divisions);
        dispatch_apply(kQueueCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            contexts[i].drawList(list, device, deviceClip, view, pdivs[i], pdivs[i + 1], buffer);
        });
        size_t begins[kQueueCount], *pbegins = begins, size;
        size = Ra::writeContextsToBuffer(list, contexts, kQueueCount, begins, *buffer);
        dispatch_apply(kQueueCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            Ra::writeContextToBuffer(list, contexts + i, pbegins[i], *buffer);
        });
        for (int i = 0; i < kQueueCount; i++)
            for (auto entry : contexts[i].entries)
                *(buffer->entries.alloc(1)) = entry;
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.back().end;
        assert(size >= end);
    }
    
    void writeDivisions(Ra::SceneList& list, size_t *divisions) {
        size_t total = 0, count, base, i, iz, target;
        for (int j = 0; j < list.scenes.size(); j++)
            total += list.scenes[j].weight;
        divisions[0] = 0, divisions[kQueueCount] = list.pathsCount;
        auto scene = & list.scenes[0];
        for (count = base = iz = 0, i = 1; i < kQueueCount; i++) {
            for (target = total * i / kQueueCount; count < target; iz++) {
                if (iz - base == scene->count)
                    scene++, base = iz;
                count += scene->paths->base[iz - base]->types.end;
            }
            divisions[i] = iz;
        }
    }
    static const int kQueueCount = 8;
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    Ra::Context contexts[kQueueCount];
 };


 typedef RasterizerRenderer RaR;
