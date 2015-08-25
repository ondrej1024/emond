/******************************************************************************
 * 
 * WebApi client
 * 
 * Description:
 *   This module connects to a HTTP API and sends its data to it. 
 *   It uses libcurl for the HTTP request handling.
 *   (needs libcurl4-gnutls-dev installed on the build system)
 * 
 * Last modified:
 *   24/08/2015
 *
 *****************************************************************************/ 
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>

#include "webapi.h"

/* Uncomment this to enable debug mode */
//#define DEBUG

/* EmonCMS API definitions */
#define EMONCMS_API_BASE_URI "http://emoncms.org"
#define EMONCMS_API_INPUT_URI "input/post.json"
#define EMONCMS_API_RESPONSE_OK "ok"
#define EMONCMS_NODE_NUMBER 1

#define API_TIMEOUT 20

#ifdef DEBUG
#define _debug(x, args...)  syslog(LOG_DAEMON | LOG_DEBUG, "" x, ##args)
#else
#define _debug(x, args...)
#endif

static unsigned int ready_to_send=1;


/**********************************************************
 * Internal Function: curl_writefunc()
 * 
 * Description:
 *           Callback function which copies the data received
 *           as response to the WebAPI call to the user buffer
 * 
 * Returns:  number of bytes received
 *********************************************************/
static size_t curl_writefunc(void *buffer, size_t size, size_t nmemb, void *userp)
{
   //_debug("reveived %d data items of size %d\n",  (int)nmemb, (int)size );
   memcpy(userp, buffer, nmemb*size);
   *((char*)userp+(nmemb*size)) = 0;
   return nmemb*size;
}


/**********************************************************
 * Internal function: emoncms_send_thread()
 * 
 * Description:
 *           Perform the sending of data to the EmonCMS
 *           WebAPI as a seperate thread
 * 
 * Returns:  None
 *********************************************************/
static void *emoncms_send_thread(void* arg)
{
   char urlbuf[128];
   char params[128];
   char json[64];
   char response[1024];
   CURL* ch;
   time_t start, now, delay;
   int  rc;
   
   emon_data_t* data = (emon_data_t*)arg;
   
   
   start = time(NULL);

   /* API key parameter check */
   if (data->api_key == NULL)
   {
      /* Cannot continue without API key */
      syslog(LOG_DAEMON | LOG_WARNING, "Cannot perform Web API request: API key missing");
   }
   else
   {   
      /* Base URI parameter check */
      if (data->api_base_uri == NULL)
      {
         /* Use default URI */
         data->api_base_uri = (char *)EMONCMS_API_BASE_URI;
      }
      
      /* Node number parameter check */
      if (data->node_number == 0)
      {
         /* Use default node number */  
         data->node_number = EMONCMS_NODE_NUMBER;
      }
      
      /* Init libcurl */
      curl_global_init(CURL_GLOBAL_NOTHING);
      ch = curl_easy_init();
      
      /* Define parameters for WebAPI request to EmonCMS */
      snprintf(urlbuf, sizeof(urlbuf), "%s/%s", data->api_base_uri, EMONCMS_API_INPUT_URI);
      snprintf(params, sizeof(params), "?apikey=%s&node=%d&json=",
               data->api_key, data->node_number);
      
      /* Build json data from input */
      sprintf(json, "{");
      strcat(params, json);
      if (data->inst_power) 
      {
         snprintf(json, sizeof(json), "power:%u,", data->inst_power);
         strcat(params, json);
      }
      if (data->energy_day) 
      {
         snprintf(json, sizeof(json), "energy_day:%u,", data->energy_day);
         strcat(params, json);
      }      
      if (data->energy_month) 
      {
         snprintf(json, sizeof(json), "energy_month:%u,", data->energy_month);
         strcat(params, json);
      }
      sprintf(json, "}");
      params[strlen(params)-1] = 0; // delete trailing ','
      strcat(params, json);
      strcat(urlbuf, params);
      _debug("Sending request: %s", urlbuf);
      
      /* Pass needed paramters to Curl */
      curl_easy_setopt(ch, CURLOPT_URL, urlbuf);
      curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_writefunc);
      curl_easy_setopt(ch, CURLOPT_WRITEDATA, &response);
      curl_easy_setopt(ch, CURLOPT_TIMEOUT, API_TIMEOUT);
      //curl_easy_setopt ( ch, CURLOPT_VERBOSE, debug );
      
      response[0] = 0;
      
      /* Send WebAPI request */
      rc = curl_easy_perform(ch);
      
      if (rc == 0)
      {
         if (strlen(response))
         {   
            _debug("Received response (%d chars): %s", (int)strlen(response), response);
            if (!strcmp(response, EMONCMS_API_RESPONSE_OK))
            {
               /* Data was successfully sent, clear struct */
               data->inst_power = 0;
               data->energy_day = 0;
               data->energy_month = 0;
            }
            else
            {
               syslog(LOG_DAEMON | LOG_WARNING, "Unexpected response to Web API request");
            }
         }
         else
         {
            syslog(LOG_DAEMON | LOG_WARNING, "Empty response to Web API request");
         }
      }
      else
      {
         syslog(LOG_DAEMON | LOG_WARNING, "Error performing Web API request: return code=%d", rc);
      }
      
      /* Cleanup */
      curl_easy_cleanup(ch);
      curl_global_cleanup();
   }
   
   /* Wait before allowing new API request */
   now = time(NULL);
   if (data->api_update_rate > (now-start))
   {
      delay = data->api_update_rate - (now-start);
      sleep(delay);
   }
   
   /* Allow new API request */
   ready_to_send = 1;
   
   return NULL;
}


/**********************************************************
 * Public function: emoncms_send()
 * 
 * Description:
 *           Start sending of data to the EmonCMS
 *           WebAPI 
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int emoncms_send(emon_data_t* data)
{
   pthread_t tid;
   
   if (ready_to_send)
   {
      ready_to_send = 0;
      if (pthread_create(&tid, NULL, &emoncms_send_thread, (void*)data) != 0)
      {
         _debug("Unable to perform Web API request: pthread_create failed");
         return -1;  
      }
   }
   else
   {
      _debug("Not ready to send Web API request");
      return -2;  
   }
   
   return 0;
}
