/*****************************************************************************
 * x11_display.hpp
 *****************************************************************************
 * Copyright (C) 2003 the VideoLAN team
 * $Id: x11_display.hpp 14187 2006-02-07 16:37:40Z courmisch $
 *
 * Authors: Cyril Deguet     <asmax@via.ecp.fr>
 *          Olivier Teulière <ipkiss@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef X11_DISPLAY_HPP
#define X11_DISPLAY_HPP

#include <X11/Xlib.h>

#include "../src/skin_common.hpp"

// Helper macros
#define XDISPLAY m_rDisplay.getDisplay()
#define XVISUAL m_rDisplay.getVisual()
#define XPIXELSIZE m_rDisplay.getPixelSize()
#define XGC m_rDisplay.getGC()


/// Class for encapsulation of a X11 Display
class X11Display: public SkinObject
{
    public:
        X11Display( intf_thread_t *pIntf );
        virtual ~X11Display();

        /// Get the display
        Display *getDisplay() const { return m_pDisplay; }

        /// Get the visual
        Visual *getVisual() const { return m_pVisual; }

        /// Get the pixel size
        int getPixelSize() const { return m_pixelSize; }

        /// Get the graphics context
        GC getGC() const { return m_gc; }

        /// Get the colormap
        Colormap getColormap() const { return m_colormap; }

        /// Type of function to put RGBA values into a pixel
        typedef void (X11Display::*MakePixelFunc_t)( uint8_t *pPixel,
            uint8_t r, uint8_t g, uint8_t b, uint8_t a ) const;

        /// Get a pointer on the right blendPixel implementation
        MakePixelFunc_t getBlendPixel() const { return blendPixelImpl; }

        /// Get a pointer on the right putPixel implementation
        MakePixelFunc_t getPutPixel() const { return putPixelImpl; }

        /// Get the pixel value corresponding to the given colors
        unsigned long getPixelValue( uint8_t r, uint8_t g, uint8_t b ) const;

        /// Get the main window ID
        Window getMainWindow() const { return m_mainWindow; }

        //XXX
        Window m_voutWindow;

    private:
        /// Dummy parent window for the task bar
        Window m_mainWindow;
        /// Display parameters
        Display *m_pDisplay;
        Visual *m_pVisual;
        int m_pixelSize;
        GC m_gc;
        Colormap m_colormap;
        int m_redLeftShift, m_redRightShift;
        int m_greenLeftShift, m_greenRightShift;
        int m_blueLeftShift, m_blueRightShift;
        /// Pointer on the right implementation of blendPixel
        MakePixelFunc_t blendPixelImpl;
        /// Pointer on the right implementation of putPixel
        MakePixelFunc_t putPixelImpl;

        /// Calculate shifts from a color mask
        void getShifts( uint32_t mask, int &rLeftShift,
                        int &rRightShift ) const;

        /// 8 bpp version of blendPixel
        void blendPixel8( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                          uint8_t a ) const;

        /// 16 bpp MSB first version of blendPixel
        void blendPixel16MSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t a ) const;

        /// 16 bpp LSB first version of blendPixel
        void blendPixel16LSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t a ) const;

        /// 24/32 bpp MSB first version of blendPixel
        void blendPixel32MSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t a ) const;

        /// 24/32 bpp LSB first version of blendPixel
        void blendPixel32LSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t a ) const;

        /// 8 bpp version of putPixel
        void putPixel8( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                        uint8_t a ) const;

        /// 16 bpp MSB first version of putPixel
        void putPixel16MSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a ) const;

        /// 16 bpp LSB first version of putPixel
        void putPixel16LSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a ) const;

        /// 24/32 bpp MSB first version of putPixel
        void putPixel32MSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a ) const;

        /// 24/32 bpp LSB first version of putPixel
        void putPixel32LSB( uint8_t *pPixel, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a ) const;
};

#endif
