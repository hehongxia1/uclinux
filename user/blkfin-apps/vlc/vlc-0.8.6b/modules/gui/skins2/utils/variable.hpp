/*****************************************************************************
 * variable.hpp
 *****************************************************************************
 * Copyright (C) 2003 the VideoLAN team
 * $Id: variable.hpp 14187 2006-02-07 16:37:40Z courmisch $
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

#ifndef VARIABLE_HPP
#define VARIABLE_HPP

#include "../src/skin_common.hpp"
#include "pointer.hpp"
#include <string>


/// Base class for variable objects
class Variable: public SkinObject
{
    public:
        virtual ~Variable() {}

        /// Get the variable type
        virtual const string &getType() const = 0;

    protected:
        Variable( intf_thread_t *pIntf ): SkinObject( pIntf ) {}
};


typedef CountedPtr<Variable> VariablePtr;


#endif
