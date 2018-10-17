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
//
// The author gratefully acknowleges the support of David Turner, 
// Robert Wilhelm, and Werner Lemberg - the authors of the FreeType 
// libray - in producing this work. See http://www.freetype.org for details.
//
//----------------------------------------------------------------------------
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://www.antigrain.com
//----------------------------------------------------------------------------
//
// Adaptation for 32-bit screen coordinates has been sponsored by 
// Liberty Technology Systems, Inc., visit http://lib-sys.com
//
// Liberty Technology Systems, Inc. is the provider of
// PostScript and PDF technology for software developers.
// 
//----------------------------------------------------------------------------
#ifndef AGG_RASTERIZER_SCANLINE_AA_P_INCLUDED
#define AGG_RASTERIZER_SCANLINE_AA_P_INCLUDED

#include "agg_rasterizer_cells_aa_p.h"


namespace agg
{
    template<class Clip=rasterizer_sl_clip_int> class rasterizer_scanline_aa_p
    {
        enum status
        {
            status_initial,
            status_move_to,
            status_line_to,
            status_closed
        };

    public:
        typedef typename Clip::rect_type  rect_type;

        enum aa_scale_e
        {
            aa_shift  = 8,
            aa_scale  = 1 << aa_shift,
            aa_mask   = aa_scale - 1,
            aa_scale2 = aa_scale * 2,
            aa_mask2  = aa_scale2 - 1
        };

        //--------------------------------------------------------------------
        rasterizer_scanline_aa_p() : 
            m_outline(),
            m_filling_rule(fill_non_zero),
            m_auto_close(true),
            m_start_x(0), m_start_y(0),
            m_x(0), m_y(0),
            m_status(status_initial) {}

        void filling_rule(filling_rule_e filling_rule) { m_filling_rule = filling_rule; }
        void auto_close(bool flag) { m_auto_close = flag; }
        
        int min_x() const  { return m_outline.min_x(); }
        int min_y() const  { return m_outline.min_y(); }
        int max_x() const  { return m_outline.max_x(); }
        int max_y() const  { return m_outline.max_y(); }
        int width() const  { return m_outline.width(); }
        int height() const { return m_outline.height(); }
        
        void clip_box(float x1, float y1, float x2, float y2) { m_outline.clip_box(x1, y1, x2, y2); }
        rect_type clip_box() const { return m_outline.clip_box(); }
        
        //--------------------------------------------------------------------
        inline void reset() {
            m_x = m_y = 0;
            m_outline.reset();
            m_status = status_initial;
        }
        
        inline void move_to(float x, float y) {
            if (m_auto_close) close_polygon();
            
            _start_x = _x = x;  _start_y = _y = y;
            
            m_status = status_move_to;
        }
        
        inline void line_to(float x, float y) {
            m_outline.line_f(_x, _y, x, y);
            _x = x;  _y = y;
            
            m_status = status_line_to;
        }
        
        inline void last(float x, float y) {}
        
        inline void close_polygon() {
            if(m_status == status_line_to) {
                m_outline.line_f(_x, _y, _start_x, _start_y);
                
                m_status = status_closed;
            }
        }
        
        inline bool rewind_scanlines() {
            if(m_auto_close) close_polygon();
            if(m_outline.total_cells() == 0) {
                return false;
            }
            return true;
        }
        
        inline unsigned calculate_alpha(float area) const {
            int cover = abs(area);
            
            if (m_filling_rule == fill_even_odd) {
                cover &= aa_mask2;
                if(cover > aa_scale)
                {
                    cover = aa_scale2 - cover;
                }
            }
            if (cover > aa_mask) cover = aa_mask;
            return cover;
        }

        inline uint8_t *make_mask() {
            rewind_scanlines();
            
            uint8_t *mask = _mask;
            cell_aa_p* cur_cell = m_outline.supercell();
            
            auto h = m_outline.height();
            while (h--) {
                unsigned w = m_outline.width();
                
                float cover = 0.0f;
                while(w--) {
                    float area = cur_cell->area * 128.0f;
                    cover += cur_cell->cover * 256.0f;
                    cur_cell->area = cur_cell->cover = 0;
                    cur_cell++;
                    
                    unsigned alpha = calculate_alpha(cover - area);
                    *mask++ = alpha;
                }
            }
            return _mask;
        }

        rasterizer_cells_aa_p<cell_aa_p> m_outline;
    private:
        //--------------------------------------------------------------------
        // Disable copying
        rasterizer_scanline_aa_p(const rasterizer_scanline_aa_p<Clip>&);
        const rasterizer_scanline_aa_p<Clip>& 
        operator = (const rasterizer_scanline_aa_p<Clip>&);
        
        
        filling_rule_e m_filling_rule;
        bool           m_auto_close;
        float          m_start_x;
        float          m_start_y;
        unsigned       m_status;
        int            m_x, m_y;
        float          _start_x, _start_y, _x, _y;
        uint8_t        _mask[max_supercell_size];
    };
}

#endif
