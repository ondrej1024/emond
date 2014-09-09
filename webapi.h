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
 *   05/06/2014
 *
 *****************************************************************************/ 

#ifndef __WEBAPI_H__
#define __WEBAPI_H__

/* 
 * Struct holding the necessary data to perform the 
 * WebAPI request to EmonCMS 
 */
typedef struct
{
   unsigned int inst_power;
   unsigned int energy_day;
   unsigned int energy_month;
   const char*  api_key;
   unsigned int node_number;
} emon_data_t;

 
/**********************************************************
 * Function: emoncms_send()
 * 
 * Description:
 *           Perform the sending of data to the EmonCMS
 *           WebAPI 
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int emoncms_send(emon_data_t* data);

#endif /* __WEBAPI_H__ */