/* $Id$ */

/* GNUPLOT - ctrl87.h */

/*[
 * Copyright 1986 - 1993, 1998   Thomas Williams, Colin Kelley
 *
 * Permission to use, copy, and distribute this software and its
 * documentation for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.
 *
 * Permission to modify the software is granted, but not the right to
 * distribute the complete modified source code.  Modifications are to
 * be distributed as patches to the released version.  Permission to
 * distribute binaries produced by compiling modified sources is granted,
 * provided you
 *   1. distribute the corresponding source modifications from the
 *    released version in the form of a patch file along with the binaries,
 *   2. add special version identification to distinguish your version
 *    in addition to the base release version number,
 *   3. provide your name and address as the primary contact for the
 *    support of your modified version, and
 *   4. retain our contact information in regard to use of the base
 *    software.
 * Permission to distribute the released version of the source code along
 * with corresponding source modifications in the form of a patch file is
 * granted with same provisions 2 through 4 for binary distributions.
 *
 * This software is provided "as is" without express or implied warranty
 * to the extent permitted by applicable law.
]*/


#ifndef __CONTROL87_H__


#define __CONTROL87_H__


#ifdef __CONTROL87_C__
#define EXTERN
#else
#define EXTERN extern
#endif


/* 8087/80287 Status Word format   */

#define SW_INVALID      0x0001  /* Invalid operation            */
#define SW_DENORMAL     0x0002  /* Denormalized operand         */
#define SW_ZERODIVIDE   0x0004  /* Zero divide                  */
#define SW_OVERFLOW     0x0008  /* Overflow                     */
#define SW_UNDERFLOW    0x0010  /* Underflow                    */
#define SW_INEXACT      0x0020  /* Precision (Inexact result)   */

/* 8087/80287 Control Word format */

#define MCW_EM              0x003f  /* interrupt Exception Masks*/
#define     EM_INVALID      0x0001  /*   invalid                */
#define     EM_DENORMAL     0x0002  /*   denormal               */
#define     EM_ZERODIVIDE   0x0004  /*   zero divide            */
#define     EM_OVERFLOW     0x0008  /*   overflow               */
#define     EM_UNDERFLOW    0x0010  /*   underflow              */
#define     EM_INEXACT      0x0020  /*   inexact (precision)    */

#define MCW_IC              0x1000  /* Infinity Control */
#define     IC_AFFINE       0x1000  /*   affine         */
#define     IC_PROJECTIVE   0x0000  /*   projective     */

#define MCW_RC          0x0c00  /* Rounding Control     */
#define     RC_CHOP     0x0c00  /*   chop               */
#define     RC_UP       0x0800  /*   up                 */
#define     RC_DOWN     0x0400  /*   down               */
#define     RC_NEAR     0x0000  /*   near               */

#define MCW_PC          0x0300  /* Precision Control    */
#define     PC_24       0x0000  /*    24 bits           */
#define     PC_53       0x0200  /*    53 bits           */
#define     PC_64       0x0300  /*    64 bits           */

/**************************************************************************/
/*************************   Type declarations   **************************/
/**************************************************************************/

/**************************************************************************/
/************************   Function declarations   ***********************/
/**************************************************************************/

/*
   _control87 changes floating-point control word.

   Declaration:
   ------------
     unsigned short _control87(unsigned short newcw, unsigned short mask);

   Remarks:
   --------
  _control87 retrieves or changes the floating-point control word.

  The floating-point control word is an unsigned short that specifies the
  following modes in the 80x87 FPU:
   o  allowed exceptions
   o  infinity mode
   o  rounding mode
   o  precision mode

  Changing these modes allows you to mask or unmask floating-point exceptions.

  _control87 matches the bits in mask to the bits in newcw.

  If any mask bit = 1, the corresponding bit in newcw contains the new value
  for the same bit in the floating-point control word.

  If mask = 0000, _control87 returns the floating-point control word without
  altering it.

   Examples:
   ---------
  Switching to projective infinity mode:
     _control87(IC_PROJECTIVE, MCW_IC);

  Disabling all exceptions:
     _control87(MCW_EM, MCW_EM);

   Return Value:
   -------------
  The bits in the value returned reflect the new floating-point control word.
*/
EXTERN unsigned short _control87(unsigned short newcw, unsigned short mask);


/**************************************************************************/
/**************************   Global variables   **************************/
/**************************************************************************/



#ifdef __CONTROL87_C__
#else
#endif


#undef EXTERN


#endif
