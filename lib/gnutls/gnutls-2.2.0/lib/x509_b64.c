/*
 * Copyright (C) 2000, 2001, 2003, 2004, 2005 Free Software Foundation
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of GNUTLS.
 *
 * The GNUTLS library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA
 *
 */

/* Functions that relate to base64 encoding and decoding.
 */

#include "gnutls_int.h"
#include "gnutls_errors.h"
#include <gnutls_datum.h>
#include <x509_b64.h>

static const uint8_t b64table[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t asciitable[128] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0x3e, 0xff, 0xff, 0xff, 0x3f,
  0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff,
  0xff, 0xf1, 0xff, 0xff, 0xff, 0x00,	/* 0xf1 for '=' */
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
  0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
  0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
  0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
  0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
  0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
  0x31, 0x32, 0x33, 0xff, 0xff, 0xff,
  0xff, 0xff
};

inline static int
encode (char *result, const uint8_t * data, int left)
{

  int data_len;

  if (left > 3)
    data_len = 3;
  else
    data_len = left;

  switch (data_len)
    {
    case 3:
      result[0] = b64table[(data[0] >> 2)];
      result[1] =
	b64table[(((((data[0] & 0x03) & 0xff) << 4) & 0xff) |
		  (data[1] >> 4))];
      result[2] =
	b64table[((((data[1] & 0x0f) << 2) & 0xff) | (data[2] >> 6))];
      result[3] = b64table[(((data[2] << 2) & 0xff) >> 2)];
      break;
    case 2:
      result[0] = b64table[(data[0] >> 2)];
      result[1] =
	b64table[(((((data[0] & 0x03) & 0xff) << 4) & 0xff) |
		  (data[1] >> 4))];
      result[2] = b64table[(((data[1] << 4) & 0xff) >> 2)];
      result[3] = '=';
      break;
    case 1:
      result[0] = b64table[(data[0] >> 2)];
      result[1] = b64table[(((((data[0] & 0x03) & 0xff) << 4) & 0xff))];
      result[2] = '=';
      result[3] = '=';
      break;
    default:
      return -1;
    }

  return 4;

}

/* data must be 4 bytes
 * result should be 3 bytes
 */
#define TOASCII(c) (c < 127 ? asciitable[c] : 0xff)
inline static int
decode (uint8_t * result, const opaque * data)
{
  uint8_t a1, a2;
  int ret = 3;

  a1 = TOASCII (data[0]);
  a2 = TOASCII (data[1]);
  if (a1 == 0xff || a2 == 0xff)
    return -1;
  result[0] = ((a1 << 2) & 0xff) | ((a2 >> 4) & 0xff);

  a1 = a2;
  a2 = TOASCII (data[2]);
  if (a2 == 0xff)
    return -1;
  result[1] = ((a1 << 4) & 0xff) | ((a2 >> 2) & 0xff);

  a1 = a2;
  a2 = TOASCII (data[3]);
  if (a2 == 0xff)
    return -1;
  result[2] = ((a1 << 6) & 0xff) | (a2 & 0xff);

  if (data[2] == '=')
    ret--;

  if (data[3] == '=')
    ret--;
  return ret;
}

/* encodes data and puts the result into result (locally allocated)
 * The result_size is the return value
 */
int
_gnutls_base64_encode (const uint8_t * data, size_t data_size,
		       uint8_t ** result)
{
  unsigned int i, j;
  int ret, tmp;
  char tmpres[4];

  ret = B64SIZE (data_size);

  (*result) = gnutls_malloc (ret + 1);
  if ((*result) == NULL)
    return GNUTLS_E_MEMORY_ERROR;

  for (i = j = 0; i < data_size; i += 3, j += 4)
    {
      tmp = encode (tmpres, &data[i], data_size - i);
      if (tmp == -1)
	{
	  gnutls_free ((*result));
	  return GNUTLS_E_MEMORY_ERROR;
	}
      memcpy (&(*result)[j], tmpres, tmp);
    }
  (*result)[ret] = 0;		/* null terminated */

  return ret;
}

#define INCR(what, size) \
	do { \
	what+=size; \
	if (what > ret) { \
		gnutls_assert(); \
		gnutls_free( (*result)); *result = NULL; \
		return GNUTLS_E_INTERNAL_ERROR; \
	} \
	} while(0)

/* encodes data and puts the result into result (locally allocated)
 * The result_size (including the null terminator) is the return value.
 */
int
_gnutls_fbase64_encode (const char *msg, const uint8_t * data,
			int data_size, uint8_t ** result)
{
  int i, ret, tmp, j;
  char tmpres[4];
  uint8_t *ptr;
  uint8_t top[80];
  uint8_t bottom[80];
  int pos, bytes, top_len, bottom_len;
  size_t msglen = strlen (msg);

  if (msglen > 50)
    {
      gnutls_assert ();
      return GNUTLS_E_BASE64_ENCODING_ERROR;
    }

  memset (bottom, 0, sizeof (bottom));
  memset (top, 0, sizeof (top));

  strcat (top, "-----BEGIN ");	/* Flawfinder: ignore */
  strcat (top, msg);		/* Flawfinder: ignore */
  strcat (top, "-----");	/* Flawfinder: ignore */

  strcat (bottom, "\n-----END ");	/* Flawfinder: ignore */
  strcat (bottom, msg);		/* Flawfinder: ignore */
  strcat (bottom, "-----\n");	/* Flawfinder: ignore */

  top_len = strlen (top);
  bottom_len = strlen (bottom);

  ret = B64FSIZE (msglen, data_size);

  (*result) = gnutls_calloc (1, ret + 1);
  if ((*result) == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  bytes = pos = 0;
  INCR (bytes, top_len);
  pos = top_len;

  strcpy (*result, top);	/* Flawfinder: ignore */

  for (i = j = 0; i < data_size; i += 3, j += 4)
    {

      tmp = encode (tmpres, &data[i], data_size - i);
      if (tmp == -1)
	{
	  gnutls_assert ();
	  gnutls_free ((*result));
	  *result = NULL;
	  return GNUTLS_E_BASE64_ENCODING_ERROR;
	}

      INCR (bytes, 4);
      ptr = &(*result)[j + pos];

      if ((j) % 64 == 0)
	{
	  INCR (bytes, 1);
	  pos++;
	  *ptr++ = '\n';
	}
      *ptr++ = tmpres[0];

      if ((j + 1) % 64 == 0)
	{
	  INCR (bytes, 1);
	  pos++;
	  *ptr++ = '\n';
	}
      *ptr++ = tmpres[1];

      if ((j + 2) % 64 == 0)
	{
	  INCR (bytes, 1);
	  pos++;
	  *ptr++ = '\n';
	}
      *ptr++ = tmpres[2];

      if ((j + 3) % 64 == 0)
	{
	  INCR (bytes, 1);
	  pos++;
	  *ptr++ = '\n';
	}
      *ptr++ = tmpres[3];
    }

  INCR (bytes, bottom_len);

  memcpy (&(*result)[bytes - bottom_len], bottom, bottom_len);
  (*result)[bytes] = 0;

  return ret + 1;
}

/**
  * gnutls_pem_base64_encode - This function will convert raw data to Base64 encoded
  * @msg: is a message to be put in the header
  * @data: contain the raw data
  * @result: the place where base64 data will be copied
  * @result_size: holds the size of the result
  *
  * This function will convert the given data to printable data, using the base64 
  * encoding. This is the encoding used in PEM messages. If the provided
  * buffer is not long enough GNUTLS_E_SHORT_MEMORY_BUFFER is returned.
  *
  * The output string will be null terminated, although the size will not include
  * the terminating null.
  * 
  **/
int
gnutls_pem_base64_encode (const char *msg, const gnutls_datum_t * data,
			  char *result, size_t * result_size)
{
  opaque *ret;
  int size;

  size = _gnutls_fbase64_encode (msg, data->data, data->size, &ret);
  if (size < 0)
    return size;

  if (result == NULL || *result_size < (unsigned) size)
    {
      gnutls_free (ret);
      *result_size = size;
      return GNUTLS_E_SHORT_MEMORY_BUFFER;
    }
  else
    {
      memcpy (result, ret, size);
      gnutls_free (ret);
      *result_size = size - 1;
    }

  return 0;
}

/**
  * gnutls_pem_base64_encode_alloc - This function will convert raw data to Base64 encoded
  * @msg: is a message to be put in the encoded header
  * @data: contains the raw data
  * @result: will hold the newly allocated encoded data
  *
  * This function will convert the given data to printable data, using the base64 
  * encoding. This is the encoding used in PEM messages. This function will
  * allocate the required memory to hold the encoded data.
  *
  * You should use gnutls_free() to free the returned data.
  * 
  **/
int
gnutls_pem_base64_encode_alloc (const char *msg,
				const gnutls_datum_t * data,
				gnutls_datum_t * result)
{
  opaque *ret;
  int size;

  if (result == NULL)
    return GNUTLS_E_INVALID_REQUEST;

  size = _gnutls_fbase64_encode (msg, data->data, data->size, &ret);
  if (size < 0)
    return size;

  result->data = ret;
  result->size = size - 1;
  return 0;
}


/* decodes data and puts the result into result (locally allocated)
 * The result_size is the return value
 */
int
_gnutls_base64_decode (const uint8_t * data, size_t data_size,
		       uint8_t ** result)
{
  unsigned int i, j;
  int ret, tmp, est;
  uint8_t tmpres[3];

  est = ((data_size * 3) / 4) + 1;
  (*result) = gnutls_malloc (est);
  if ((*result) == NULL)
    return GNUTLS_E_MEMORY_ERROR;

  ret = 0;
  for (i = j = 0; i < data_size; i += 4, j += 3)
    {
      tmp = decode (tmpres, &data[i]);
      if (tmp < 0)
	{
	  gnutls_free (*result);
	  *result = NULL;
	  return tmp;
	}
      memcpy (&(*result)[j], tmpres, tmp);
      ret += tmp;
    }
  return ret;
}

/* copies data to result but removes newlines and <CR>
 * returns the size of the data copied.
 */
inline static int
cpydata (const uint8_t * data, int data_size, uint8_t ** result)
{
  int i, j;

  (*result) = gnutls_malloc (data_size);
  if (*result == NULL)
    return GNUTLS_E_MEMORY_ERROR;

  for (j = i = 0; i < data_size; i++)
    {
      if (data[i] == '\n' || data[i] == '\r')
	continue;
      (*result)[j] = data[i];
      j++;
    }
  return j;
}

/* Searches the given string for ONE PEM encoded certificate, and
 * stores it in the result.
 *
 * The result_size is the return value
 */
#define ENDSTR "-----\n"
#define ENDSTR2 "-----\r"
int
_gnutls_fbase64_decode (const char *header, const opaque * data,
			size_t data_size, uint8_t ** result)
{
  int ret;
  static const char top[] = "-----BEGIN ";
  static const char bottom[] = "\n-----END ";
  uint8_t *rdata;
  int rdata_size;
  uint8_t *kdata;
  int kdata_size;
  char pem_header[128];

  _gnutls_str_cpy (pem_header, sizeof (pem_header), top);
  if (header != NULL)
    _gnutls_str_cat (pem_header, sizeof (pem_header), header);

  rdata = memmem (data, data_size, pem_header, strlen (pem_header));

  if (rdata == NULL)
    {
      gnutls_assert ();
      _gnutls_debug_log ("Could not find '%s'\n", pem_header);
      return GNUTLS_E_BASE64_UNEXPECTED_HEADER_ERROR;
    }

  data_size -= (unsigned long int) rdata - (unsigned long int) data;

  if (data_size < 4 + strlen (bottom))
    {
      gnutls_assert ();
      return GNUTLS_E_BASE64_DECODING_ERROR;
    }

  kdata = memmem (rdata, data_size, ENDSTR, sizeof (ENDSTR) - 1);
  /* allow CR as well.
   */
  if (kdata == NULL)
    kdata = memmem (rdata, data_size, ENDSTR2, sizeof (ENDSTR2) - 1);

  if (kdata == NULL)
    {
      gnutls_assert ();
      _gnutls_x509_log ("Could not find '%s'\n", ENDSTR);
      return GNUTLS_E_BASE64_DECODING_ERROR;
    }
  data_size -= strlen (ENDSTR);
  data_size -= (unsigned long int) kdata - (unsigned long int) rdata;

  rdata = kdata + strlen (ENDSTR);

  /* position is now after the ---BEGIN--- headers */

  kdata = memmem (rdata, data_size, bottom, strlen (bottom));
  if (kdata == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_BASE64_DECODING_ERROR;
    }

  /* position of kdata is before the ----END--- footer 
   */
  rdata_size = (unsigned long int) kdata - (unsigned long int) rdata;

  if (rdata_size < 4)
    {
      gnutls_assert ();
      return GNUTLS_E_BASE64_DECODING_ERROR;
    }

  kdata_size = cpydata (rdata, rdata_size, &kdata);

  if (kdata_size < 0)
    {
      gnutls_assert ();
      return kdata_size;
    }

  if (kdata_size < 4)
    {
      gnutls_assert ();
      gnutls_free (kdata);
      return GNUTLS_E_BASE64_DECODING_ERROR;
    }

  if ((ret = _gnutls_base64_decode (kdata, kdata_size, result)) < 0)
    {
      gnutls_free (kdata);
      gnutls_assert ();
      return GNUTLS_E_BASE64_DECODING_ERROR;
    }
  gnutls_free (kdata);

  return ret;
}

/**
  * gnutls_pem_base64_decode - This function will decode base64 encoded data
  * @header: A null terminated string with the PEM header (eg. CERTIFICATE)
  * @b64_data: contain the encoded data
  * @result: the place where decoded data will be copied
  * @result_size: holds the size of the result
  *
  * This function will decode the given encoded data. If the header given
  * is non null this function will search for "-----BEGIN header" and decode
  * only this part. Otherwise it will decode the first PEM packet found.
  *
  * Returns GNUTLS_E_SHORT_MEMORY_BUFFER if the buffer given is not long enough,
  * or 0 on success.
  **/
int
gnutls_pem_base64_decode (const char *header,
			  const gnutls_datum_t * b64_data,
			  unsigned char *result, size_t * result_size)
{
  opaque *ret;
  int size;

  size =
    _gnutls_fbase64_decode (header, b64_data->data, b64_data->size, &ret);
  if (size < 0)
    return size;

  if (result == NULL || *result_size < (unsigned) size)
    {
      gnutls_free (ret);
      *result_size = size;
      return GNUTLS_E_SHORT_MEMORY_BUFFER;
    }
  else
    {
      memcpy (result, ret, size);
      gnutls_free (ret);
      *result_size = size;
    }

  return 0;
}

/**
  * gnutls_pem_base64_decode_alloc - This function will decode base64 encoded data
  * @header: The PEM header (eg. CERTIFICATE)
  * @b64_data: contains the encoded data
  * @result: the place where decoded data lie
  *
  * This function will decode the given encoded data. The decoded data
  * will be allocated, and stored into result.
  * If the header given is non null this function will search for 
  * "-----BEGIN header" and decode only this part. Otherwise it will decode the 
  * first PEM packet found.
  *
  * You should use gnutls_free() to free the returned data.
  *
  **/
int
gnutls_pem_base64_decode_alloc (const char *header,
				const gnutls_datum_t * b64_data,
				gnutls_datum_t * result)
{
  opaque *ret;
  int size;

  if (result == NULL)
    return GNUTLS_E_INVALID_REQUEST;

  size =
    _gnutls_fbase64_decode (header, b64_data->data, b64_data->size, &ret);
  if (size < 0)
    return size;

  result->data = ret;
  result->size = size;
  return 0;
}
