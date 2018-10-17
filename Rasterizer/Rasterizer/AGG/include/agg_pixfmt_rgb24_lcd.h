//----------------------------------------------------------------------------
// Anti-Grain Geometry (AGG) - Version 2.5
// A high quality rendering engine for C++
// Copyright (C) 2002-2006 Maxim Shemanarev
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://antigrain.com
// 
// AGG is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// AGG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with AGG; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
// MA 02110-1301, USA.
//----------------------------------------------------------------------------

#ifndef AGG_PIXFMT_RGB24_LCD_INCLUDED
#define AGG_PIXFMT_RGB24_LCD_INCLUDED

#include <string.h>
#include "agg_basics.h"
#include "agg_color_rgba.h"
#include "agg_rendering_buffer.h"

namespace agg
{
 

    //=====================================================lcd_distribution_lut
    class lcd_distribution_lut
    {
    public:
        lcd_distribution_lut(double prim, double second, double tert)
        {
            double norm = 1.0 / (prim + second*2 + tert*2);
            prim   *= norm;
            second *= norm;
            tert   *= norm;
            for(unsigned i = 0; i < 256; i++)
            {
                m_primary[i]   = (unsigned char)floor(prim   * i);
                m_secondary[i] = (unsigned char)floor(second * i);
                m_tertiary[i]  = (unsigned char)floor(tert   * i);
            }
        }

        unsigned primary(unsigned v)   const { return m_primary[v];   }
        unsigned secondary(unsigned v) const { return m_secondary[v]; }
        unsigned tertiary(unsigned v)  const { return m_tertiary[v];  }

    private:
        unsigned char m_primary[256];
        unsigned char m_secondary[256];
        unsigned char m_tertiary[256];
    };





    //========================================================pixfmt_rgb24_lcd
    class pixfmt_rgb24_lcd
    {
    public:
        typedef rgba8 color_type;
        typedef rendering_buffer::row_data row_data;
        typedef color_type::value_type value_type;
        typedef color_type::calc_type calc_type;

        //--------------------------------------------------------------------
        pixfmt_rgb24_lcd(rendering_buffer& rb, const lcd_distribution_lut& lut)
            : m_rbuf(&rb),
              m_lut(&lut)
        {
        }

        //--------------------------------------------------------------------
        unsigned width()  const { return m_rbuf->width() * 3;  }
        unsigned height() const { return m_rbuf->height(); }


        //--------------------------------------------------------------------
        void blend_hline(int x, int y,
                         unsigned len, 
                         const color_type& c,
                         int8u cover)
        {
            int8u* p = m_rbuf->row_ptr(y) + x + x + x;
            int alpha = int(cover) * c.a;
            do
            {
                p[0] = (int8u)((((c.r - p[0]) * alpha) + (p[0] << 16)) >> 16);
                p[1] = (int8u)((((c.g - p[1]) * alpha) + (p[1] << 16)) >> 16);
                p[2] = (int8u)((((c.b - p[2]) * alpha) + (p[2] << 16)) >> 16);
                p += 3;
            }
            while(--len);
        }


        //--------------------------------------------------------------------
        void blend_solid_hspan(int x, int y,
                               unsigned len, 
                               const color_type& c,
                               const int8u* covers)
        {
            int8u c3[2048*3];
            memset(c3, 0, len+4);

            int i;
            for(i = 0; i < int(len); i++)
            {
                c3[i+0] += m_lut->tertiary(covers[i]);
                c3[i+1] += m_lut->secondary(covers[i]);
                c3[i+2] += m_lut->primary(covers[i]);
                c3[i+3] += m_lut->secondary(covers[i]);
                c3[i+4] += m_lut->tertiary(covers[i]);
            }

            x -= 2;
            len += 4;

            if(x < 0)
            {
                len -= x;
                x = 0;
            }

            covers = c3;
            i = x % 3;

            int8u rgb[3] = { c.r, c.g, c.b };
            int8u* p = m_rbuf->row_ptr(y) + x;

            do 
            {
                int alpha = int(*covers++) * c.a;
                if(alpha)
                {
                    if(alpha == 255*255)
                    {
                        *p = (int8u)rgb[i];
                    }
                    else
                    {
                        *p = (int8u)((((rgb[i] - *p) * alpha) + (*p << 16)) >> 16);
                    }
                }
                ++p;
                ++i;
                if(i >= 3) i = 0;
            }
            while(--len);
        }


    private:
        rendering_buffer* m_rbuf;
        const lcd_distribution_lut* m_lut;
    };


}

#endif

