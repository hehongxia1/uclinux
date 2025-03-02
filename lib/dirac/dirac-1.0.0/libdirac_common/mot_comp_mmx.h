/* ***** BEGIN LICENSE BLOCK *****
*
* $Id: mot_comp_mmx.h,v 1.4 2008/01/09 10:50:23 asuraparaju Exp $ $Name: Dirac_1_0_0 $
*
* Version: MPL 1.1/GPL 2.0/LGPL 2.1
*
* The contents of this file are subject to the Mozilla Public License
* Version 1.1 (the "License"); you may not use this file except in compliance
* with the License. You may obtain a copy of the License at
* http://www.mozilla.org/MPL/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for
* the specific language governing rights and limitations under the License.
*
* The Original Code is BBC Research and Development code.
*
* The Initial Developer of the Original Code is the British Broadcasting
* Corporation.
* Portions created by the Initial Developer are Copyright (C) 2004.
* All Rights Reserved.
*
* Contributor(s): Anuradha Suraparaju (Original Author),
*
* Alternatively, the contents of this file may be used under the terms of
* the GNU General Public License Version 2 (the "GPL"), or the GNU Lesser
* Public License Version 2.1 (the "LGPL"), in which case the provisions of
* the GPL or the LGPL are applicable instead of those above. If you wish to
* allow use of your version of this file only under the terms of the either
* the GPL or LGPL and not to allow others to use your version of this file
* under the MPL, indicate your decision by deleting the provisions above
* and replace them with the notice and other provisions required by the GPL
* or LGPL. If you do not delete the provisions above, a recipient may use
* your version of this file under the terms of any one of the MPL, the GPL
* or the LGPL.
* ***** END LICENSE BLOCK ***** */

//  Motion Compensation routines.
//  Supports different sizes of blocks as long as the parameters
//     describing them are 'legal'. Blocks overlap the edge of the image
//     being written to but blocks in the reference image are forced to
//     lie completely within the image bounds.

#ifndef _INCLUDED_MOT_COMP_MMX_H
#define _INCLUDED_MOT_COMP_MMX_H

#if defined (HAVE_MMX)
#include <libdirac_common/common.h>
#include <libdirac_common/motion.h>
namespace dirac
{

    void CompensateComponentAddAndShift_mmx (int start_y, int end_y,
                                           int weight_bits,
                                           const ImageCoords& orig_pic_size, 
                                           TwoDArray<ValueType> &comp_data, 
                                           PicArray &pic_data_out);
    
    void AddMCBlock_mmx (const ImageCoords& start_pos, TwoDArray<ValueType> &comp_strip, TwoDArray<ValueType>& block_data);
}

#endif // HAVE_MMX

#endif
