/*
* config.h	No longer automatically generated configuration includefile
*
* NET-TOOLS	A collection of programs that form the base set of the
*		NET-3 Networking Distribution for the LINUX operating
*		system.
*
*		DO  NOT  EDIT  DIRECTLY
*
*/

/* 
 * 
 * Internationalization
 * 
 * The net-tools package has currently been translated to French,
 * German and Brazilian Portugese.  Other translations are, of
 * course, welcome.  Answer `n' here if you have no support for
 * internationalization on your system.
 * 
 */
#include <config/autoconf.h>

#define I18N 0

/* 
 * 
 * Protocol Families.
 * 
 */
#define HAVE_AFUNIX 1
#define HAVE_AFINET 1
#ifdef CONFIG_IPV6
#define HAVE_AFINET6 1
#else
#define HAVE_AFINET6 0
#endif
#define HAVE_AFIPX 0
#define HAVE_AFATALK 0
#define HAVE_AFAX25 0
#define HAVE_AFNETROM 0
#define HAVE_AFROSE 0
#define HAVE_AFX25 0
#define HAVE_AFECONET 0
#define HAVE_AFDECnet 0
#define HAVE_AFASH 0

/* 
 * 
 * Device Hardware types.
 * 
 */
#define HAVE_HWETHER 1
#define HAVE_HWARC 0
#define HAVE_HWSLIP 0
#define HAVE_HWPPP 1
#define HAVE_HWTUNNEL 0
#define HAVE_HWSTRIP 0
#define HAVE_HWTR 0
#define HAVE_HWAX25 0
#define HAVE_HWROSE 0
#define HAVE_HWNETROM 0
#define HAVE_HWX25 0
#define HAVE_HWFR 0
#ifdef CONFIG_USER_IPV6
#define HAVE_HWSIT 1
#else
#define HAVE_HWSIT 0
#endif
#define HAVE_HWFDDI 0
#define HAVE_HWHIPPI 0
#define HAVE_HWASH 0
#define HAVE_HWHDLCLAPB 0
#define HAVE_HWIRDA 0
#define HAVE_HWEC 0

/* 
 * 
 * Other Features.
 * 
 */
#define HAVE_FW_MASQUERADE 1
#define HAVE_IP_TOOLS 1
#define HAVE_MII 1
