//
//  RasterizerScene.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 06/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"

struct RasterizerScene {
    struct Scene {
        void empty() { ctms.resize(0), paths.resize(0), bgras.resize(0); }
        void addPath(Rasterizer::Path path, Rasterizer::AffineTransform ctm, uint8_t *bgra) {
            bgras.emplace_back(*((uint32_t *)bgra));
            paths.emplace_back(path);
            ctms.emplace_back(ctm);
        }
        std::vector<uint32_t> bgras;
        std::vector<Rasterizer::AffineTransform> ctms;
        std::vector<Rasterizer::Path> paths;
    };
};
