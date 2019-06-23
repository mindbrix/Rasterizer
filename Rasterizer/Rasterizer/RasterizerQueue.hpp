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
    struct Arguments {
        Arguments() {}
        Arguments(Function function, void *info) : function(function), info(info) {}
        Function function = nullptr;
        void *info = nullptr;
    };
    static void *queue_main(void *arguments) {
        RasterizerQueue *queue = (RasterizerQueue *)arguments;
        while (1) {
            Arguments args = queue->get();
            (*args.function)(args.info);
            queue->pop();
        }
        return 0;
    }
    RasterizerQueue() {
        pthread_mutex_init(& mtx, NULL);
        pthread_cond_init(& added, NULL);
        pthread_cond_init(& removed, NULL);
        pthread_create(& thread, NULL, queue_main, (void *)this);
    }
    ~RasterizerQueue() {
        pthread_mutex_lock(& mtx);
        pthread_cancel(thread);
        pthread_mutex_destroy(& mtx);
        pthread_cond_destroy(& added);
        pthread_cond_destroy(& removed);
        // pthread_join(thread, NULL);
    }
    void add(Function function, void *info) {
        pthread_mutex_lock(& mtx);
        arguments.emplace_back(function, info);
        pthread_cond_signal(& added);
        pthread_mutex_unlock(& mtx);
    }
    Arguments get() {
        pthread_mutex_lock(& mtx);
        while (arguments.size() == 0)
            pthread_cond_wait(& added, & mtx);
        Arguments args = arguments[0];
        pthread_mutex_unlock(& mtx);
        return args;
    }
    void pop() {
        pthread_mutex_lock(& mtx);
        arguments.erase(arguments.begin());
        pthread_cond_signal(& removed);
        pthread_mutex_unlock(& mtx);
    }
    void foreach(Function function, void *arguments, size_t count, size_t stride) {
        uint8_t *base = (uint8_t *)arguments;
        for (int i = 0; i < count; i++, base += stride)
            add(function, (void *)base);
    }
    void wait() {
        pthread_mutex_lock(& mtx);
        while (arguments.size())
            pthread_cond_wait(& removed, & mtx);
        pthread_mutex_unlock(& mtx);
    }
    pthread_t thread;
    pthread_mutex_t mtx;
    pthread_cond_t added, removed;
    std::vector<Arguments> arguments;
};
