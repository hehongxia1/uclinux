/* Copyright 2007 Free Software Foundation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

extern void print_x509_certificate_info (gnutls_session_t);

/* This function will print some details of the
 * given session.
 */
int
print_info (gnutls_session_t session)
{
  const char *tmp;
  gnutls_credentials_type_t cred;
  gnutls_kx_algorithm_t kx;

  /* print the key exchange's algorithm name
   */
  kx = gnutls_kx_get (session);
  tmp = gnutls_kx_get_name (kx);
  printf ("- Key Exchange: %s\n", tmp);

  /* Check the authentication type used and switch
   * to the appropriate.
   */
  cred = gnutls_auth_get_type (session);
  switch (cred)
    {
    case GNUTLS_CRD_IA:
      printf ("- TLS/IA session\n");
      break;


    case GNUTLS_CRD_SRP:
      printf ("- SRP session with username %s\n",
	      gnutls_srp_server_get_username (session));
      break;

    case GNUTLS_CRD_PSK:
      if (gnutls_psk_server_get_username (session) != NULL)
	printf ("- PSK authentication. Connected as '%s'\n",
		gnutls_psk_server_get_username (session));
      break;

    case GNUTLS_CRD_ANON:	/* anonymous authentication */

      printf ("- Anonymous DH using prime of %d bits\n",
	      gnutls_dh_get_prime_bits (session));
      break;

    case GNUTLS_CRD_CERTIFICATE:	/* certificate authentication */

      /* Check if we have been using ephemeral Diffie Hellman.
       */
      if (kx == GNUTLS_KX_DHE_RSA || kx == GNUTLS_KX_DHE_DSS)
	{
	  printf ("\n- Ephemeral DH using prime of %d bits\n",
		  gnutls_dh_get_prime_bits (session));
	}

      /* if the certificate list is available, then
       * print some information about it.
       */
      print_x509_certificate_info (session);

    }				/* switch */

  /* print the protocol's name (ie TLS 1.0) 
   */
  tmp = gnutls_protocol_get_name (gnutls_protocol_get_version (session));
  printf ("- Protocol: %s\n", tmp);

  /* print the certificate type of the peer.
   * ie X.509
   */
  tmp =
    gnutls_certificate_type_get_name (gnutls_certificate_type_get (session));

  printf ("- Certificate Type: %s\n", tmp);

  /* print the compression algorithm (if any)
   */
  tmp = gnutls_compression_get_name (gnutls_compression_get (session));
  printf ("- Compression: %s\n", tmp);

  /* print the name of the cipher used.
   * ie 3DES.
   */
  tmp = gnutls_cipher_get_name (gnutls_cipher_get (session));
  printf ("- Cipher: %s\n", tmp);

  /* Print the MAC algorithms name.
   * ie SHA1
   */
  tmp = gnutls_mac_get_name (gnutls_mac_get (session));
  printf ("- MAC: %s\n", tmp);

  return 0;
}
