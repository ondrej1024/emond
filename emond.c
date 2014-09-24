/* 
 * Energy Monitor 
 * 
 * This program counts the pulses from an energy meter and calculates the 
 * following values:
 *  - instant power consumption
 *  - daily energy consumption
 *  - monthly energy consumption
 * 
 * The values will be displayed on a local LCD display via the lcdproc daemon 
 * which needs to be installed and running on the same machine.
 * (see http://lcdproc.omnipotent.net)
 * 
 * Furthermore, the data is sent to a cloud storage via its WebAPI.
 *
 * GPIO handling is done using the wiringPi library, which needs to be installed.
 * (see http://wiringpi.com)
 * 
 * Build command:
 * gcc -o emond emond.c sockets.c lcdproc.c config.c webapi.c \ 
 *  -I/usr/local/include -L/usr/local/lib \
 *  -lwiringPi -lrt -lcurl
 *
 * Author: Ondrej Wisniewski (ondrej.wisniewski at gmail.com)
 * 
 * Last modified: 10/06/2014
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <wiringPi.h>

#include "config.h"
#include "lcdproc.h"
#include "webapi.h"


#define VERSION "0.4"

#define CONFIG_FILE "/etc/emon.conf"

#define DEBUG 1

/* timer period (in sec) */
#define TIMER_PERIOD 30

/* min pulse period for glitch detection */
#define MIN_PULSE_PERIOD_MS 200


typedef struct
{
    /* [counter] */
    unsigned int pulse_input_pin;
    unsigned int wh_per_pulse;
    unsigned int max_power;
    /* [lcd] */
    unsigned int lcdproc_port;
    /* [webapi] */
    const char* api_key;
    unsigned int node_number;
} config_t;

/* Local variables */
static struct itimerval timer;
static config_t config;
static emon_data_t emon_data;
static unsigned long pulse_count_daily=0;
static unsigned long pulse_count_monthly=0;


/**********************************************************
 * Function: config_cb()
 * 
 * Description:
 *           Callback function for handling the name=value
 *           pairs returned by the conf_parse() function
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
static int config_cb(void* user, const char* section, const char* name, const char* value)
{
   config_t* pconfig = (config_t*)user;
   
   #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
   if (MATCH("counter", "pulse_input_pin")) 
   {
      pconfig->pulse_input_pin = atoi(value);
   } 
   else if (MATCH("counter", "wh_per_pulse")) 
   {
      pconfig->wh_per_pulse = atoi(value);
   } 
   else if (MATCH("counter", "max_power")) 
   {
      pconfig->max_power = atoi(value);
   } 
   else if (MATCH("lcd", "lcdproc_port")) 
   {
      pconfig->lcdproc_port = atoi(value);
   } 
   else if (MATCH("webapi", "api_key")) 
   {
      pconfig->api_key = strdup(value);
   } 
   else if (MATCH("webapi", "node_number")) 
   {
      pconfig->node_number = atoi(value);
   } 
   else 
   {
      syslog(LOG_DAEMON | LOG_WARNING, "unknown config parameter %s/%s\n", section, name);
      return -1;  /* unknown section/name, error */
   }
   return 0;
}

/**********************************************************
 * Function: time_diff_ms()
 * 
 * Description:
 *           Calculate the difference between two time 
 *           values in milliseconds
 * 
 * Returns:  time difference (in ms)
 *********************************************************/
static unsigned long time_diff_ms(struct timespec now_ts, struct timespec prev_ts)
{
   unsigned long diff_sec;
   unsigned long diff_msec;
   
   if (now_ts.tv_nsec > prev_ts.tv_nsec)
   {
      /* normal difference */
      diff_msec = (now_ts.tv_nsec - prev_ts.tv_nsec)/1000000;
      diff_sec = now_ts.tv_sec - prev_ts.tv_sec;
   }
   else
   {  /* handle zero crossing */
      diff_msec = (1000000000 - prev_ts.tv_nsec + now_ts.tv_nsec)/1000000;
      diff_sec = now_ts.tv_sec - prev_ts.tv_sec - 1;
   }
   
   /* return difference in milliseconds (this will *
    * overflow if difference is greater 49,7 days) */
   return ((diff_sec*1000) + diff_msec);
}

/**********************************************************
 * Function: exit_handler()
 * 
 * Description:
 *           Handles the cleanup at reception of the 
 *           TERM signal.
 * 
 * Returns:  -
 *********************************************************/
static void exit_handler(int signum)
{
   lcd_exit();
   syslog(LOG_DAEMON | LOG_NOTICE, "Exit Energy Monitor\n");
   exit(0);
}

/**********************************************************
 * Function: sigpipe_handler()
 * 
 * Description:
 *           Handles the reception of the PIPE signal and
 *           restarts communication with the LCD daemon.
 * 
 * Returns:  -
 *********************************************************/
void sigpipe_handler(int signum)
{
   syslog(LOG_DAEMON | LOG_NOTICE, "Broken connection to LCDd, reinitializing ...\n");
   lcd_init();
}

/**********************************************************
 * Function: gpio_handler()
 * 
 * Description:
 *           Handles the event of the pulse detected on 
 *           the GPIO pin.
 * 
 *           It calculates the time between the last two 
 *           pulses and the resulting power consumption.
 * 
 * Returns:  -
 *********************************************************/
static void gpio_handler(void)
{
   static int first = 1;   
   static struct timespec prev_ts;
   struct timespec now_ts;
   unsigned long t_diff;
   unsigned int power;
   
  
   clock_gettime(CLOCK_REALTIME, &now_ts);
   if (first)
   {
      first=0;
      //printf ("first edge deteted\n");
      /* Count pulses */
      pulse_count_daily++;
      pulse_count_monthly++;
      
      /* Display updated measurements on LCD */
      lcd_print(1, 0);
      lcd_print(2, pulse_count_daily*config.wh_per_pulse);
      lcd_print(3, pulse_count_monthly*config.wh_per_pulse);
   }
   else
   {
      t_diff = time_diff_ms(now_ts, prev_ts);
      //printf ("elapsed %lu ms since last toggle event\n", t_diff);

      /* Filter very short pulses (glitches) */
      if (t_diff > MIN_PULSE_PERIOD_MS)
      {
         /* Calculate instant power (in Watt) and display it */
         power = (unsigned int)(config.wh_per_pulse*3600000.0/t_diff);

         /* Filter spurious pulses */
         if (power < config.max_power)
         {         
            syslog(LOG_DAEMON | LOG_NOTICE, "Instant power is %u W\n", power);
            
            /* Count pulses */
            pulse_count_daily++;
            pulse_count_monthly++;
            
            /* Display updated measurements on LCD */
            lcd_print(1, power);
            lcd_print(2, pulse_count_daily*config.wh_per_pulse);
            lcd_print(3, pulse_count_monthly*config.wh_per_pulse);
            
            /* Send data to EmonCMS via WebAPI */
            emon_data.inst_power = power;
            emoncms_send(&emon_data);
         }
         else
         {
            syslog(LOG_DAEMON | LOG_WARNING, "Instant power is out of range! (%u W), spurious pulse?\n", power);
         }
      }
   }
   prev_ts = now_ts;
}

/**********************************************************
 * Function: is_midnight()
 * 
 * Description:
 *           Checks if the current time is midnight. 
 * 
 * Returns:  1 if the current time is 00:00 min
 *           0 otherwise
 *********************************************************/
static int is_midnight(void)
{
   struct tm *now_tm;
   time_t now = time(NULL);
   
   now_tm = localtime(&now);
   
   return ((now_tm->tm_hour==0) && (now_tm->tm_min==0));
}

/**********************************************************
 * Function: is_first_dom()
 * 
 * Description:
 *           Checks if the current day is the first day 
 *           of the month
 * 
 * Returns:  1 if the current day is 1
 *           0 otherwise
 *********************************************************/
static int is_first_dom(void)
{
   struct tm *now_tm;
   time_t now = time(NULL);
   
   now_tm = localtime(&now);
   
   return (now_tm->tm_mday == 1);
}

/**********************************************************
 * Function: timer_handler()
 * 
 * Description:
 *           Handles the periodic timer event.
 * 
 *           It resets the daily and monthly energy counters
 *           at midnight and the first day of the month.
 * 
 * Returns:  -
 *********************************************************/
static void timer_handler(int signum)
{
   static int reset_done=0;
   
   /* Restart the timer */
   if (setitimer(ITIMER_REAL, &timer, NULL) < 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to restart interval timer: %s\n", strerror (errno));
      return;
   }
   
   if (is_midnight())
   {
      if (!reset_done)
      {
         emon_data.energy_day = pulse_count_daily*config.wh_per_pulse;
         
         /* Reset daily pulse counter */
         syslog(LOG_DAEMON | LOG_NOTICE, "Resetting daily energy counter (current value %lu)\n", 
                pulse_count_daily);
         pulse_count_daily=0;
         reset_done=1;
         
         /* Save monthly pulse counter to flash */
         /* TODO */

         if(is_first_dom())
         {
            emon_data.energy_month = pulse_count_monthly*config.wh_per_pulse;
            
            /* Reset monthly pulse counter */
            syslog(LOG_DAEMON | LOG_NOTICE, "Resetting monthly energy counter (current value %lu)\n", 
                   pulse_count_monthly);
            pulse_count_monthly=0;
         }
         
         /* Send data to EmonCMS via WebAPI */
         /* TODO: retry if sending fails */
         emoncms_send(&emon_data);
      }
   }
   else
   {
      reset_done=0;
   }
}



/**********************************************************
 * 
 * Main function
 * 
 *********************************************************/
int main(int argc, char **argv)
{
   openlog("emond", LOG_PID|LOG_CONS, LOG_USER);
   syslog(LOG_DAEMON | LOG_NOTICE, "Starting Energy Monitor (version %s)\n", VERSION);
   
   /* Setup error handlers */
   signal(SIGINT, exit_handler);   /* Ctrl-C */
   signal(SIGTERM, exit_handler);  /* "regular" kill */
   signal(SIGPIPE, sigpipe_handler);  /* write to closed socket */
   
   /* Setup timer handler */
   signal(SIGALRM, timer_handler);
   
   /* Load configuration from .conf file */
   if (conf_parse(CONFIG_FILE, config_cb, &config) < 0) 
   {
        syslog(LOG_DAEMON | LOG_ERR, "Can't load %s\n", CONFIG_FILE);
        return (1);
   }

#ifdef DEBUG
   syslog(LOG_DAEMON | LOG_NOTICE, "Config paramters read from .conf file:\n");
   syslog(LOG_DAEMON | LOG_NOTICE, "pulse_input_pin: %u\n", config.pulse_input_pin);
   syslog(LOG_DAEMON | LOG_NOTICE, "wh_per_pulse: %u\n", config.wh_per_pulse);
   syslog(LOG_DAEMON | LOG_NOTICE, "max_power: %u\n", config.max_power);
   syslog(LOG_DAEMON | LOG_NOTICE, "lcdproc_port: %u\n", config.lcdproc_port);
   syslog(LOG_DAEMON | LOG_NOTICE, "api_key: %s\n", config.api_key);   
   syslog(LOG_DAEMON | LOG_NOTICE, "node_number: %u\n", config.node_number);
#endif
   
   /* Fill in common data for WebAPI */
   emon_data.api_key = strdup(config.api_key);
   emon_data.node_number = config.node_number;
   
   /* Load monthly (and daily?) pulse counter from flash */
   /* TODO */

   /* Init LCD screen */
   if (lcd_init() < 0)
   {
      syslog(LOG_DAEMON | LOG_WARNING, "Unable to setup LCD screen, display is disabled\n");
   }
   
   /* Initialise the GPIO lines */ 
   if (wiringPiSetupGpio() < 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to setup GPIO: %s\n", strerror (errno));
      return (2);
   }
   pinMode(config.pulse_input_pin, INPUT);
   pullUpDnControl(config.pulse_input_pin, PUD_UP);
   usleep(10000);
   
   if (wiringPiISR(config.pulse_input_pin, INT_EDGE_FALLING, gpio_handler) < 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to setup ISR for GPIO: %s\n", strerror (errno));
      return (3);
   }
   
   /* Configure the timer period (one shot mode). 
    * Periodic interval timer is not reliable, it stops after some time.
    * Therefore we restart the timer in the timer handler procedure.
    */
   timer.it_value.tv_sec = TIMER_PERIOD;
   timer.it_value.tv_usec = 0;
   timer.it_interval.tv_sec = 0; //TIMER_PERIOD;
   timer.it_interval.tv_usec = 0;
   
   /* Start an interval timer using the system clock */
   if (setitimer(ITIMER_REAL, &timer, NULL) < 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to setup interval timer: %s\n", strerror (errno));
      return (4);
   }
   
   /*
    * Initialization is done. All the other work will be done 
    * in the event handlers which are activated by the timer and
    * GPIO interrupts.
    */
   while (1)
   {
      sleep (1);
   }
   
   return (0);
}
