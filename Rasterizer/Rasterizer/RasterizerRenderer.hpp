//
//  RasterizerRenderer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 10/05/2020.
//  Copyright © 2020 @mindbrix. All rights reserved.
//

#import "RasterizerQueue.hpp"
#import "RasterizerState.hpp"

struct RasterizerRenderer {
     struct ThreadInfo {
         Ra::Context *context;
         Ra::SceneList *list;  RasterizerState *state;
         uint32_t *idxs;  Ra::Transform *ctms;  Ra::Colorant *colors;  Ra::Transform *clips;  float *widths;  Ra::Bounds *bounds;
         size_t begin;  std::vector<Ra::Buffer::Entry> *entries;
         Ra::Buffer *buffer;
    };
    static void drawList(void *info) {
        ThreadInfo *ti = (ThreadInfo *)info;
        ti->context->drawList(*ti->list, ti->state->view, ti->idxs, ti->ctms, ti->colors, ti->clips, ti->widths, ti->bounds, ti->state->outlineWidth, ti->buffer);
    }
    static void writeContextsToBuffer(void *info) {
        ThreadInfo *ti = (ThreadInfo *)info;
        Ra::writeContextToBuffer(*ti->list, ti->context, ti->idxs, ti->begin, *ti->entries, *ti->buffer);
    }
    void renderList(Ra::SceneList& list, RasterizerState& state, Ra::Buffer *buffer) {
        if (list.pathsCount == 0)
            return;
        uint32_t *idxs = (uint32_t *)malloc(list.pathsCount * sizeof(uint32_t));
        assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
        buffer->prepare(list.pathsCount);
        Ra::Colorant *colors = (Ra::Colorant *)(buffer->base + buffer->colors);
        Ra::Transform *ctms = (Ra::Transform *)(buffer->base + buffer->transforms);
        Ra::Transform *clips = (Ra::Transform *)(buffer->base + buffer->clips);
        float *widths = (float *)(buffer->base + buffer->widths);
        Ra::Bounds *bounds = (Ra::Bounds *)(buffer->base + buffer->bounds);
              
        if (state.outlineWidth) {
            Ra::Colorant black(0, 0, 0, 255);
            memset_pattern4(colors, & black, list.pathsCount * sizeof(Ra::Colorant));
        } else {
            Ra::Scene *scene = & list.scenes[0];
            for (size_t i = 0, iz = 0; i < list.scenes.size(); i++, iz += scene->count, scene++)
                memcpy(colors + iz, & scene->colors[0].src0, scene->count * sizeof(Ra::Colorant));
        }
        if (state.indices.begin != INT_MAX) {
            size_t index = list.index(state.indices.begin, state.indices.end);
            colors[index].src0 = 0, colors[index].src1 = 0, colors[index].src2 = 255, colors[index].src3 = 255;
        }
        renderListOnQueues(list, state, idxs, ctms, colors, clips, widths, bounds, buffer, true);
        free(idxs);
    }
    
    void renderListOnQueues(Ra::SceneList& list, RasterizerState& state, uint32_t *idxs, Ra::Transform *ctms, Ra::Colorant *colors, Ra::Transform *clips, float *widths, Ra::Bounds *bounds, Ra::Buffer *buffer, bool multithread) {
        size_t eiz = 0, total = 0, count, divisions = kQueueCount, base, i, iz, izeds[divisions + 1], target, *izs = izeds;
        for (int j = 0; j < list.scenes.size(); j++)
            eiz += list.scenes[j].count, total += list.scenes[j].weight;
        if (buffer)
            buffer->useCurves = state.useCurves;
        ThreadInfo threadInfo[kQueueCount], *ti = threadInfo;
        if (multithread) {
            ti->context = contexts, ti->list = & list, ti->state = & state, ti->idxs = idxs, ti->ctms = ctms, ti->clips = clips, ti->colors = colors, ti->widths = widths, ti->bounds = bounds,  ti->buffer = buffer;
            for (i = 1; i < kQueueCount; i++)
                threadInfo[i] = threadInfo[0], threadInfo[i].context += i;
            izeds[0] = 0, izeds[divisions] = eiz;
            auto scene = & list.scenes[0];
            for (count = base = iz = 0, i = 1; i < divisions; i++) {
                for (target = total * i / divisions; count < target; iz++) {
                    if (iz - base == scene->count)
                        scene++, base = iz;
                    count += scene->paths[iz - base].ref->typesSize;
                }
                izeds[i] = iz;
            }
            count = kQueueCount;
            for (i = 0; i < count; i++)
                contexts[i].prepare(state.device.ux, state.device.uy, list.pathsCount, izs[i], izs[i + 1]);
            RasterizerQueue::scheduleAndWait(queues, kQueueCount, drawList, threadInfo, sizeof(ThreadInfo), count);
        } else {
            count = 1;
            contexts[0].prepare(state.device.ux, state.device.uy, list.pathsCount, 0, eiz);
            contexts[0].drawList(list, state.view, idxs, ctms, colors, clips, widths, bounds, state.outlineWidth, buffer);
        }
        std::vector<Ra::Buffer::Entry> entries[count];
        size_t begins[count], size = Ra::writeContextsToBuffer(list, contexts, count, begins, *buffer);
        if (count == 1)
            Ra::writeContextToBuffer(list, contexts, idxs, begins[0], entries[0], *buffer);
        else {
            for (i = 0; i < count; i++)
                threadInfo[i].begin = begins[i], threadInfo[i].entries = & entries[i];
            RasterizerQueue::scheduleAndWait(queues, kQueueCount, writeContextsToBuffer, threadInfo, sizeof(ThreadInfo), count);
        }
        for (int i = 0; i < count; i++)
            for (auto entry : entries[i])
                *(buffer->entries.alloc(1)) = entry;
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.base[buffer->entries.end - 1].end;
        assert(size >= end);
    }
    
    static const int kQueueCount = 8;
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    Ra::Context contexts[kQueueCount];
    RasterizerQueue queues[kQueueCount];
 };

 typedef RasterizerRenderer RaR;
