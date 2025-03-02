/*
   (c) Copyright 2001-2008  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#ifndef __SDL__SDL_H__
#define __SDL__SDL_H__

#include <fusion/call.h>
#include <fusion/lock.h>

#include <core/surface_pool.h>
#include <core/system.h>

typedef struct {
     FusionSkirmish   lock;
     FusionCall       call;

     CoreSurface     *primary;
     CoreSurfacePool *sdl_pool;

     struct {
          pthread_mutex_t  lock;
          pthread_cond_t   cond;

          DirectThread    *thread;

          bool             pending;
          DFBRegion        region;

          bool             quit;
     } update;

     VideoMode            *modes;        /* linked list of valid video modes */

     SDL_Surface          *screen;
} DFBSDL;

extern DFBSDL  *dfb_sdl D_HIDDEN;
extern CoreDFB *dfb_sdl_core D_HIDDEN;

DFBResult dfb_sdl_set_video_mode( CoreDFB *core, CoreSurfaceConfig *config ) D_HIDDEN;
DFBResult dfb_sdl_update_screen( CoreDFB *core, DFBRegion *region ) D_HIDDEN;
DFBResult dfb_sdl_set_palette( CorePalette *palette ) D_HIDDEN;

#endif

