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
 *   09/06/2014
 *
 *****************************************************************************/ 
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

#include "webapi.h"


/* EmonCMS API definitions */
#define EMONCMS_API_BASE_URI "http://emoncms.org"
#define EMONCMS_API_INPUT_URI EMONCMS_API_BASE_URI"/input/post.json"
#define EMONCMS_API_RESPONSE_OK "ok"


#define DEBUG

#ifdef DEBUG
#define _debug(x, args...)  printf("DBG: " x, ##args)
#else
#define _debug(x, args...)
#endif


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
 * Public function: emoncms_send()
 * 
 * Description:
 *           Perform the sending of data to the EmonCMS
 *           WebAPI 
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int emoncms_send(emon_data_t* data)
{
   char urlbuf[128];
   char params[128];
   char json[64];
   char response[1024];
   CURL* ch;
   int  rc;
   
   if (strlen(data->api_key) == 0)
   {
      return 0;  
   }
   
   /* Init libcurl */
   curl_global_init(CURL_GLOBAL_NOTHING);
   ch = curl_easy_init();
   
   /* Define parameters for WebAPI request to EmonCMS */
   snprintf(urlbuf, sizeof(urlbuf), "%s", EMONCMS_API_INPUT_URI);
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
   _debug("sending request: %s\n", urlbuf);
   
   /* Pass needed paramters to Curl */
   curl_easy_setopt(ch, CURLOPT_URL, urlbuf);
   curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_writefunc);
   curl_easy_setopt(ch, CURLOPT_WRITEDATA, &response);
   //curl_easy_setopt ( ch, CURLOPT_VERBOSE, debug );
      
   response[0] = 0;
   
   /* Send WebAPI request */
   rc = curl_easy_perform(ch);
   
   if (rc == 0)
   {
      if (strlen(response))
      {   
         _debug("received response (%d chars): %s\n", (int)strlen(response), response);
         if (!strcmp(response, EMONCMS_API_RESPONSE_OK))
         {
            /* Data was successfully sent, clear struct */
            data->inst_power = 0;
            data->energy_day = 0;
            data->energy_month = 0;
         }
         else
         {
            _debug("unexpected response (something went wrong)\n");
         }
      }
      else
      {
         _debug("empty response (something went wrong)\n");
      }
   }
   else
   {
      _debug("Error during HTTP request: return code=%d\n", rc);
   }
   
   /* Cleanup */
   curl_easy_cleanup(ch);
   curl_global_cleanup();

   return rc;
}
