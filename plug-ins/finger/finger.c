/*
    finger -- ettercap plugin -- fingerprint a remote host.

    it sends a syn to an open port and collect the passive ACK fingerprint.

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
#include <ec_fingerprint.h>
#include <ec_packet.h>
#include <ec_hook.h>
#include <ec_socket.h>
#include <ec_sleep.h>

#include <stdlib.h>
#include <string.h>

/* globals */

static struct ip_addr ip;
static u_int16 port;
static char fingerprint[FINGER_LEN + 1];

/* protos */
int plugin_load(void *);
static int finger_init(void *);
static int finger_fini(void *);
static int finger_unload(void *);

static void get_finger(struct packet_object *po);
static int good_target(struct ip_addr *ip, u_int16 *port);
static int get_user_target(struct ip_addr *ip, u_int16 *port);
static void do_fingerprint(void);

/* plugin operations */

struct plugin_ops finger_ops = { 
   /* ettercap version MUST be the global EC_VERSION */
   .ettercap_version =  EC_VERSION,                        
   /* the name of the plugin */
   .name =              "finger",  
    /* a short description of the plugin (max 50 chars) */                    
   .info =              "Fingerprint a remote host",  
   /* the plugin version. */ 
   .version =           "1.6",   
   /* activation function */
   .init =              &finger_init,
   /* deactivation function */                     
   .fini =              &finger_fini,
   /* clean-up function */
   .unload =            &finger_unload,
};

/**********************************************************/

/* this function is called on plugin load */
int plugin_load(void *handle) 
{
   return plugin_register(handle, &finger_ops);
}

/******************* STANDARD FUNCTIONS *******************/

static int finger_init(void *dummy) 
{
   /* variable not used */
   (void) dummy;

   /* don't show packets while operating */
   EC_GBL_OPTIONS->quiet = 1;
   
   /* wipe the global vars */
   memset(&ip, 0, sizeof(struct ip_addr));
   port = 0;

   /* 
    * can we use EC_GBL_TARGETS ?
    * else ask the user
    */
   if (good_target(&ip, &port) != E_SUCCESS) {
      /* get the target from user */
      if (get_user_target(&ip, &port) == E_SUCCESS) {
         /* do the actual finterprinting */
         do_fingerprint();   
      }
   } else {
      struct ip_list *host;
   
      /* look over all the hosts in the TARGET */ 
      LIST_FOREACH(host, &EC_GBL_TARGET1->ips, next) {
         /* 
          * copy the ip address 
          * the port was alread retrived by good_target()
          */
         memcpy(&ip, &host->ip, sizeof(struct ip_addr));

         /* cicle thru all the specified port */
         for (port = 0; port < 0xffff; port++) {
            if (BIT_TEST(EC_GBL_TARGET1->ports, port)) {
               /* do the actual finterprinting */
               do_fingerprint();
            }
         }
      }
      
   }
   
   return PLUGIN_FINISHED;
}


static int finger_fini(void *dummy) 
{
   /* variable not used */
   (void) dummy;

   return PLUGIN_FINISHED;
}

static int finger_unload(void *dummy)
{
   /* variable not used */
   (void) dummy;

   return PLUGIN_UNLOADED;
}

/*********************************************************/

/*
 * sends a SYN to a specified port and collect the 
 * passive fingerprint for that host 
 */
static void get_finger(struct packet_object *po)
{
  
   /* check that the source is our host and the fingerprint was collecter */
   if (!ip_addr_cmp(&ip, &po->L3.src) && strcmp(po->PASSIVE.fingerprint, "")) 
      memcpy(fingerprint, &po->PASSIVE.fingerprint, FINGER_LEN);
}

/*
 * check if we can use EC_GBL_TARGETS
 */
static int good_target(struct ip_addr *p_ip, u_int16 *p_port)
{
   struct ip_list *host;
   
   /* is it possible to get it from EC_GBL_TARGETS ? */
   if ((host = LIST_FIRST(&EC_GBL_TARGET1->ips)) != NULL) {
      
      /* copy the ip address */
      memcpy(p_ip, &host->ip, sizeof(struct ip_addr));
      
      /* find the port */
      for (*p_port = 0; *p_port < 0xffff; (*p_port)++) {
         if (BIT_TEST(EC_GBL_TARGET1->ports, *p_port)) {
            break;
         }
      }
      
      /* port was found */
      if (*p_port != 0xffff)
         return E_SUCCESS;
   }
   
   return -E_NOTFOUND;
}


/* 
 * get the target from user input 
 */
static int get_user_target(struct ip_addr *p_ip, u_int16 *p_port)
{
   char input[MAX_ASCII_ADDR_LEN+1+5+1];
   char ipstr[MAX_ASCII_ADDR_LEN];

   memset(input, 0, sizeof(input));
   
   /* get the user input */
   ui_input("Insert ip:port : ", input, sizeof(input), NULL);

   /* no input was entered */
   if (strlen(input) == 0)
      return -E_INVALID;
   
   /* get the hostname */
   if (ec_strsplit_ipport(input, ipstr, p_port))
      return -E_INVALID;

   /* convert IP string into ip_addr struct */
   if (ip_addr_pton(ipstr, p_ip))
      return -E_INVALID;

   /* correct parsing */
   if (*p_port != 0)
      return E_SUCCESS;

   return -E_INVALID;
}


/*
 * fingerprint the host
 */
static void do_fingerprint(void)
{
   char tmp[MAX_ASCII_ADDR_LEN];
   char os[OS_LEN + 1];
   int fd;
   
   /* clear the buffer */
   memset(fingerprint, 0, sizeof(fingerprint));
   
   /* convert the in ascii ip address */
   ip_addr_ntoa(&ip, tmp);

   /* 
    * add the hook to collect tcp SYN+ACK packets from 
    * the target and extract the passive fingerprint
    */
   hook_add(HOOK_PACKET_TCP, &get_finger);
   
   INSTANT_USER_MSG("Fingerprinting %s:%d...\n", tmp, port);
   
   /* 
    * open the connection and close it immediately.
    * this ensure that a SYN will be sent to the port
    */
   if ((fd = open_socket(tmp, port)) < 0)
      return;
   
   /* close the socket */
   close_socket(fd);

   /* wait for the response */
   ec_usleep(SEC2MICRO(1));

   /* remove the hook, we have collected the finger */
   hook_del(HOOK_PACKET_TCP, &get_finger);

   /* no fingerprint collected */
   if (!strcmp(fingerprint, ""))
      return;
   
   INSTANT_USER_MSG("\n FINGERPRINT      : %s\n", fingerprint);

   /* decode the finterprint */
   if (fingerprint_search(fingerprint, os) == E_SUCCESS)
      INSTANT_USER_MSG(" OPERATING SYSTEM : %s \n\n", os);
   else {
      INSTANT_USER_MSG(" OPERATING SYSTEM : unknown fingerprint (please submit it) \n");
      INSTANT_USER_MSG(" NEAREST ONE IS   : %s \n\n", os);
   }  
}


/* EOF */

// vim:ts=3:expandtab
 
