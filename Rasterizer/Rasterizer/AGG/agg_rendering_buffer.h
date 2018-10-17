//----------------------------------------------------------------------------
// Anti-Grain Geometry - Version 2.4
// Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com)
//
// Permission to copy, use, modify, sell and distribute this software 
// is granted provided this copyright notice appears in all copies. 
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
//----------------------------------------------------------------------------
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://www.antigrain.com
//----------------------------------------------------------------------------
//
// class rendering_buffer
//
//----------------------------------------------------------------------------

#import <cassert>

#ifndef AGG_RENDERING_BUFFER_INCLUDED
#define AGG_RENDERING_BUFFER_INCLUDED

namespace agg
{

    template<class T>
    class row_accessor
    {
    public:
        row_accessor() : m_buf(0), m_start(0), m_width(0), m_height(0), m_stride(0), m_bpp(0) {}

        row_accessor(T* buf, int width, int height, int stride, int bpp) :
            m_buf(buf), m_start(buf), m_width(width), m_height(height), m_stride(stride), m_bpp(bpp) {}

        inline       T* buf()          { return m_buf;    }
        inline const T* buf()    const { return m_buf;    }
        inline int width()       const { return m_width;  }
        inline int height()      const { return m_height; }
        inline int      stride() const { return m_stride; }

        inline       T* pixel_ptr(int x, int y, int w, int h) {
            assert(x > -1 && x < m_width);
            assert(y > -1 && y < m_height);
            assert(x + w <= m_width);
            assert(y + h <= m_height);
            return m_start + y * m_stride + x * m_bpp;
        }
    
    private:
        T*            m_buf;    // Pointer to renrdering buffer
        T*            m_start;  // Pointer to first pixel depending on stride 
        unsigned      m_width;  // Width in pixels
        unsigned      m_height; // Height in pixels
        int           m_stride; // Number of bytes per row
        int           m_bpp;    // Bytes per pixel
    };
}


#endif
