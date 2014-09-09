/* 
 * Energy Monitor: build-in lcdproc client
 * 
 * Author: Ondrej Wisniewski
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>

#include "sockets.h"


static int sock = -1;

/**********************************************************
 * Public function: lcd_init()
 * 
 * Description:
 *           Perform the initialization and configuration
 *           of the LCD display
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int lcd_init(void)
{
   char server[16];
   unsigned short port=LCDPORT;
   unsigned short retry=5;

   strcpy(server, "localhost");
   
   /* Connect to the server, with retry */
   do 
   {
      if ((sock = sock_connect(server, port)) < 0)
      {
         syslog(LOG_DAEMON | LOG_WARNING, "LCD server %s on port %d not available, retrying (%d more)...\n", server, port, retry);
         sleep(5);
      }
      else
         break;
   }
   while (--retry);

   if (sock < 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Error connecting to LCD server %s on port %d.\n", server, port);
      return (-1);
   }
   
   /* Be polite, say "hello" */
   sock_send_string(sock, "hello\n");
   usleep(500000);         /* wait for the server to respond */

   /* Set up our basic screen properties */
   sock_send_string(sock, "screen_add emon\n");
   sock_send_string(sock, "screen_set emon -name emon\n");
   sock_send_string(sock, "screen_set emon -priority foreground\n");
   sock_send_string(sock, "screen_set emon -heartbeat off\n");
 
   /* Add widgets with default content to the screen */
   sock_send_string(sock, "widget_add emon title title\n");
   sock_send_string(sock, "widget_set emon title {Energy Monitor}\n");
   sock_send_string(sock, "widget_add emon line1 string\n");
   sock_send_string(sock, "widget_add emon line2 string\n");
   sock_send_string(sock, "widget_add emon line3 string\n");
   sock_printf(sock, "widget_set emon line1 1 2 {Power now: }\n");
   sock_printf(sock, "widget_set emon line2 1 3 {Energy day: }\n");
   sock_printf(sock, "widget_set emon line3 1 4 {Energy mon: }\n");
   
   return (0);
}

/**********************************************************
 * Public function: lcd_exit()
 * 
 * Description:
 *           Close the connection to the LCDd daemon
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int lcd_exit(void)
{
   if (sock == -1)
   {
      return (-1);
   }
   
   sock_close(sock);
   return (0);
}

/**********************************************************
 * Public function: lcd_print()
 * 
 * Description:
 *           Print data on the LCD display
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int lcd_print(int line, unsigned int value)
{
   if (sock == -1)
   {
      return (-1);
   }
   
   switch (line)
   {
      case 1:
         sock_printf(sock, "widget_set emon line1 1 2 {Power now: %uW}\n", value);
      break;
      case 2:
         sock_printf(sock, "widget_set emon line2 1 3 {Energy day: %.1fkWh}\n", value/1000.0);
      break;
      case 3:
         sock_printf(sock, "widget_set emon line3 1 4 {Energy mon: %.1fkWh}\n", value/1000.0);
      break;
      
      default:
         return (-1);
   }
   
   return (0);
}