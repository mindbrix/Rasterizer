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
#ifndef AGG_RASTERIZER_CELLS_AA_P_INCLUDED
#define AGG_RASTERIZER_CELLS_AA_P_INCLUDED

#include <math.h>

namespace agg {
    
    struct cell_aa_p { float cover, area; };

    
    
    //-----------------------------------------------------rasterizer_cells_aa_p
    // An internal class that implements the main rasterization algorithm.
    // Used in the rasterizer. Should not be used direcly.
    template<class Cell> class rasterizer_cells_aa_p
    {
    public:
        typedef Cell cell_type;
        typedef rasterizer_cells_aa_p<Cell> self_type;
        
        rasterizer_cells_aa_p();
        
        void reset()  { m_num_cells = 0; }
        void style(const cell_type& style_cell)  {}
        void line_f(float x1, float y1, float x2, float y2);
        
        void clip_box(int x1, int y1, int x2, int y2) {
            rect_i rect(x1, y1, x2, y2);  rect.normalize();
            m_min_x = rect.x1;  m_min_y = rect.y1;  m_max_x = rect.x2;  m_max_y = rect.y2;
            m_width_f = m_width = m_max_x - m_min_x;  m_height = m_max_y - m_min_y;
        }
        rect_i clip_box() const { return rect_i(m_min_x, m_min_y, m_max_x, m_max_y); }
        
        int min_x() const { return m_min_x; }
        int min_y() const { return m_min_y; }
        int max_x() const { return m_max_x; }
        int max_y() const { return m_max_y; }
        int width() const { return m_width; }
        int height() const { return m_height; }
        
        void sort_cells()  {}
        bool sorted() const { return true; }
        
        unsigned total_cells() const  { return m_width * m_height; }
        cell_type* supercell() { return m_supercell; }
        
    private:
        rasterizer_cells_aa_p(const self_type&);
        const self_type& operator = (const self_type&);
        
        void render_hline_f(cell_type *cell_row, float x1, float y1, float x2, float y2);
        
    private:
        unsigned                m_num_cells;
        cell_type               m_supercell[max_supercell_size];
        int                     m_min_x, m_min_y, m_max_x, m_max_y;
        int                     m_width, m_height;
        float                   m_width_f;
    };


    //------------------------------------------------------------------------
    template<class Cell>
    rasterizer_cells_aa_p<Cell>::rasterizer_cells_aa_p() :
    m_num_cells(0), m_min_x(0x7FFFFFFF), m_min_y(0x7FFFFFFF), m_max_x(-0x7FFFFFFF), m_max_y(-0x7FFFFFFF) {
        memset(m_supercell, 0, sizeof(m_supercell));
    }
    
    //------------------------------------------------------------------------
    
    template<class Cell>
    AGG_INLINE void rasterizer_cells_aa_p<Cell>::render_hline_f(cell_type *cell_row, float x1, float y1, float x2, float y2) {
        if (x1 > x2)
            std::swap(x1, x2);
        
        float dx = x2 - x1;  float dy = y2 - y1;
        float ex1 = int(x1);  float ex2 = int(x2);
        auto curr_cell = cell_row + (int)x1;
        
        if (ex1 == ex2) {
            float fx1 = x1 - ex1;  float fx2 = x2 - ex2;
            float fx1_plus_fx2 = fx1 + fx2;
            curr_cell->cover += dy;
            curr_cell->area  += fx1_plus_fx2 * dy;
            return;
        }
        
        float dydx = dy / dx;
        float px = x1;
        float ex = ex1, y = y1, delta;
        
        do {
            float fx1 = px - ex;
            ex += 1.0f;
            delta = (ex - px) * dydx;
            if (fx1 < 1.0)  fx1 += 1.0f;
            curr_cell->cover += delta;
            curr_cell->area  += fx1 * delta;
            px = ex;
            y += delta;
            curr_cell++;
        } while (ex != ex2);
        
        delta = y2 - y;
        curr_cell->cover += delta;
        curr_cell->area  += (x2 - ex) * delta;
    }
    
    template<class Cell>
    AGG_INLINE void rasterizer_cells_aa_p<Cell>::line_f(float x1, float y1, float x2, float y2) {
        //trivial case. Happens often
        if (y1 == y2)  return;
        
        float ey1 = int(y1);  float ey2 = int(y2);
        
        if (ey1 == ey2) {
            auto cell_row  = m_supercell + int(ey1 * m_width_f);
            render_hline_f(cell_row, x1, y1, x2, y2);
            return;
        }
        
        bool flipped = false;
        
        if (y1 > y2) {
            flipped = true;
            std::swap(x1, x2);
            std::swap(y1, y2);
            std::swap(ey1, ey2);
        }
        auto cell_row = m_supercell + int(ey1 * m_width_f);
        
        float dx = x2 - x1;  float dy = y2 - y1;  float dxdy = dx / dy;
        float px = x1, py = y1;
        float x, ey = ey1;
        
        do {
            ey += 1.0f;
            x = x1 + (ey - y1) * dxdy;
            if (flipped)
                render_hline_f(cell_row, x, ey, px, py);
            else
                render_hline_f(cell_row, px, py, x, ey);
            px = x;  py = ey;
            cell_row += m_width;
        } while (ey != ey2);
        
        if (flipped)
            render_hline_f(cell_row, x2, y2, x, ey);
        else
            render_hline_f(cell_row, x, ey, x2, y2);
    }
}

#endif
