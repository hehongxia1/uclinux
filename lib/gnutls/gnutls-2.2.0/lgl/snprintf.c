/* Formatted output to strings.
   Copyright (C) 2004, 2006-2007 Free Software Foundation, Inc.
   Written by Simon Josefsson and Paul Eggert.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License along
   with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <config.h>

/* Specification.  */
#include <stdio.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vasnprintf.h"

/* Some systems, like OSF/1 4.0 and Woe32, don't have EOVERFLOW.  */
#ifndef EOVERFLOW
# define EOVERFLOW E2BIG
#endif

/* Print formatted output to string STR.  Similar to sprintf, but
   additional length SIZE limit how much is written into STR.  Returns
   string length of formatted string (which may be larger than SIZE).
   STR may be NULL, in which case nothing will be written.  On error,
   return a negative value.  */
int
snprintf (char *str, size_t size, const char *format, ...)
{
  char *output;
  size_t len;
  size_t lenbuf = size;
  va_list args;

  va_start (args, format);
  output = vasnprintf (str, &lenbuf, format, args);
  len = lenbuf;
  va_end (args);

  if (!output)
    return -1;

  if (output != str)
    {
      if (size)
	{
	  size_t pruned_len = (len < size ? len : size - 1);
	  memcpy (str, output, pruned_len);
	  str[pruned_len] = '\0';
	}

      free (output);
    }

  if (INT_MAX < len)
    {
      errno = EOVERFLOW;
      return -1;
    }

  return len;
}
