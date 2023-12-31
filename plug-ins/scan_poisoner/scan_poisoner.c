/*
    scan_poisoner -- ettercap plugin -- Actively search other poisoners

    It checks the hosts list, searching for eqaul mac addresses. 
    It also sends icmp packets to see if any ip-mac association has
    changed.
    
    Copyright (C) ALoR & NaGA
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/


#include <ec.h>                        /* required for global variables */
#include <ec_plugins.h>                /* required for plugin ops */
#include <ec_packet.h>
#include <ec_hook.h>
#include <ec_send.h>
#include <ec_sleep.h>

/* globals */
char flag_strange;

/* mutexes */
static pthread_mutex_t scan_poisoner_mutex = PTHREAD_MUTEX_INITIALIZER;

/* protos */
int plugin_load(void *);
static int scan_poisoner_init(void *);
static int scan_poisoner_fini(void *);
static int scan_poisoner_unload(void *);
static EC_THREAD_FUNC(scan_poisoner_thread);
static void parse_icmp(struct packet_object *po);

/* plugin operations */

struct plugin_ops scan_poisoner_ops = { 
   /* ettercap version MUST be the global EC_VERSION */
   .ettercap_version =  EC_VERSION,                        
   /* the name of the plugin */
   .name =              "scan_poisoner",  
    /* a short description of the plugin (max 50 chars) */                    
   .info =              "Actively search other poisoners",  
   /* the plugin version. */ 
   .version =           "1.0",   
   /* activation function */
   .init =              &scan_poisoner_init,
   /* deactivation function */                     
   .fini =              &scan_poisoner_fini,
   /* clean-up function */
   .unload =            &scan_poisoner_unload,
};

/**********************************************************/

/* this function is called on plugin load */
int plugin_load(void *handle) 
{
   return plugin_register(handle, &scan_poisoner_ops);
}

/******************* STANDARD FUNCTIONS *******************/

static int scan_poisoner_init(void *dummy) 
{
   /* variable not used */
   (void) dummy;

   ec_thread_new("scan_poisoner", "plugin scan_poisoner", 
         &scan_poisoner_thread, NULL);

   return PLUGIN_RUNNING;
}

static int scan_poisoner_fini(void *dummy)
{
   /* variable not used */
   (void) dummy;

   pthread_t pid;

   pid = ec_thread_getpid("scan_poisoner");

   if (!pthread_equal(pid, ec_thread_getpid(NULL)))
         ec_thread_destroy(pid);

   INSTANT_USER_MSG("scan_poisoner: plugin terminated...\n");

   return PLUGIN_FINISHED;
}

static int scan_poisoner_unload(void *dummy)
{
   /* variable not used */
   (void) dummy;

   return PLUGIN_UNLOADED;
}

static EC_THREAD_FUNC(scan_poisoner_thread)
{
   /* variable not used */
   (void) EC_THREAD_PARAM;

   
   char tmp1[MAX_ASCII_ADDR_LEN];
   char tmp2[MAX_ASCII_ADDR_LEN];
   struct hosts_list *h1, *h2;
   
   ec_thread_init();
   PLUGIN_LOCK(scan_poisoner_mutex);

   /* don't show packets while operating */
   EC_GBL_OPTIONS->quiet = 1;
      
   if (LIST_EMPTY(&EC_GBL_HOSTLIST)) {
      INSTANT_USER_MSG("scan_poisoner: You have to build host-list to run this plugin.\n\n"); 
      PLUGIN_UNLOCK(scan_poisoner_mutex);
      plugin_kill_thread("scan_poisoner", "scan_poisoner");
      return PLUGIN_FINISHED;
   }

   INSTANT_USER_MSG("scan_poisoner: Checking hosts list...\n");
   flag_strange = 0;

   /* Compares mac address of each host with any other */   
   LIST_FOREACH(h1, &EC_GBL_HOSTLIST, next) 
      for(h2=LIST_NEXT(h1,next); h2!=LIST_END(&EC_GBL_HOSTLIST); h2=LIST_NEXT(h2,next)) 
         if (!memcmp(h1->mac, h2->mac, MEDIA_ADDR_LEN)) {
            flag_strange = 1;
            INSTANT_USER_MSG("scan_poisoner: - %s and %s have same MAC address\n", ip_addr_ntoa(&h1->ip, tmp1), ip_addr_ntoa(&h2->ip, tmp2));
         }

   if (!flag_strange)
      INSTANT_USER_MSG("scan_poisoner: - Nothing strange\n");
   flag_strange=0;

   /* Can't continue in unoffensive */
   if (EC_GBL_OPTIONS->unoffensive || EC_GBL_OPTIONS->read) {
      INSTANT_USER_MSG("\nscan_poisoner: Can't make active test in UNOFFENSIVE mode.\n\n");
      PLUGIN_UNLOCK(scan_poisoner_mutex);
      plugin_kill_thread("scan_poisoner", "scan_poisoner");
      return PLUGIN_FINISHED;
   }

   INSTANT_USER_MSG("\nscan_poisoner: Actively searching poisoners...\n");
   
   /* Add the hook to collect ICMP replies from the victim */
   hook_add(HOOK_PACKET_ICMP, &parse_icmp);

   /* Send ICMP echo request to each target */
   LIST_FOREACH(h1, &EC_GBL_HOSTLIST, next) {
      send_L3_icmp_echo(&EC_GBL_IFACE->ip, &h1->ip);   
      ec_usleep(MILLI2MICRO(EC_GBL_CONF->arp_storm_delay));
   }
         
   /* wait for the response */
   ec_usleep(SEC2MICRO(1));

   /* remove the hook */
   hook_del(HOOK_PACKET_ICMP, &parse_icmp);

   /* We don't need mutex on it :) */
   if (!flag_strange)
      INSTANT_USER_MSG("scan_poisoner: - Nothing strange\n");
     
   PLUGIN_UNLOCK(scan_poisoner_mutex);
   plugin_kill_thread("scan_poisoner", "scan_poisoner");
   return PLUGIN_FINISHED;
}


/*********************************************************/

/* Check icmp replies */
static void parse_icmp(struct packet_object *po)
{
   struct hosts_list *h1, *h2;
   char poisoner[MAX_ASCII_ADDR_LEN];
   char tmp[MAX_ASCII_ADDR_LEN];

   /* If the poisoner is not in the hosts list */
   sprintf(poisoner, "UNKNOWN");
   
   /* Check if the reply contains the correct ip/mac association */
   LIST_FOREACH(h1, &EC_GBL_HOSTLIST, next) {
      if (!ip_addr_cmp(&(po->L3.src), &h1->ip) && memcmp(po->L2.src, h1->mac, MEDIA_ADDR_LEN)) {
         flag_strange = 1;
         /* Check if the mac address of the poisoner is in the hosts list */
         LIST_FOREACH(h2, &EC_GBL_HOSTLIST, next) 
            if (!memcmp(po->L2.src, h2->mac, MEDIA_ADDR_LEN))
               ip_addr_ntoa(&h2->ip, poisoner);
		
         INSTANT_USER_MSG("scan_poisoner: - %s is replying for %s\n", poisoner, ip_addr_ntoa(&h1->ip, tmp)); 
      }
   }	  
}


/* EOF */

// vim:ts=3:expandtab
 
