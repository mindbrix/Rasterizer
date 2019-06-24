//
//  RasterizerQueue.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/06/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <vector>
#import <pthread.h>

struct RasterizerQueue {
    typedef void (*Function)(void *info);
    static void scheduleAndWait(RasterizerQueue *queues, size_t count, Function function, void *arguments, size_t stride) {
        if (queues && count && function && arguments && stride) {
            uint8_t *base = (uint8_t *)arguments;
            for (int i = 0; i < count; i++, base += stride)
                queues[i].add(function, (void *)base);
            for (int i = 0; i < count; i++)
                queues[i].wait();
        }
    }
    struct Arguments {
        Arguments() {}
        Arguments(Function function, void *info) : function(function), info(info) {}
        Function function;
        void *info;
    };
    static void *queue_main(void *arguments) {
        RasterizerQueue *queue = (RasterizerQueue *)arguments;
        while (1)
            queue->cycle();
        return 0;
    }
    RasterizerQueue() {
        pthread_mutex_init(& mtx, NULL);
        pthread_cond_init(& added, NULL);
        pthread_cond_init(& empty, NULL);
        pthread_create(& thread, NULL, queue_main, (void *)this);
    }
    ~RasterizerQueue() {
        pthread_mutex_lock(& mtx);
        pthread_cancel(thread);
        pthread_mutex_destroy(& mtx);
        pthread_cond_destroy(& added);
        pthread_cond_destroy(& empty);
    }
    void add(Function function, void *info) {
        pthread_mutex_lock(& mtx);
        arguments.emplace_back(function, info);
        pthread_cond_signal(& added);
        pthread_mutex_unlock(& mtx);
    }
    void cycle(void) {
        pthread_mutex_lock(& mtx);
        while (arguments.size() == 0)
            pthread_cond_wait(& added, & mtx);
        Arguments args = arguments[0];
        pthread_mutex_unlock(& mtx);
        
        (*args.function)(args.info);
        
        pthread_mutex_lock(& mtx);
        arguments.erase(arguments.begin());
        if (arguments.size() == 0)
            pthread_cond_signal(& empty);
        pthread_mutex_unlock(& mtx);
    }
    void wait() {
        pthread_mutex_lock(& mtx);
        while (arguments.size())
            pthread_cond_wait(& empty, & mtx);
        pthread_mutex_unlock(& mtx);
    }
    pthread_t thread;
    pthread_mutex_t mtx;
    pthread_cond_t added, empty;
    std::vector<Arguments> arguments;
};
