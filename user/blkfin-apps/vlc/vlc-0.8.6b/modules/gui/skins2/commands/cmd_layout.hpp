/*****************************************************************************
 * cmd_layout.hpp
 *****************************************************************************
 * Copyright (C) 2003 the VideoLAN team
 * $Id: cmd_layout.hpp 14187 2006-02-07 16:37:40Z courmisch $
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

#ifndef CMD_LAYOUT_HPP
#define CMD_LAYOUT_HPP

#include "cmd_generic.hpp"

class TopWindow;
class GenericLayout;

/// "Change layout" command
class CmdLayout: public CmdGeneric
{
    public:
        CmdLayout( intf_thread_t *pIntf, TopWindow &rWindow,
                   GenericLayout &rLayout );
        virtual ~CmdLayout() {}

        /// This method does the real job of the command
        virtual void execute();

        /// Return the type of the command
        virtual string getType() const { return "change layout"; }

    private:
        TopWindow &m_rWindow;
        GenericLayout &m_rLayout;
};

#endif
