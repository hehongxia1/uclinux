/*
 *  OpenVPN -- An application to securely tunnel IP networks
 *             over a single TCP/UDP port, with support for SSL/TLS-based
 *             session authentication and key exchange,
 *             packet encryption, packet authentication, and
 *             packet compression.
 *
 *  Copyright (C) 2002-2005 OpenVPN Solutions LLC <info@openvpn.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef WIN32
#include "config-win32.h"
#else
#include "config.h"
#endif

#include "syshead.h"

#if P2MP_SERVER

#include "multi.h"
#include "push.h"
#include "misc.h"
#include "otime.h"
#include "gremlin.h"

#include "memdbg.h"

#include "forward-inline.h"

/*#define MULTI_DEBUG_EVENT_LOOP*/

#ifdef MULTI_DEBUG_EVENT_LOOP
static const char *
id (struct multi_instance *mi)
{
  if (mi)
    return tls_common_name (mi->context.c2.tls_multi, false);
  else
    return "NULL";
}
#endif

static bool
learn_address_script (const struct multi_context *m,
		      const struct multi_instance *mi,
		      const char *op,
		      const struct mroute_addr *addr)
{
  struct gc_arena gc = gc_new ();
  struct env_set *es;
  bool ret = true;

  /* get environmental variable source */
  if (mi && mi->context.c2.es)
    es = mi->context.c2.es;
  else
    es = env_set_create (&gc);

  if (plugin_defined (m->top.c1.plugins, OPENVPN_PLUGIN_LEARN_ADDRESS))
    {
      struct buffer cmd = alloc_buf_gc (256, &gc);

      buf_printf (&cmd, "\"%s\" \"%s\"",
		  op,
		  mroute_addr_print (addr, &gc));
      if (mi)
	buf_printf (&cmd, " \"%s\"", tls_common_name (mi->context.c2.tls_multi, false));

      if (plugin_call (m->top.c1.plugins, OPENVPN_PLUGIN_LEARN_ADDRESS, BSTR (&cmd), es))
	{
	  msg (M_WARN, "WARNING: learn-address plugin call failed");
	  ret = false;
	}
    }

  if (m->top.options.learn_address_script)
    {
      struct buffer cmd = alloc_buf_gc (256, &gc);

      setenv_str (es, "script_type", "learn-address");

      buf_printf (&cmd, "%s \"%s\" \"%s\"",
		  m->top.options.learn_address_script,
		  op,
		  mroute_addr_print (addr, &gc));
      if (mi)
	buf_printf (&cmd, " \"%s\"", tls_common_name (mi->context.c2.tls_multi, false));

      if (!system_check (BSTR (&cmd), es, S_SCRIPT, "WARNING: learn-address command failed"))
	ret = false;
    }

  gc_free (&gc);
  return ret;
}

void
multi_ifconfig_pool_persist (struct multi_context *m, bool force)
{
 /* write pool data to file */
  if (m->ifconfig_pool
      && m->top.c1.ifconfig_pool_persist
      && (force || ifconfig_pool_write_trigger (m->top.c1.ifconfig_pool_persist)))
    {
      ifconfig_pool_write (m->top.c1.ifconfig_pool_persist, m->ifconfig_pool);
    }
}

static void
multi_reap_range (const struct multi_context *m,
		  int start_bucket,
		  int end_bucket)
{
  struct gc_arena gc = gc_new ();
  struct hash_iterator hi;
  struct hash_element *he;

  if (start_bucket < 0)
    {
      start_bucket = 0;
      end_bucket = hash_n_buckets (m->vhash);
    }

  dmsg (D_MULTI_DEBUG, "MULTI: REAP range %d -> %d", start_bucket, end_bucket);
  hash_iterator_init_range (m->vhash, &hi, true, start_bucket, end_bucket);
  while ((he = hash_iterator_next (&hi)) != NULL)
    {
      struct multi_route *r = (struct multi_route *) he->value;
      if (!multi_route_defined (m, r))
	{
	  dmsg (D_MULTI_DEBUG, "MULTI: REAP DEL %s",
	       mroute_addr_print (&r->addr, &gc));
	  learn_address_script (m, NULL, "delete", &r->addr);
	  multi_route_del (r);
	  hash_iterator_delete_element (&hi);
	}
    }
  hash_iterator_free (&hi);
  gc_free (&gc);
}

static void
multi_reap_all (const struct multi_context *m)
{
  multi_reap_range (m, -1, 0);
}

static struct multi_reap *
multi_reap_new (int buckets_per_pass)
{
  struct multi_reap *mr;
  ALLOC_OBJ (mr, struct multi_reap);
  mr->bucket_base = 0;
  mr->buckets_per_pass = buckets_per_pass;
  mr->last_call = now;
  return mr;
}

void
multi_reap_process_dowork (const struct multi_context *m)
{
  struct multi_reap *mr = m->reaper;
  if (mr->bucket_base >= hash_n_buckets (m->vhash))
    mr->bucket_base = 0;
  multi_reap_range (m, mr->bucket_base, mr->bucket_base + mr->buckets_per_pass); 
  mr->bucket_base += mr->buckets_per_pass;
  mr->last_call = now;
}

static void
multi_reap_free (struct multi_reap *mr)
{
  free (mr);
}

/*
 * How many buckets in vhash to reap per pass.
 */
static int
reap_buckets_per_pass (int n_buckets)
{
  return constrain_int (n_buckets / REAP_DIVISOR, REAP_MIN, REAP_MAX);
}

/*
 * Main initialization function, init multi_context object.
 */
void
multi_init (struct multi_context *m, struct context *t, bool tcp_mode, int thread_mode)
{
  int dev = DEV_TYPE_UNDEF;

  msg (D_MULTI_LOW, "MULTI: multi_init called, r=%d v=%d",
       t->options.real_hash_size,
       t->options.virtual_hash_size);

  /*
   * Get tun/tap/null device type
   */
  dev = dev_type_enum (t->options.dev, t->options.dev_type);

  /*
   * Init our multi_context object.
   */
  CLEAR (*m);
  
  m->thread_mode = thread_mode;

  /*
   * Real address hash table (source port number is
   * considered to be part of the address).  Used
   * to determine which client sent an incoming packet
   * which is seen on the TCP/UDP socket.
   */
  m->hash = hash_init (t->options.real_hash_size,
		       mroute_addr_hash_function,
		       mroute_addr_compare_function);

  /*
   * Virtual address hash table.  Used to determine
   * which client to route a packet to. 
   */
  m->vhash = hash_init (t->options.virtual_hash_size,
			mroute_addr_hash_function,
			mroute_addr_compare_function);

  /*
   * This hash table is a clone of m->hash but with a
   * bucket size of one so that it can be used
   * for fast iteration through the list.
   */
  m->iter = hash_init (1,
		       mroute_addr_hash_function,
		       mroute_addr_compare_function);

  /*
   * This is our scheduler, for time-based wakeup
   * events.
   */
  m->schedule = schedule_init ();

  /*
   * Limit frequency of incoming connections to control
   * DoS.
   */
  m->new_connection_limiter = frequency_limit_init (t->options.cf_max,
						    t->options.cf_per);

  /*
   * Allocate broadcast/multicast buffer list
   */
  m->mbuf = mbuf_init (t->options.n_bcast_buf);

  /*
   * Different status file format options are available
   */
  m->status_file_version = t->options.status_file_version;

  /*
   * Possibly allocate an ifconfig pool, do it
   * differently based on whether a tun or tap style
   * tunnel.
   */
  if (t->options.ifconfig_pool_defined)
    {
      if (dev == DEV_TYPE_TAP || t->options.ifconfig_pool_linear)
	{
	  m->ifconfig_pool = ifconfig_pool_init (IFCONFIG_POOL_INDIV,
						 t->options.ifconfig_pool_start,
						 t->options.ifconfig_pool_end,
						 t->options.duplicate_cn);
	}
      else if (dev == DEV_TYPE_TUN)
	{
	  m->ifconfig_pool = ifconfig_pool_init (IFCONFIG_POOL_30NET,
						 t->options.ifconfig_pool_start,
						 t->options.ifconfig_pool_end,
						 t->options.duplicate_cn);
	}
      else
	{
	  ASSERT (0);
	}

      /* reload pool data from file */
      if (t->c1.ifconfig_pool_persist)
	ifconfig_pool_read (t->c1.ifconfig_pool_persist, m->ifconfig_pool);
    }

  /*
   * Help us keep track of routing table.
   */
  m->route_helper = mroute_helper_init (MULTI_CACHE_ROUTE_TTL);

  /*
   * Initialize route and instance reaper.
   */
  m->reaper = multi_reap_new (reap_buckets_per_pass (t->options.virtual_hash_size));

  /*
   * Get local ifconfig address
   */
  CLEAR (m->local);
  ASSERT (t->c1.tuntap);
  mroute_extract_in_addr_t (&m->local, t->c1.tuntap->local);

  /*
   * Per-client limits
   */
  m->max_clients = t->options.max_clients;

  /*
   * Initialize multi-socket TCP I/O wait object
   */
  if (tcp_mode)
    m->mtcp = multi_tcp_init (t->options.max_clients, &m->max_clients);
  m->tcp_queue_limit = t->options.tcp_queue_limit;
  
  /*
   * Allow client <-> client communication, without going through
   * tun/tap interface and network stack?
   */
  m->enable_c2c = t->options.enable_c2c;
}

const char *
multi_instance_string (const struct multi_instance *mi, bool null, struct gc_arena *gc)
{
  if (mi)
    {
      struct buffer out = alloc_buf_gc (256, gc);
      const char *cn = tls_common_name (mi->context.c2.tls_multi, true);

      if (cn)
	buf_printf (&out, "%s/", cn);
      buf_printf (&out, "%s", mroute_addr_print (&mi->real, gc));
      return BSTR (&out);
    }
  else if (null)
    return NULL;
  else
    return "UNDEF";
}

void
generate_prefix (struct multi_instance *mi)
{
  mi->msg_prefix = multi_instance_string (mi, true, &mi->gc);
  set_prefix (mi);
}

void
ungenerate_prefix (struct multi_instance *mi)
{
  mi->msg_prefix = NULL;
  set_prefix (mi);
}

/*
 * Tell the route helper about deleted iroutes so
 * that it can update its mask of currently used
 * CIDR netlengths.
 */
static void
multi_del_iroutes (struct multi_context *m,
		   struct multi_instance *mi)
{
  const struct iroute *ir;
  if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TUN)
    {
      for (ir = mi->context.options.iroutes; ir != NULL; ir = ir->next)
	mroute_helper_del_iroute (m->route_helper, ir);
    }
}

static void
multi_client_disconnect_setenv (struct multi_context *m,
				struct multi_instance *mi)
{
  /* setenv client real IP address */
  setenv_trusted (mi->context.c2.es, get_link_socket_info (&mi->context));

  /* setenv stats */
  setenv_int (mi->context.c2.es, "bytes_received", mi->context.c2.link_read_bytes);
  setenv_int (mi->context.c2.es, "bytes_sent", mi->context.c2.link_write_bytes);

}

static void
multi_client_disconnect_script (struct multi_context *m,
				struct multi_instance *mi)
{
  if ((mi->context.c2.context_auth == CAS_SUCCEEDED && mi->connection_established_flag)
      || mi->context.c2.context_auth == CAS_PARTIAL)
    {
      multi_client_disconnect_setenv (m, mi);

      if (plugin_defined (m->top.c1.plugins, OPENVPN_PLUGIN_CLIENT_DISCONNECT))
	{
	  if (plugin_call (m->top.c1.plugins, OPENVPN_PLUGIN_CLIENT_DISCONNECT, NULL, mi->context.c2.es))
	    msg (M_WARN, "WARNING: client-disconnect plugin call failed");
	}

      if (mi->context.options.client_disconnect_script)
	{
	  struct gc_arena gc = gc_new ();
	  struct buffer cmd = alloc_buf_gc (256, &gc);

	  setenv_str (mi->context.c2.es, "script_type", "client-disconnect");
	  
	  buf_printf (&cmd, "%s", mi->context.options.client_disconnect_script);

	  system_check (BSTR (&cmd), mi->context.c2.es, S_SCRIPT, "client-disconnect command failed");
	  
	  gc_free (&gc);
	}
    }
}

void
multi_close_instance (struct multi_context *m,
		      struct multi_instance *mi,
		      bool shutdown)
{
  perf_push (PERF_MULTI_CLOSE_INSTANCE);

  ASSERT (!mi->halt);
  mi->halt = true;

  dmsg (D_MULTI_DEBUG, "MULTI: multi_close_instance called");

  /* prevent dangling pointers */
  if (m->pending == mi)
    multi_set_pending (m, NULL);
  if (m->earliest_wakeup == mi)
    m->earliest_wakeup = NULL;

  if (!shutdown)
    {
      if (mi->did_real_hash)
	{
	  ASSERT (hash_remove (m->hash, &mi->real));
	}
      if (mi->did_iter)
	{
	  ASSERT (hash_remove (m->iter, &mi->real));
	}

      schedule_remove_entry (m->schedule, (struct schedule_entry *) mi);

      ifconfig_pool_release (m->ifconfig_pool, mi->vaddr_handle, false);
      
      if (mi->did_iroutes)
        {
          multi_del_iroutes (m, mi);
          mi->did_iroutes = false;
        }

      if (m->mtcp)
	multi_tcp_dereference_instance (m->mtcp, mi);

      mbuf_dereference_instance (m->mbuf, mi);
    }

  multi_client_disconnect_script (m, mi);

  if (mi->did_open_context)
    close_context (&mi->context, SIGTERM, CC_GC_FREE);

  multi_tcp_instance_specific_free (mi);

  ungenerate_prefix (mi);

  /*
   * Don't actually delete the instance memory allocation yet,
   * because virtual routes may still point to it.  Let the
   * vhash reaper deal with it.
   */
  multi_instance_dec_refcount (mi);

  perf_pop ();
}

/*
 * Called on shutdown or restart.
 */
void
multi_uninit (struct multi_context *m)
{
  if (m->thread_mode & MC_WORK_THREAD)
    {
      multi_top_free (m);
      m->thread_mode = MC_UNDEF;
    }
  else if (m->thread_mode)
    {
      if (m->hash)
	{
	  struct hash_iterator hi;
	  struct hash_element *he;

	  hash_iterator_init (m->iter, &hi, true);
	  while ((he = hash_iterator_next (&hi)))
	    {
	      struct multi_instance *mi = (struct multi_instance *) he->value;
	      mi->did_iter = false;
	      multi_close_instance (m, mi, true);
	    }
	  hash_iterator_free (&hi);

	  multi_reap_all (m);

	  hash_free (m->hash);
	  hash_free (m->vhash);
	  hash_free (m->iter);
	  m->hash = NULL;

	  schedule_free (m->schedule);
	  mbuf_free (m->mbuf);
	  ifconfig_pool_free (m->ifconfig_pool);
	  frequency_limit_free (m->new_connection_limiter);
	  multi_reap_free (m->reaper);
	  mroute_helper_free (m->route_helper);
	  multi_tcp_free (m->mtcp);
	  m->thread_mode = MC_UNDEF;
	}
    }
}

/*
 * Create a client instance object for a newly connected client.
 */
struct multi_instance *
multi_create_instance (struct multi_context *m, const struct mroute_addr *real)
{
  struct gc_arena gc = gc_new ();
  struct multi_instance *mi;

  perf_push (PERF_MULTI_CREATE_INSTANCE);

  msg (D_MULTI_LOW, "MULTI: multi_create_instance called");

  ALLOC_OBJ_CLEAR (mi, struct multi_instance);

  mutex_init (&mi->mutex);
  mi->gc = gc_new ();
  multi_instance_inc_refcount (mi);
  mi->vaddr_handle = -1;
  mi->created = now;
  mroute_addr_init (&mi->real);

  if (real)
    {
      mi->real = *real;
      generate_prefix (mi);
    }

  mi->did_open_context = true;
  inherit_context_child (&mi->context, &m->top);
  if (IS_SIG (&mi->context))
    goto err;

  mi->context.c2.context_auth = CAS_PENDING;

  if (hash_n_elements (m->hash) >= m->max_clients)
    {
      msg (D_MULTI_ERRORS, "MULTI: new incoming connection would exceed maximum number of clients (%d)", m->max_clients);
      goto err;
    }

  if (!real) /* TCP mode? */
    {
      if (!multi_tcp_instance_specific_init (m, mi))
	goto err;
      generate_prefix (mi);
    }

  if (!hash_add (m->iter, &mi->real, mi, false))
    {
      msg (D_MULTI_LOW, "MULTI: unable to add real address [%s] to iterator hash table",
	   mroute_addr_print (&mi->real, &gc));
      goto err;
    }
  mi->did_iter = true;

  mi->context.c2.push_reply_deferred = true;

  if (!multi_process_post (m, mi, MPP_PRE_SELECT))
    {
      msg (D_MULTI_ERRORS, "MULTI: signal occurred during client instance initialization");
      goto err;
    }

  perf_pop ();
  gc_free (&gc);
  return mi;

 err:
  multi_close_instance (m, mi, false);
  perf_pop ();
  gc_free (&gc);
  return NULL;
}

/*
 * Dump tables -- triggered by SIGUSR2.
 * If status file is defined, write to file.
 * If status file is NULL, write to syslog.
 */
void
multi_print_status (struct multi_context *m, struct status_output *so, const int version)
{
  if (m->hash)
    {
      struct gc_arena gc_top = gc_new ();
      struct hash_iterator hi;
      const struct hash_element *he;

      status_reset (so);

      if (version == 1) /* WAS: m->status_file_version */
	{
	  /*
	   * Status file version 1
	   */
	  status_printf (so, PACKAGE_NAME " CLIENT LIST");
	  status_printf (so, "Updated,%s", time_string (0, 0, false, &gc_top));
	  status_printf (so, "Common Name,Real Address,Bytes Received,Bytes Sent,Connected Since");
	  hash_iterator_init (m->hash, &hi, true);
	  while ((he = hash_iterator_next (&hi)))
	    {
	      struct gc_arena gc = gc_new ();
	      const struct multi_instance *mi = (struct multi_instance *) he->value;

	      if (!mi->halt)
		{
		  status_printf (so, "%s,%s," counter_format "," counter_format ",%s",
				 tls_common_name (mi->context.c2.tls_multi, false),
				 mroute_addr_print (&mi->real, &gc),
				 mi->context.c2.link_read_bytes,
				 mi->context.c2.link_write_bytes,
				 time_string (mi->created, 0, false, &gc));
		}
	      gc_free (&gc);
	    }
	  hash_iterator_free (&hi);

	  status_printf (so, "ROUTING TABLE");
	  status_printf (so, "Virtual Address,Common Name,Real Address,Last Ref");
	  hash_iterator_init (m->vhash, &hi, true);
	  while ((he = hash_iterator_next (&hi)))
	    {
	      struct gc_arena gc = gc_new ();
	      const struct multi_route *route = (struct multi_route *) he->value;

	      if (multi_route_defined (m, route))
		{
		  const struct multi_instance *mi = route->instance;
		  const struct mroute_addr *ma = &route->addr;
		  char flags[2] = {0, 0};

		  if (route->flags & MULTI_ROUTE_CACHE)
		    flags[0] = 'C';
		  status_printf (so, "%s%s,%s,%s,%s",
				 mroute_addr_print (ma, &gc),
				 flags,
				 tls_common_name (mi->context.c2.tls_multi, false),
				 mroute_addr_print (&mi->real, &gc),
				 time_string (route->last_reference, 0, false, &gc));
		}
	      gc_free (&gc);
	    }
	  hash_iterator_free (&hi);

	  status_printf (so, "GLOBAL STATS");
	  if (m->mbuf)
	    status_printf (so, "Max bcast/mcast queue length,%d",
			   mbuf_maximum_queued (m->mbuf));

	  status_printf (so, "END");
	}
      else if (version == 2)
	{
	  /*
	   * Status file version 2
	   */
	  status_printf (so, "TITLE,%s", title_string);
	  status_printf (so, "TIME,%s,%u", time_string (now, 0, false, &gc_top), (unsigned int)now);
	  status_printf (so, "HEADER,CLIENT_LIST,Common Name,Real Address,Virtual Address,Bytes Received,Bytes Sent,Connected Since,Connected Since (time_t)");
	  hash_iterator_init (m->hash, &hi, true);
	  while ((he = hash_iterator_next (&hi)))
	    {
	      struct gc_arena gc = gc_new ();
	      const struct multi_instance *mi = (struct multi_instance *) he->value;

	      if (!mi->halt)
		{
		  status_printf (so, "CLIENT_LIST,%s,%s,%s," counter_format "," counter_format ",%s,%u",
				 tls_common_name (mi->context.c2.tls_multi, false),
				 mroute_addr_print (&mi->real, &gc),
				 print_in_addr_t (mi->reporting_addr, IA_EMPTY_IF_UNDEF, &gc),
				 mi->context.c2.link_read_bytes,
				 mi->context.c2.link_write_bytes,
				 time_string (mi->created, 0, false, &gc),
				 (unsigned int)mi->created);
		}
	      gc_free (&gc);
	    }
	  hash_iterator_free (&hi);

	  status_printf (so, "HEADER,ROUTING_TABLE,Virtual Address,Common Name,Real Address,Last Ref,Last Ref (time_t)");
	  hash_iterator_init (m->vhash, &hi, true);
	  while ((he = hash_iterator_next (&hi)))
	    {
	      struct gc_arena gc = gc_new ();
	      const struct multi_route *route = (struct multi_route *) he->value;

	      if (multi_route_defined (m, route))
		{
		  const struct multi_instance *mi = route->instance;
		  const struct mroute_addr *ma = &route->addr;
		  char flags[2] = {0, 0};

		  if (route->flags & MULTI_ROUTE_CACHE)
		    flags[0] = 'C';
		  status_printf (so, "ROUTING_TABLE,%s%s,%s,%s,%s,%u",
				 mroute_addr_print (ma, &gc),
				 flags,
				 tls_common_name (mi->context.c2.tls_multi, false),
				 mroute_addr_print (&mi->real, &gc),
				 time_string (route->last_reference, 0, false, &gc),
				 (unsigned int)route->last_reference);
		}
	      gc_free (&gc);
	    }
	  hash_iterator_free (&hi);

	  if (m->mbuf)
	    status_printf (so, "GLOBAL_STATS,Max bcast/mcast queue length,%d",
			   mbuf_maximum_queued (m->mbuf));

	  status_printf (so, "END");
	}
      else
	{
	  status_printf (so, "ERROR: bad status format version number");
	}
      status_flush (so);
      gc_free (&gc_top);
    }
}

/*
 * Learn a virtual address or route.
 * The learn will fail if the learn address
 * script/plugin fails.  In this case the
 * return value may be != mi.
 * Return the instance which owns this route,
 * or NULL if none.
 */
static struct multi_instance *
multi_learn_addr (struct multi_context *m,
		  struct multi_instance *mi,
		  const struct mroute_addr *addr,
		  const unsigned int flags)
{
  struct hash_element *he;
  const uint32_t hv = hash_value (m->vhash, addr);
  struct hash_bucket *bucket = hash_bucket (m->vhash, hv);
  struct multi_route *oldroute = NULL;
  struct multi_instance *owner = NULL;

  hash_bucket_lock (bucket);

  /* if route currently exists, get the instance which owns it */
  he = hash_lookup_fast (m->vhash, bucket, addr, hv);
  if (he)
    oldroute = (struct multi_route *) he->value;
  if (oldroute && multi_route_defined (m, oldroute))
    owner = oldroute->instance;

  /* do we need to add address to hash table? */
  if ((!owner || owner != mi)
      && mroute_learnable_address (addr)
      && !mroute_addr_equal (addr, &m->local))
    {
      struct gc_arena gc = gc_new ();
      struct multi_route *newroute;
      bool learn_succeeded = false;

      ALLOC_OBJ (newroute, struct multi_route);
      newroute->addr = *addr;
      newroute->instance = mi;
      newroute->flags = flags;
      newroute->last_reference = now;
      newroute->cache_generation = 0;

      /* The cache is invalidated when cache_generation is incremented */
      if (flags & MULTI_ROUTE_CACHE)
	newroute->cache_generation = m->route_helper->cache_generation;

      if (oldroute) /* route already exists? */
	{
	  if (route_quota_test (m, mi) && learn_address_script (m, mi, "update", &newroute->addr))
	    {
	      learn_succeeded = true;
	      owner = mi;
	      multi_instance_inc_refcount (mi);
	      route_quota_inc (mi);

	      /* delete old route */
	      multi_route_del (oldroute);

	      /* modify hash table entry, replacing old route */
	      he->key = &newroute->addr;
	      he->value = newroute;
	    }
	}
      else
	{
	  if (route_quota_test (m, mi) && learn_address_script (m, mi, "add", &newroute->addr))
	    {
	      learn_succeeded = true;
	      owner = mi;
	      multi_instance_inc_refcount (mi);
	      route_quota_inc (mi);

	      /* add new route */
	      hash_add_fast (m->vhash, bucket, &newroute->addr, hv, newroute);
	    }
	}
      
      msg (D_MULTI_LOW, "MULTI: Learn%s: %s -> %s",
	   learn_succeeded ? "" : " FAILED",
	   mroute_addr_print (&newroute->addr, &gc),
	   multi_instance_string (mi, false, &gc));

      if (!learn_succeeded)
	free (newroute);

      gc_free (&gc);
    }

  hash_bucket_unlock (bucket);
  return owner;
}

/*
 * Get client instance based on virtual address.
 */
static struct multi_instance *
multi_get_instance_by_virtual_addr (struct multi_context *m,
				    const struct mroute_addr *addr,
				    bool cidr_routing)
{
  struct multi_route *route;
  struct multi_instance *ret = NULL;

  /* check for local address */
  if (mroute_addr_equal (addr, &m->local))
    return NULL;

  route = (struct multi_route *) hash_lookup (m->vhash, addr);

  /* does host route (possible cached) exist? */
  if (route && multi_route_defined (m, route))
    {
      struct multi_instance *mi = route->instance;
      route->last_reference = now;
      ret = mi;
    }
  else if (cidr_routing) /* do we need to regenerate a host route cache entry? */
    {
      struct mroute_helper *rh = m->route_helper;
      struct mroute_addr tryaddr;
      int i;

      mroute_helper_lock (rh);

      /* cycle through each CIDR length */
      for (i = 0; i < rh->n_net_len; ++i)
	{
	  tryaddr = *addr;
	  tryaddr.type |= MR_WITH_NETBITS;
	  tryaddr.netbits = rh->net_len[i];
	  mroute_addr_mask_host_bits (&tryaddr);

	  /* look up a possible route with netbits netmask */
	  route = (struct multi_route *) hash_lookup (m->vhash, &tryaddr);

	  if (route && multi_route_defined (m, route))
	    {
	      /* found an applicable route, cache host route */
	      struct multi_instance *mi = route->instance;
	      multi_learn_addr (m, mi, addr, MULTI_ROUTE_CACHE|MULTI_ROUTE_AGEABLE);
	      ret = mi;
	      break;
	    }
	}
      
      mroute_helper_unlock (rh);
    }
  
#ifdef ENABLE_DEBUG
  if (check_debug_level (D_MULTI_DEBUG))
    {
      struct gc_arena gc = gc_new ();
      const char *addr_text = mroute_addr_print (addr, &gc);
      if (ret)
	{
	  dmsg (D_MULTI_DEBUG, "GET INST BY VIRT: %s -> %s via %s",
	       addr_text,
	       multi_instance_string (ret, false, &gc),
	       mroute_addr_print (&route->addr, &gc));
	}
      else
	{
	  dmsg (D_MULTI_DEBUG, "GET INST BY VIRT: %s [failed]",
	       addr_text);
	}
      gc_free (&gc);
    }
#endif

  ASSERT (!(ret && ret->halt));
  return ret;
}

/*
 * Helper function to multi_learn_addr().
 */
static struct multi_instance *
multi_learn_in_addr_t (struct multi_context *m,
		       struct multi_instance *mi,
		       in_addr_t a,
		       int netbits) /* -1 if host route, otherwise # of network bits in address */
{
  struct sockaddr_in remote_si;
  struct mroute_addr addr;

  CLEAR (remote_si);
  remote_si.sin_family = AF_INET;
  remote_si.sin_addr.s_addr = htonl (a);
  ASSERT (mroute_extract_sockaddr_in (&addr, &remote_si, false));

  if (netbits >= 0)
    {
      addr.type |= MR_WITH_NETBITS;
      addr.netbits = (uint8_t) netbits;
    }
  return multi_learn_addr (m, mi, &addr, 0);
}

/*
 * A new client has connected, add routes (server -> client)
 * to internal routing table.
 */
static void
multi_add_iroutes (struct multi_context *m,
		   struct multi_instance *mi)
{
  struct gc_arena gc = gc_new ();
  const struct iroute *ir;
  if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TUN)
    {
      mi->did_iroutes = true;
      for (ir = mi->context.options.iroutes; ir != NULL; ir = ir->next)
	{
	  if (ir->netbits >= 0)
	    msg (D_MULTI_LOW, "MULTI: internal route %s/%d -> %s",
		 print_in_addr_t (ir->network, 0, &gc),
		 ir->netbits,
		 multi_instance_string (mi, false, &gc));
	  else
	    msg (D_MULTI_LOW, "MULTI: internal route %s -> %s",
		 print_in_addr_t (ir->network, 0, &gc),
		 multi_instance_string (mi, false, &gc));

	  mroute_helper_add_iroute (m->route_helper, ir);
      
	  multi_learn_in_addr_t (m, mi, ir->network, ir->netbits);
	}
    }
  gc_free (&gc);
}

/*
 * Given an instance (new_mi), delete all other instances which use the
 * same common name.
 */
static void
multi_delete_dup (struct multi_context *m, struct multi_instance *new_mi)
{
  if (new_mi)
    {
      const char *new_cn = tls_common_name (new_mi->context.c2.tls_multi, true);
      if (new_cn)
	{
	  struct hash_iterator hi;
	  struct hash_element *he;
	  int count = 0;

	  hash_iterator_init (m->iter, &hi, true);
	  while ((he = hash_iterator_next (&hi)))
	    {
	      struct multi_instance *mi = (struct multi_instance *) he->value;
	      if (mi != new_mi && !mi->halt)
		{
		  const char *cn = tls_common_name (mi->context.c2.tls_multi, true);
		  if (cn && !strcmp (cn, new_cn))
		    {
		      mi->did_iter = false;
		      multi_close_instance (m, mi, false);
		      hash_iterator_delete_element (&hi);
		      ++count;
		    }
		}
	    }
	  hash_iterator_free (&hi);

	  if (count)
	    msg (D_MULTI_LOW, "MULTI: new connection by client '%s' will cause previous active sessions by this client to be dropped.  Remember to use the --duplicate-cn option if you want multiple clients using the same certificate or username to concurrently connect.", new_cn);
	}
    }
}

/*
 * Select a virtual address for a new client instance.
 * Use an --ifconfig-push directive, if given (static IP).
 * Otherwise use an --ifconfig-pool address (dynamic IP). 
 */
static void
multi_select_virtual_addr (struct multi_context *m, struct multi_instance *mi)
{
  struct gc_arena gc = gc_new ();

  /*
   * If ifconfig addresses were set by dynamic config file,
   * release pool addresses, otherwise keep them.
   */
  if (mi->context.options.push_ifconfig_defined)
    {
      /* ifconfig addresses were set statically,
	 release dynamic allocation */
      if (mi->vaddr_handle >= 0)
	{
	  ifconfig_pool_release (m->ifconfig_pool, mi->vaddr_handle, true);
	  mi->vaddr_handle = -1;
	}

      mi->context.c2.push_ifconfig_defined = true;
      mi->context.c2.push_ifconfig_local = mi->context.options.push_ifconfig_local;
      mi->context.c2.push_ifconfig_remote_netmask = mi->context.options.push_ifconfig_remote_netmask;
    }
  else if (m->ifconfig_pool && mi->vaddr_handle < 0) /* otherwise, choose a pool address */
    {
      in_addr_t local=0, remote=0;
      const char *cn = NULL;

      if (!mi->context.options.duplicate_cn)
	cn = tls_common_name (mi->context.c2.tls_multi, true);

      mi->vaddr_handle = ifconfig_pool_acquire (m->ifconfig_pool, &local, &remote, cn);
      if (mi->vaddr_handle >= 0)
	{
	  /* use pool ifconfig address(es) */
	  mi->context.c2.push_ifconfig_local = remote;
	  if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TUN)
	    {
	      if (mi->context.options.ifconfig_pool_linear)		    
		mi->context.c2.push_ifconfig_remote_netmask = mi->context.c1.tuntap->local;
	      else
		mi->context.c2.push_ifconfig_remote_netmask = local;
	      mi->context.c2.push_ifconfig_defined = true;
	    }
	  else if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TAP)
	    {
	      mi->context.c2.push_ifconfig_remote_netmask = mi->context.options.ifconfig_pool_netmask;
	      if (!mi->context.c2.push_ifconfig_remote_netmask)
		mi->context.c2.push_ifconfig_remote_netmask = mi->context.c1.tuntap->remote_netmask;
	      if (mi->context.c2.push_ifconfig_remote_netmask)
		mi->context.c2.push_ifconfig_defined = true;
	      else
		msg (D_MULTI_ERRORS, "MULTI: no --ifconfig-pool netmask parameter is available to push to %s",
		     multi_instance_string (mi, false, &gc));
	    }
	}
      else
	{
	  msg (D_MULTI_ERRORS, "MULTI: no free --ifconfig-pool addresses are available");
	}
    }
  gc_free (&gc);
}

/*
 * Set virtual address environmental variables.
 */
static void
multi_set_virtual_addr_env (struct multi_context *m, struct multi_instance *mi)
{
  setenv_del (mi->context.c2.es, "ifconfig_pool_local_ip");
  setenv_del (mi->context.c2.es, "ifconfig_pool_remote_ip");
  setenv_del (mi->context.c2.es, "ifconfig_pool_netmask");

  if (mi->context.c2.push_ifconfig_defined)
    {
      setenv_in_addr_t (mi->context.c2.es,
			"ifconfig_pool_remote_ip",
			mi->context.c2.push_ifconfig_local,
			SA_SET_IF_NONZERO);

      if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TUN)
	{
	  setenv_in_addr_t (mi->context.c2.es,
			    "ifconfig_pool_local_ip",
			    mi->context.c2.push_ifconfig_remote_netmask,
			    SA_SET_IF_NONZERO);
	}
      else if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TAP)
	{
	  setenv_in_addr_t (mi->context.c2.es,
			    "ifconfig_pool_netmask",
			    mi->context.c2.push_ifconfig_remote_netmask,
			    SA_SET_IF_NONZERO);
	}
    }
}

/*
 * Called after client-connect script or plug-in is called
 */
static void
multi_client_connect_post (struct multi_context *m,
			   struct multi_instance *mi,
			   const char *dc_file,
			   unsigned int option_permissions_mask,
			   unsigned int *option_types_found)
{
  /* Did script generate a dynamic config file? */
  if (test_file (dc_file))
    {
      options_server_import (&mi->context.options,
			     dc_file,
			     D_IMPORT_ERRORS|M_OPTERR,
			     option_permissions_mask,
			     option_types_found,
			     mi->context.c2.es);

      if (!delete_file (dc_file))
	msg (D_MULTI_ERRORS, "MULTI: problem deleting temporary file: %s",
	     dc_file);

      /*
       * If the --client-connect script generates a config file
       * with an --ifconfig-push directive, it will override any
       * --ifconfig-push directive from the --client-config-dir
       * directory or any --ifconfig-pool dynamic address.
       */
      multi_select_virtual_addr (m, mi);
      multi_set_virtual_addr_env (m, mi);
    }
}

/*
 * Called as soon as the SSL/TLS connection authenticates.
 *
 * Instance-specific directives to be processed:
 *
 *   iroute start-ip end-ip
 *   ifconfig-push local remote-netmask
 *   push
 */
static void
multi_connection_established (struct multi_context *m, struct multi_instance *mi)
{
  if (tls_authenticated (mi->context.c2.tls_multi))
    {
      struct gc_arena gc = gc_new ();
      unsigned int option_types_found = 0;
      const unsigned int option_permissions_mask = OPT_P_INSTANCE|OPT_P_INHERIT|OPT_P_PUSH|OPT_P_TIMER|OPT_P_CONFIG|OPT_P_ECHO;
      int cc_succeeded = true; /* client connect script status */
      int cc_succeeded_count = 0;

      ASSERT (mi->context.c1.tuntap);

      /* lock down the common name so it can't change during future TLS renegotiations */
      tls_lock_common_name (mi->context.c2.tls_multi);

      /* generate a msg() prefix for this client instance */
      generate_prefix (mi);

      /* delete instances of previous clients with same common-name */
      if (!mi->context.options.duplicate_cn)
	multi_delete_dup (m, mi);

      /* reset pool handle to null */
      mi->vaddr_handle = -1;

      /*
       * Try to source a dynamic config file from the
       * --client-config-dir directory.
       */
      if (mi->context.options.client_config_dir)
	{
	  const char *ccd_file;
	  
	  ccd_file = gen_path (mi->context.options.client_config_dir,
			       tls_common_name (mi->context.c2.tls_multi, false),
			       &gc);

	  /* try common-name file */
	  if (test_file (ccd_file))
	    {
	      options_server_import (&mi->context.options,
				     ccd_file,
				     D_IMPORT_ERRORS|M_OPTERR,
				     option_permissions_mask,
				     &option_types_found,
				     mi->context.c2.es);
	    }
	  else /* try default file */
	    {
	      ccd_file = gen_path (mi->context.options.client_config_dir,
				   CCD_DEFAULT,
				   &gc);

	      if (test_file (ccd_file))
		{
		  options_server_import (&mi->context.options,
					 ccd_file,
					 D_IMPORT_ERRORS|M_OPTERR,
					 option_permissions_mask,
					 &option_types_found,
					 mi->context.c2.es);
		}
	    }
	}

      /*
       * Select a virtual address from either --ifconfig-push in --client-config-dir file
       * or --ifconfig-pool.
       */
      multi_select_virtual_addr (m, mi);

      /* setenv incoming cert common name for script */
      setenv_str (mi->context.c2.es, "common_name", tls_common_name (mi->context.c2.tls_multi, true));

      /* setenv client real IP address */
      setenv_trusted (mi->context.c2.es, get_link_socket_info (&mi->context));

      /* setenv client virtual IP address */
      multi_set_virtual_addr_env (m, mi);

      /*
       * Call client-connect plug-in.
       */
      if (plugin_defined (m->top.c1.plugins, OPENVPN_PLUGIN_CLIENT_CONNECT))
	{
	  const char *dc_file = create_temp_filename (mi->context.options.tmp_dir, &gc);

	  delete_file (dc_file);

	  if (plugin_call (m->top.c1.plugins, OPENVPN_PLUGIN_CLIENT_CONNECT, dc_file, mi->context.c2.es))
	    {
	      msg (M_WARN, "WARNING: client-connect plugin call failed");
	      cc_succeeded = false;
	    }
	  else
	    {
	      multi_client_connect_post (m, mi, dc_file, option_permissions_mask, &option_types_found);
	      ++cc_succeeded_count;
	    }
	}

      /*
       * Run --client-connect script.
       */
      if (mi->context.options.client_connect_script && cc_succeeded)
	{
	  struct buffer cmd = alloc_buf_gc (256, &gc);
	  const char *dc_file = NULL;

	  setenv_str (mi->context.c2.es, "script_type", "client-connect");

	  dc_file = create_temp_filename (mi->context.options.tmp_dir, &gc);

	  delete_file (dc_file);

	  buf_printf (&cmd, "%s %s",
		      mi->context.options.client_connect_script,
		      dc_file);

	  if (system_check (BSTR (&cmd), mi->context.c2.es, S_SCRIPT, "client-connect command failed"))
	    {
	      multi_client_connect_post (m, mi, dc_file, option_permissions_mask, &option_types_found);
	      ++cc_succeeded_count;
	    }
	  else
	    cc_succeeded = false;
	}

      /*
       * Check for "disable" directive in client-config-dir file
       * or config file generated by --client-connect script.
       */
      if (mi->context.options.disable)
	{
	  msg (D_MULTI_ERRORS, "MULTI: client has been rejected due to 'disable' directive");
	  cc_succeeded = false;
	}

      if (cc_succeeded)
	{
	  /*
	   * Process sourced options.
	   */
	  do_deferred_options (&mi->context, option_types_found);

	  /*
	   * make sure we got ifconfig settings from somewhere
	   */
	  if (!mi->context.c2.push_ifconfig_defined)
	    {
	      msg (D_MULTI_ERRORS, "MULTI: no dynamic or static remote --ifconfig address is available for %s",
		   multi_instance_string (mi, false, &gc));
	    }

	  /*
	   * For routed tunnels, set up internal route to endpoint
	   * plus add all iroute routes.
	   */
	  if (TUNNEL_TYPE (mi->context.c1.tuntap) == DEV_TYPE_TUN)
	    {
	      if (mi->context.c2.push_ifconfig_defined)
		{
		  multi_learn_in_addr_t (m, mi, mi->context.c2.push_ifconfig_local, -1);
		  msg (D_MULTI_LOW, "MULTI: primary virtual IP for %s: %s",
		       multi_instance_string (mi, false, &gc),
		       print_in_addr_t (mi->context.c2.push_ifconfig_local, 0, &gc));
		}

	      /* add routes locally, pointing to new client, if
		 --iroute options have been specified */
	      multi_add_iroutes (m, mi);

	      /*
	       * iroutes represent subnets which are "owned" by a particular
	       * client.  Therefore, do not actually push a route to a client
	       * if it matches one of the client's iroutes.
	       */
	      remove_iroutes_from_push_route_list (&mi->context.options);
	    }
	  else if (mi->context.options.iroutes)
	    {
	      msg (D_MULTI_ERRORS, "MULTI: --iroute options rejected for %s -- iroute only works with tun-style tunnels",
		   multi_instance_string (mi, false, &gc));
	    }

	  /* set our client's VPN endpoint for status reporting purposes */
	  mi->reporting_addr = mi->context.c2.push_ifconfig_local;

	  /* set context-level authentication flag */
	  mi->context.c2.context_auth = CAS_SUCCEEDED;
	}
      else
	{
	  /* set context-level authentication flag */
	  mi->context.c2.context_auth = cc_succeeded_count ? CAS_PARTIAL : CAS_FAILED;
	}

      /* set flag so we don't get called again */
      mi->connection_established_flag = true;

      gc_free (&gc);
    }

  /*
   * Reply now to client's PUSH_REQUEST query
   */
  mi->context.c2.push_reply_deferred = false;
}

/*
 * Add a mbuf buffer to a particular
 * instance.
 */
void
multi_add_mbuf (struct multi_context *m,
		struct multi_instance *mi,
		struct mbuf_buffer *mb)
{
  if (multi_output_queue_ready (m, mi))
    {
      struct mbuf_item item;
      item.buffer = mb;
      item.instance = mi;
      mbuf_add_item (m->mbuf, &item);
    }
  else
    {
      msg (D_MULTI_DROPPED, "MULTI: packet dropped due to output saturation (multi_add_mbuf)");
    }
}

/*
 * Add a packet to a client instance output queue.
 */
static inline void
multi_unicast (struct multi_context *m,
	       const struct buffer *buf,
	       struct multi_instance *mi)
{
  struct mbuf_buffer *mb;

  if (BLEN (buf) > 0)
    {
      mb = mbuf_alloc_buf (buf);
      mb->flags = MF_UNICAST;
      multi_add_mbuf (m, mi, mb);
      mbuf_free_buf (mb);
    }
}

/*
 * Broadcast a packet to all clients.
 */
void
multi_bcast (struct multi_context *m,
	     const struct buffer *buf,
	     struct multi_instance *omit)
{
  struct hash_iterator hi;
  struct hash_element *he;
  struct multi_instance *mi;
  struct mbuf_buffer *mb;

  if (BLEN (buf) > 0)
    {
      perf_push (PERF_MULTI_BCAST);
#ifdef MULTI_DEBUG_EVENT_LOOP
      printf ("BCAST len=%d\n", BLEN (buf));
#endif
      mb = mbuf_alloc_buf (buf);
      hash_iterator_init (m->iter, &hi, true);

      while ((he = hash_iterator_next (&hi)))
	{
	  mi = (struct multi_instance *) he->value;
	  if (mi != omit && !mi->halt)
	    multi_add_mbuf (m, mi, mb);
	}

      hash_iterator_free (&hi);
      mbuf_free_buf (mb);
      perf_pop ();
    }
}

/*
 * Given a time delta, indicating that we wish to be
 * awoken by the scheduler at time now + delta, figure
 * a sigma parameter (in microseconds) that represents
 * a sort of fuzz factor around delta, so that we're
 * really telling the scheduler to wake us up any time
 * between now + delta - sigma and now + delta + sigma.
 *
 * The sigma parameter helps the scheduler to run more efficiently.
 * Sigma should be no larger than TV_WITHIN_SIGMA_MAX_USEC
 */
static inline unsigned int
compute_wakeup_sigma (const struct timeval *delta)
{
  if (delta->tv_sec < 1)
    {
      /* if < 1 sec, fuzz = # of microseconds / 8 */
      return delta->tv_usec >> 3;
    }
  else
    {
      /* if < 10 minutes, fuzz = 13.1% of timeout */
      if (delta->tv_sec < 600)
	return delta->tv_sec << 17;
      else
	return 120000000; /* if >= 10 minutes, fuzz = 2 minutes */
    }
}

/*
 * Figure instance-specific timers, convert
 * earliest to absolute time in mi->wakeup,
 * call scheduler with our future wakeup time.
 *
 * Also close context on signal.
 */
bool
multi_process_post (struct multi_context *m, struct multi_instance *mi, const unsigned int flags)
{
  bool ret = true;

  if (!IS_SIG (&mi->context) && ((flags & MPP_PRE_SELECT) || ((flags & MPP_CONDITIONAL_PRE_SELECT) && !ANY_OUT (&mi->context))))
    {
      /* figure timeouts and fetch possible outgoing
	 to_link packets (such as ping or TLS control) */
      pre_select (&mi->context);

      if (!IS_SIG (&mi->context))
	{
	  /* calculate an absolute wakeup time */
	  ASSERT (!gettimeofday (&mi->wakeup, NULL));
	  tv_add (&mi->wakeup, &mi->context.c2.timeval);

	  /* tell scheduler to wake us up at some point in the future */
	  schedule_add_entry (m->schedule,
			      (struct schedule_entry *) mi,
			      &mi->wakeup,
			      compute_wakeup_sigma (&mi->context.c2.timeval));

	  /* connection is "established" when SSL/TLS key negotiation succeeds
	     and (if specified) auth user/pass succeeds */
	  if (!mi->connection_established_flag && CONNECTION_ESTABLISHED (&mi->context))
	    multi_connection_established (m, mi);
	}
    }

  if (IS_SIG (&mi->context))
    {
      if (flags & MPP_CLOSE_ON_SIGNAL)
	{
	  multi_close_instance_on_signal (m, mi);
	  ret = false;
	}
    }
  else
    {
      /* continue to pend on output? */
      multi_set_pending (m, ANY_OUT (&mi->context) ? mi : NULL);

#ifdef MULTI_DEBUG_EVENT_LOOP
      printf ("POST %s[%d] to=%d lo=%d/%d w=%d/%d\n",
	      id(mi),
	      (int) (mi == m->pending),
	      mi ? mi->context.c2.to_tun.len : -1,
	      mi ? mi->context.c2.to_link.len : -1,
	      (mi && mi->context.c2.fragment) ? mi->context.c2.fragment->outgoing.len : -1,
	      (int)mi->context.c2.timeval.tv_sec,
	      (int)mi->context.c2.timeval.tv_usec);
#endif
    }

  if ((flags & MPP_RECORD_TOUCH) && m->mpp_touched)
    *m->mpp_touched = mi;

  return ret;
}

/*
 * Process packets in the TCP/UDP socket -> TUN/TAP interface direction,
 * i.e. client -> server direction.
 */
bool
multi_process_incoming_link (struct multi_context *m, struct multi_instance *instance, const unsigned int mpp_flags)
{
  struct gc_arena gc = gc_new ();

  struct context *c;
  struct mroute_addr src, dest;
  unsigned int mroute_flags;
  struct multi_instance *mi;
  bool ret = true;

  if (m->pending)
    return true;

  if (!instance)
    {
#ifdef MULTI_DEBUG_EVENT_LOOP
      printf ("TCP/UDP -> TUN [%d]\n", BLEN (&m->top.c2.buf));
#endif
      multi_set_pending (m, multi_get_create_instance_udp (m));
    }
  else
    multi_set_pending (m, instance);

  if (m->pending)
    {
      set_prefix (m->pending);

      /* get instance context */
      c = &m->pending->context;

      if (!instance)
	{
	  /* transfer packet pointer from top-level context buffer to instance */
	  c->c2.buf = m->top.c2.buf;

	  /* transfer from-addr from top-level context buffer to instance */
	  c->c2.from = m->top.c2.from;
	}

      if (BLEN (&c->c2.buf) > 0)
	{
	  /* decrypt in instance context */
	  process_incoming_link (c);

	  if (TUNNEL_TYPE (m->top.c1.tuntap) == DEV_TYPE_TUN)
	    {
	      /* extract packet source and dest addresses */
	      mroute_flags = mroute_extract_addr_from_packet (&src,
							      &dest,
							      &c->c2.to_tun,
							      DEV_TYPE_TUN);

	      /* drop packet if extract failed */
	      if (!(mroute_flags & MROUTE_EXTRACT_SUCCEEDED))
		{
		  c->c2.to_tun.len = 0;
		}
	      /* make sure that source address is associated with this client */
	      else if (multi_get_instance_by_virtual_addr (m, &src, true) != m->pending)
		{
		  msg (D_MULTI_DROPPED, "MULTI: bad source address from client [%s], packet dropped",
		       mroute_addr_print (&src, &gc));
		  c->c2.to_tun.len = 0;
		}
	      /* client-to-client communication enabled? */
	      else if (m->enable_c2c)
		{
		  /* multicast? */
		  if (mroute_flags & MROUTE_EXTRACT_MCAST)
		    {
		      /* for now, treat multicast as broadcast */
		      multi_bcast (m, &c->c2.to_tun, m->pending);
		    }
		  else /* possible client to client routing */
		    {
		      ASSERT (!(mroute_flags & MROUTE_EXTRACT_BCAST));
		      mi = multi_get_instance_by_virtual_addr (m, &dest, true);
		  
		      /* if dest addr is a known client, route to it */
		      if (mi)
			{
			  multi_unicast (m, &c->c2.to_tun, mi);
			  register_activity (c);
			  c->c2.to_tun.len = 0;
			}
		    }
		}
	    }
	  else if (TUNNEL_TYPE (m->top.c1.tuntap) == DEV_TYPE_TAP)
	    {
	      /* extract packet source and dest addresses */
	      mroute_flags = mroute_extract_addr_from_packet (&src,
							      &dest,
							      &c->c2.to_tun,
							      DEV_TYPE_TAP);

	      if (mroute_flags & MROUTE_EXTRACT_SUCCEEDED)
		{
		  if (multi_learn_addr (m, m->pending, &src, 0) == m->pending)
		    {
		      /* check for broadcast */
		      if (m->enable_c2c)
			{
			  if (mroute_flags & (MROUTE_EXTRACT_BCAST|MROUTE_EXTRACT_MCAST))
			    {
			      multi_bcast (m, &c->c2.to_tun, m->pending);
			    }
			  else /* try client-to-client routing */
			    {
			      mi = multi_get_instance_by_virtual_addr (m, &dest, false);

			      /* if dest addr is a known client, route to it */
			      if (mi)
				{
				  multi_unicast (m, &c->c2.to_tun, mi);
				  register_activity (c);
				  c->c2.to_tun.len = 0;
				}
			    }
			}
		    }
		  else
		    {
		      msg (D_MULTI_DROPPED, "MULTI: bad source address from client [%s], packet dropped",
			   mroute_addr_print (&src, &gc));
		      c->c2.to_tun.len = 0;
		    }
		}
	      else
		{
		  c->c2.to_tun.len = 0;
		}
	    }
	}

      /* postprocess and set wakeup */
      ret = multi_process_post (m, m->pending, mpp_flags);

      clear_prefix ();
    }

  gc_free (&gc);
  return ret;
}

/*
 * Process packets in the TUN/TAP interface -> TCP/UDP socket direction,
 * i.e. server -> client direction.
 */
bool
multi_process_incoming_tun (struct multi_context *m, const unsigned int mpp_flags)
{
  struct gc_arena gc = gc_new ();
  bool ret = true;

  if (BLEN (&m->top.c2.buf) > 0)
    {
      unsigned int mroute_flags;
      struct mroute_addr src, dest;
      const int dev_type = TUNNEL_TYPE (m->top.c1.tuntap);

#ifdef MULTI_DEBUG_EVENT_LOOP
      printf ("TUN -> TCP/UDP [%d]\n", BLEN (&m->top.c2.buf));
#endif

      if (m->pending)
	return true;

      /* 
       * Route an incoming tun/tap packet to
       * the appropriate multi_instance object.
       */

      mroute_flags = mroute_extract_addr_from_packet (&src,
						      &dest,
						      &m->top.c2.buf,
						      dev_type);

      if (mroute_flags & MROUTE_EXTRACT_SUCCEEDED)
	{
	  struct context *c;

	  /* broadcast or multicast dest addr? */
	  if (mroute_flags & (MROUTE_EXTRACT_BCAST|MROUTE_EXTRACT_MCAST))
	    {
	      /* for now, treat multicast as broadcast */
	      multi_bcast (m, &m->top.c2.buf, NULL);
	    }
	  else
	    {
	      multi_set_pending (m, multi_get_instance_by_virtual_addr (m, &dest, dev_type == DEV_TYPE_TUN));

	      if (m->pending)
		{
		  /* get instance context */
		  c = &m->pending->context;
		  
		  set_prefix (m->pending);

		  if (multi_output_queue_ready (m, m->pending))
		    {
		      /* transfer packet pointer from top-level context buffer to instance */
		      c->c2.buf = m->top.c2.buf;
		    }
		  else
		    {
		      /* drop packet */
		      msg (D_MULTI_DROPPED, "MULTI: packet dropped due to output saturation (multi_process_incoming_tun)");
		      buf_clear (&c->c2.buf);
		    }
	      
		  /* encrypt in instance context */
		  process_incoming_tun (c);

		  /* postprocess and set wakeup */
		  ret = multi_process_post (m, m->pending, mpp_flags);

		  clear_prefix ();
		}
	    }
	}
    }
  gc_free (&gc);
  return ret;
}

/*
 * Process a possible client-to-client/bcast/mcast message in the
 * queue.
 */
struct multi_instance *
multi_get_queue (struct mbuf_set *ms)
{
  struct mbuf_item item;

  if (mbuf_extract_item (ms, &item, true)) /* cleartext IP packet */
    {
      unsigned int pipv4_flags = PIPV4_PASSTOS;

      set_prefix (item.instance);
      item.instance->context.c2.buf = item.buffer->buf;
      if (item.buffer->flags & MF_UNICAST) /* --mssfix doesn't make sense for broadcast or multicast */
	pipv4_flags |= PIPV4_MSSFIX;
      process_ipv4_header (&item.instance->context, pipv4_flags, &item.instance->context.c2.buf);
      encrypt_sign (&item.instance->context, true);
      mbuf_free_buf (item.buffer);

      dmsg (D_MULTI_DEBUG, "MULTI: C2C/MCAST/BCAST");

      clear_prefix ();
      return item.instance;
    }
  else
    {
      return NULL;
    }
}

/*
 * Called when an I/O wait times out.  Usually means that a particular
 * client instance object needs timer-based service.
 */
bool
multi_process_timeout (struct multi_context *m, const unsigned int mpp_flags)
{
  bool ret = true;

#ifdef MULTI_DEBUG_EVENT_LOOP
  printf ("%s -> TIMEOUT\n", id(m->earliest_wakeup));
#endif

  /* instance marked for wakeup? */
  if (m->earliest_wakeup)
    {
      set_prefix (m->earliest_wakeup);
      ret = multi_process_post (m, m->earliest_wakeup, mpp_flags);
      m->earliest_wakeup = NULL;
      clear_prefix ();
    }
  return ret;
}

/*
 * Drop a TUN/TAP outgoing packet..
 */
void
multi_process_drop_outgoing_tun (struct multi_context *m, const unsigned int mpp_flags)
{
  struct multi_instance *mi = m->pending;

  ASSERT (mi);

  set_prefix (mi);

  msg (D_MULTI_ERRORS, "MULTI: Outgoing TUN queue full, dropped packet len=%d",
       mi->context.c2.to_tun.len);

  buf_reset (&mi->context.c2.to_tun);

  multi_process_post (m, mi, mpp_flags);
  clear_prefix ();
}

/*
 * Per-client route quota management
 */

void
route_quota_exceeded (const struct multi_context *m, const struct multi_instance *mi)
{
  struct gc_arena gc = gc_new ();
  msg (D_ROUTE_QUOTA, "MULTI ROUTE: route quota (%d) exceeded for %s (see --max-routes-per-client option)",
	mi->context.options.max_routes_per_client,
	multi_instance_string (mi, false, &gc));
  gc_free (&gc);
}

#ifdef ENABLE_DEBUG
/*
 * Flood clients with random packets
 */
static void
gremlin_flood_clients (struct multi_context *m)
{
  const int level = GREMLIN_PACKET_FLOOD_LEVEL (m->top.options.gremlin);
  if (level)
    {
      struct gc_arena gc = gc_new ();
      struct buffer buf = alloc_buf_gc (BUF_SIZE (&m->top.c2.frame), &gc);
      struct packet_flood_parms parm = get_packet_flood_parms (level);
      int i;

      ASSERT (buf_init (&buf, FRAME_HEADROOM (&m->top.c2.frame)));
      parm.packet_size = min_int (parm.packet_size, MAX_RW_SIZE_TUN (&m->top.c2.frame));

      msg (D_GREMLIN, "GREMLIN_FLOOD_CLIENTS: flooding clients with %d packets of size %d",
	   parm.n_packets,
	   parm.packet_size);

      for (i = 0; i < parm.packet_size; ++i)
	ASSERT (buf_write_u8 (&buf, get_random () & 0xFF));

      for (i = 0; i < parm.n_packets; ++i)
	multi_bcast (m, &buf, NULL);

      gc_free (&gc);
    }
}
#endif

/*
 * Process timers in the top-level context
 */
void
multi_process_per_second_timers_dowork (struct multi_context *m)
{
  /* possibly reap instances/routes in vhash */
  multi_reap_process (m);

  /* possibly print to status log */
  if (m->top.c1.status_output)
    {
      if (status_trigger (m->top.c1.status_output))
	multi_print_status (m, m->top.c1.status_output, m->status_file_version);
    }

  /* possibly flush ifconfig-pool file */
  multi_ifconfig_pool_persist (m, false);

#ifdef ENABLE_DEBUG
  gremlin_flood_clients (m);
#endif
}

void
multi_top_init (struct multi_context *m, const struct context *top, const bool alloc_buffers)
{
  inherit_context_top (&m->top, top);
  m->top.c2.buffers = NULL;
  if (alloc_buffers)
    m->top.c2.buffers = init_context_buffers (&top->c2.frame);
}

void
multi_top_free (struct multi_context *m)
{
  close_context (&m->top, -1, CC_GC_FREE);
  free_context_buffers (m->top.c2.buffers);
}

/*
 * Return true if event loop should break,
 * false if it should continue.
 */
bool
multi_process_signal (struct multi_context *m)
{
  if (m->top.sig->signal_received == SIGUSR2)
    {
      struct status_output *so = status_open (NULL, 0, M_INFO, NULL, 0);
      multi_print_status (m, so, m->status_file_version);
      status_close (so);
      m->top.sig->signal_received = 0;
      return false;
    }
  return true;
}

/*
 * Called when an instance should be closed due to the
 * reception of a soft signal.
 */
void
multi_close_instance_on_signal (struct multi_context *m, struct multi_instance *mi)
{
  remap_signal (&mi->context);
  set_prefix (mi);
  print_signal (mi->context.sig, "client-instance", D_MULTI_LOW);
  clear_prefix ();
  multi_close_instance (m, mi, false);
}

static void
multi_signal_instance (struct multi_context *m, struct multi_instance *mi, const int sig)
{
  mi->context.sig->signal_received = sig;
  multi_close_instance_on_signal (m, mi);
}

/*
 * Management subsystem callbacks
 */

#ifdef ENABLE_MANAGEMENT

static void
management_callback_status (void *arg, const int version, struct status_output *so)
{
  struct multi_context *m = (struct multi_context *) arg;

  if (!version)
    multi_print_status (m, so, m->status_file_version);
  else
    multi_print_status (m, so, version);
}

static int
management_callback_kill_by_cn (void *arg, const char *del_cn)
{
  struct multi_context *m = (struct multi_context *) arg;
  struct hash_iterator hi;
  struct hash_element *he;
  int count = 0;

  hash_iterator_init (m->iter, &hi, true);
  while ((he = hash_iterator_next (&hi)))
    {
      struct multi_instance *mi = (struct multi_instance *) he->value;
      if (!mi->halt)
	{
	  const char *cn = tls_common_name (mi->context.c2.tls_multi, false);
	  if (cn && !strcmp (cn, del_cn))
	    {
	      multi_signal_instance (m, mi, SIGTERM);
	      ++count;
	    }
	}
    }
  hash_iterator_free (&hi);
  return count;
}

static int
management_callback_kill_by_addr (void *arg, const in_addr_t addr, const int port)
{
  struct multi_context *m = (struct multi_context *) arg;
  struct hash_iterator hi;
  struct hash_element *he;
  struct sockaddr_in saddr;
  struct mroute_addr maddr;
  int count = 0;

  CLEAR (saddr);
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = htonl (addr);
  saddr.sin_port = htons (port);
  if (mroute_extract_sockaddr_in (&maddr, &saddr, true))
    {
      hash_iterator_init (m->iter, &hi, true);
      while ((he = hash_iterator_next (&hi)))
	{
	  struct multi_instance *mi = (struct multi_instance *) he->value;
	  if (!mi->halt && mroute_addr_equal (&maddr, &mi->real))
	    {
	      multi_signal_instance (m, mi, SIGTERM);
	      ++count;
	    }
	}
      hash_iterator_free (&hi);
    }
  return count;
}

static void
management_delete_event (void *arg, event_t event)
{
  struct multi_context *m = (struct multi_context *) arg;
  if (m->mtcp)
    multi_tcp_delete_event (m->mtcp, event);
}

#endif

void
init_management_callback_multi (struct multi_context *m)
{
#ifdef ENABLE_MANAGEMENT
  if (management)
    {
      struct management_callback cb;
      CLEAR (cb);
      cb.arg = m;
      cb.status = management_callback_status;
      cb.show_net = management_show_net_callback;
      cb.kill_by_cn = management_callback_kill_by_cn;
      cb.kill_by_addr = management_callback_kill_by_addr;
      cb.delete_event = management_delete_event;
      management_set_callback (management, &cb);
    }
#endif
}

void
uninit_management_callback_multi (struct multi_context *m)
{
  uninit_management_callback ();
}

/*
 * Top level event loop.
 */
void
tunnel_server (struct context *top)
{
  ASSERT (top->options.mode == MODE_SERVER);

  switch (top->options.proto) {
  case PROTO_UDPv4:
    tunnel_server_udp (top);
    break;
  case PROTO_TCPv4_SERVER:
    tunnel_server_tcp (top);
    break;
  default:
    ASSERT (0);
  }
}

#else
static void dummy(void) {}
#endif /* P2MP_SERVER */
