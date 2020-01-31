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
 *  -lwiringPi -lrt -lcurl -lpthread
 *
 * Author: Ondrej Wisniewski (ondrej.wisniewski at gmail.com)
 * 
 * Last modified: 24/08/2015
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


/* Uncomment this to enable debug mode */
//#define DEBUG

#define VERSION "0.7.1"

#define CONFIG_FILE "/etc/emon.conf"
#define NV_FILENAME "emond.dat"

/* timer period (in sec) */
#define TIMER_PERIOD 30

/* min pulse period for glitch detection */
#define MIN_PULSE_PERIOD_MS 200

/* tolerance for pulse verification (in %) */
#define PULSE_TOLERANCE 5


typedef struct
{
    /* [counter] */
    unsigned int pulse_input_pin;
    unsigned int wh_per_pulse;
    unsigned int pulse_length;
    unsigned int max_power;
    /* [storage] */
    const char* flash_dir;
    /* [lcd] */
    unsigned int lcdproc_port;
    /* [webapi] */
    const char* api_base_uri;
    const char* api_key;
    unsigned int api_update_rate;
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
   else if (MATCH("counter", "pulse_length")) 
   {
      pconfig->pulse_length = atoi(value);
   } 
   else if (MATCH("counter", "max_power")) 
   {
      pconfig->max_power = atoi(value);
   } 
   else if (MATCH("storage", "flash_dir")) 
   {
      pconfig->flash_dir = strdup(value);
   } 
   else if (MATCH("lcd", "lcdproc_port")) 
   {
      pconfig->lcdproc_port = atoi(value);
   } 
   else if (MATCH("webapi", "api_base_uri")) 
   {
      pconfig->api_base_uri = strdup(value);
   } 
   else if (MATCH("webapi", "api_key")) 
   {
      pconfig->api_key = strdup(value);
   } 
   else if (MATCH("webapi", "api_update_rate")) 
   {
      pconfig->api_update_rate = atoi(value);
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
 * Function: read_flash()
 * 
 * Description:
 *           Reads non volatile data from permanent
 *           storage on flash disk.
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int read_flash(const char *path, const char *filename)
{ 

#define BUFSIZE 16
   
   FILE *f;
   char file[40];
   char buf[BUFSIZE];
   int  rc=0;
   
   /* TODO: check that the saved data is still useful,
    * e.g. we didn't cross midnight since last saving data 
    */
   
   sprintf(file, "%s/%s", path, filename);
   if ((f=fopen(file, "r")) != NULL)
   {
      /* Read daily counter */
      if (fgets(buf, BUFSIZE, f) != NULL)
      {
         pulse_count_daily=atoi(buf); 
      
         /* Read monthly counter */
         if (fgets(buf, BUFSIZE, f) != NULL)
         {
            pulse_count_monthly=atoi(buf); 
            syslog(LOG_DAEMON | LOG_INFO, "Load data from file: daily counter %lu, monthly counter %lu\n", 
                                           pulse_count_daily, pulse_count_monthly);
         }
         else
         {
            syslog(LOG_DAEMON | LOG_ERR, "Error reading monthly counter from file: %s\n", strerror(errno));
            rc = -2;  
         }      
      }
      else
      {
         syslog(LOG_DAEMON | LOG_ERR, "Error reading daily counter from file: %s\n", strerror(errno));
         rc = -1;  
      }
      
      fclose(f);
   }
   else
   {
      if (errno == 2)
      {
         syslog(LOG_DAEMON | LOG_INFO, "Data file %s not yet created\n", file);
      }
      else
      {
         syslog(LOG_DAEMON | LOG_ERR, "Error opening data file %s for reading: %s\n", file, strerror(errno));
         rc = -3;
      }
   }
   
   return rc;
}

/**********************************************************
 * Function: write_flash()
 * 
 * Description:
 *           Writes non volatile data to permanent
 *           storage on flash disk (must be writeable).
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int write_flash(const char *path, const char *filename)
{ 

   FILE *f;
   char file[40];
   int  rc=0;

   sprintf(file, "%s/%s", path, filename);
   if ((f=fopen(file, "w")) != NULL)
   {
      if (fprintf(f, "%lu\n", pulse_count_daily) > 0)
      {
         if (fprintf(f, "%lu\n", pulse_count_monthly) > 0)
         {
            rc = 0;
#ifdef DEBUG
            syslog(LOG_DAEMON | LOG_DEBUG, "Saved data to file: daily counter %lu, monthly counter %lu\n", 
                                            pulse_count_daily, pulse_count_monthly);
#endif
         }
         else
         {
            syslog(LOG_DAEMON | LOG_ERR, "Error writing monthly counter to file: %s\n", strerror(errno));
            rc = -2;  
         }
      }
      else
      {
         syslog(LOG_DAEMON | LOG_ERR, "Error writing daily counter to file: %s\n", strerror(errno));
         rc = -1;  
      }
      
      fclose(f);
   }
   else
   {
      syslog(LOG_DAEMON | LOG_ERR, "Error opening file %s for writing: %s", file, strerror(errno));
      rc = -3;
   }
   
   return rc;
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
 *           First a validation of the pulse is performed
 *           to filter out wrong pulses. Then the time 
 *           between the last two pulses and the resulting 
 *           power consumption is calculated.
 * 
 * Returns:  -
 *********************************************************/
static void gpio_handler(void)
{
   static int first = 1;   
   static int pulse_started = 0;
   static struct timespec prev_ts;
   static struct timespec pulse_start_ts;
   struct timespec now_ts;
   struct timespec pulse_end_ts;

   unsigned long t_diff;
   unsigned long pulse_length;
   unsigned long pulse_delta = (config.pulse_length*PULSE_TOLERANCE)/100;
   unsigned int power;
   
  
   /* read current pin value */
   if (digitalRead(config.pulse_input_pin) == LOW)
   {
      /* Pulse started, check validity */   
      if (pulse_started == 0)
      {
         pulse_started = 1;
         clock_gettime(CLOCK_REALTIME, &pulse_start_ts);
         //syslog(LOG_DAEMON | LOG_DEBUG, "Detected starting pulse");
      }
      else
      {
         syslog(LOG_DAEMON | LOG_WARNING, "Detected starting pulse out of sequence");
      }
   }
   else
   {
      /* Pulse ended,  check validity */
      if (pulse_started == 1)
      {
         pulse_started = 0;
         clock_gettime(CLOCK_REALTIME, &pulse_end_ts);
         pulse_length = time_diff_ms(pulse_end_ts, pulse_start_ts);
#ifdef DEBUG
         syslog(LOG_DAEMON | LOG_DEBUG, "Detected pulse with length %lu ms", pulse_length);
#endif
         /* If no reference pulse length was specified in the 
          * configuration file, we use the length of the first
          * pulse as reference to validate the subsequent pulses
          */
         if (first && (config.pulse_length==0))
         {
             config.pulse_length = pulse_length;
             pulse_delta = (config.pulse_length*PULSE_TOLERANCE)/100;
             syslog(LOG_DAEMON | LOG_INFO, "Using pulse lenght %lu ms as reference", pulse_length);
         }

         /* Check if pulse lenght is within expected limits 
          * (from energy meter data sheet), apply a tolerance 
          */         
         if (pulse_length > (config.pulse_length-pulse_delta) && 
             pulse_length < (config.pulse_length+pulse_delta))
         {
            /* Pulse is valid, but more checks will be performed */
            
            now_ts = pulse_end_ts;
            if (first)
            {
               first=0;
               
               syslog(LOG_DAEMON | LOG_INFO, "Detected first pulse with length %lu ms", pulse_length);
               
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
               /* Calculate elapsed time since last pulse occured */
               t_diff = time_diff_ms(now_ts, prev_ts);
               
               /* Filter pulses which occur very close to each other (possible glitches) */
               if (t_diff > MIN_PULSE_PERIOD_MS)
               {
                  /* Calculate instant power (in Watt) and display it */
                  power = (unsigned int)(config.wh_per_pulse*3600000.0/t_diff);
                  
                  /* Filter impossible high power values */
                  if (power < config.max_power)
                  {
                     /* All filter checks passed, perform measurement/calculation */
#ifdef DEBUG
                     syslog(LOG_DAEMON | LOG_DEBUG, "Instant power is %u W\n", power);
#endif
                     /* Count pulses */
                     pulse_count_daily++;
                     pulse_count_monthly++;
                     
                     /* Display updated measurements on LCD */
                     lcd_print(1, power);
                     lcd_print(2, pulse_count_daily*config.wh_per_pulse);
                     lcd_print(3, pulse_count_monthly*config.wh_per_pulse);
                     
                     /* Send data to EmonCMS via WebAPI */
                     emon_data.inst_power = power;
                     emon_data.energy_day = pulse_count_daily*config.wh_per_pulse;
                     emon_data.energy_month = pulse_count_monthly*config.wh_per_pulse;
                     emoncms_send(&emon_data);
                  }
                  else
                  {
                     syslog(LOG_DAEMON | LOG_WARNING, "Instant power is out of range! (%u W)\n", power);
                  }
               }
            }
            prev_ts = now_ts;
         }
         else
         {
            syslog(LOG_DAEMON | LOG_WARNING, "Detected invalid pulse (length=%lu ms)\n", pulse_length);
         }
      }
      else
      {
         syslog(LOG_DAEMON | LOG_WARNING, "Detected ending pulse out of sequence");
      }
   } 
}

/**********************************************************
 * Function: is_full_hour()
 * 
 * Description:
 *           Checks if the current time is the full hour. 
 * 
 * Returns:  1 if the current time is xx:00 min
 *           0 otherwise
 *********************************************************/
static int is_full_hour(void)
{
   struct tm *now_tm;
   time_t now = time(NULL);
   
   now_tm = localtime(&now);
   
   return (now_tm->tm_min==0);
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
   static int save_done=0;
   
   /* Restart the timer */
   if (setitimer(ITIMER_REAL, &timer, NULL) < 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to restart interval timer: %s\n", strerror (errno));
      return;
   }

   /* Check if it is midnight */
   if (is_midnight())
   {
      if (!reset_done)
      {
         /* Reset daily pulse counter */
         syslog(LOG_DAEMON | LOG_NOTICE, "Resetting daily energy counter (current value %lu)\n", 
                pulse_count_daily);
         pulse_count_daily=0;
         reset_done=1;
         
         if(is_first_dom())
         {
            /* Reset monthly pulse counter */
            syslog(LOG_DAEMON | LOG_NOTICE, "Resetting monthly energy counter (current value %lu)\n", 
                   pulse_count_monthly);
            pulse_count_monthly=0;
         }
      }
   }
   else
   {
      reset_done=0;
   }
   
   /* Check if it is the full hour */
   if (is_full_hour())
   {
      if (!save_done)
      {
         /* Save pulse counters to flash */
         if (strlen(config.flash_dir) > 0)
         {
            write_flash(config.flash_dir, NV_FILENAME);
         }
         save_done=1;
      }
   }
   else
   {
      save_done=0;
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
   memset((void*)&config, 0, sizeof(config));
   if (conf_parse(CONFIG_FILE, config_cb, &config) < 0) 
   {
        syslog(LOG_DAEMON | LOG_ERR, "Can't load %s\n", CONFIG_FILE);
        return (1);
   }

   syslog(LOG_DAEMON | LOG_NOTICE, "Config parameters read from %s:\n", CONFIG_FILE);
   syslog(LOG_DAEMON | LOG_NOTICE, "***************************\n");
   syslog(LOG_DAEMON | LOG_NOTICE, "pulse_input_pin: %u\n", config.pulse_input_pin);
   syslog(LOG_DAEMON | LOG_NOTICE, "wh_per_pulse: %u\n", config.wh_per_pulse);
   syslog(LOG_DAEMON | LOG_NOTICE, "pulse_length: %u\n", config.pulse_length);
   syslog(LOG_DAEMON | LOG_NOTICE, "max_power: %u\n", config.max_power);
   if (config.flash_dir != NULL)
      syslog(LOG_DAEMON | LOG_NOTICE, "flash_dir: %s\n", config.flash_dir);
   syslog(LOG_DAEMON | LOG_NOTICE, "lcdproc_port: %u\n", config.lcdproc_port);
   if (config.api_base_uri != NULL)
      syslog(LOG_DAEMON | LOG_NOTICE, "api_base_uri: %s\n", config.api_base_uri);
   if (config.api_key != NULL)
      syslog(LOG_DAEMON | LOG_NOTICE, "api_key: %s\n", config.api_key);   
   syslog(LOG_DAEMON | LOG_NOTICE, "api_update_rate: %u\n", config.api_update_rate);
   syslog(LOG_DAEMON | LOG_NOTICE, "node_number: %u\n", config.node_number);
   syslog(LOG_DAEMON | LOG_NOTICE, "***************************\n");
   
   /* Fill in common data for WebAPI */
   emon_data.api_base_uri = config.api_base_uri;
   emon_data.api_key = config.api_key;
   emon_data.api_update_rate = config.api_update_rate;
   emon_data.node_number = config.node_number;
   
   /* Load monthly and daily pulse counters from flash */
   if (config.flash_dir != NULL)
   {
      read_flash(config.flash_dir, NV_FILENAME);
   }
   else
   {
      syslog(LOG_DAEMON | LOG_INFO, "No storage dir provided in config, disabling periodic storage of counter values");
   }
   
   /* Init LCD screen */
   if (lcd_init() < 0)
   {
      syslog(LOG_DAEMON | LOG_WARNING, "Unable to setup LCD screen, display is disabled\n");
   }
   
   if (config.pulse_input_pin > 0)
   {
      /* Initialise the GPIO lines */ 
      if (wiringPiSetupGpio() < 0)
      {
         syslog(LOG_DAEMON | LOG_ERR, "Unable to setup GPIO: %s\n", strerror (errno));
         return (2);
      }
      pinMode(config.pulse_input_pin, INPUT);
      pullUpDnControl(config.pulse_input_pin, PUD_UP);
      usleep(10000);
      
      /* Generate interrupt on both edges on the inut pin */
      if (wiringPiISR(config.pulse_input_pin, INT_EDGE_BOTH, gpio_handler) < 0)
      {
         syslog(LOG_DAEMON | LOG_ERR, "Unable to setup ISR for GPIO: %s\n", strerror (errno));
         return (3);
      }
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
      sleep(10);
   }
   
   return (0);
}
