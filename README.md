# emond
#### Smart Energy Monitor

### About
This software implements a Smart Energy Monitor to be run on the RaspberryPi. It connects to an energy meter via the SO (pulse) interface and measures/calculates the instant power consumption as well as the electrical energy on daily and mothly basis. The data can be sent to EmonCMS (http://emoncms.org) which is the Web server developed by the OpenEnergyMonitor project (http://openenergymonitor.org). EmonCMS can then be used to further process your data (store, chart, ...).  
A local LCD display is supported to have instant access to the latest measurements.  
<br>

### Features
- Instant power measurement using energy meter pulses
- Daily and monthly energy calculation
- Display of measurements on local LCD display (via integrated lcdproc client)
- Trasmission of measurements to EmonCMS (via WebAPI)
- Easy customization of parameters via configuration file  
<br>

### Coming soon (to do)
- Save daily and monthly energy values periodically and restore at restart
- Display energy cost  
<br>

### Nice to have (wishlist)
- Command line tool for reading power and energy values
- Alarm handling when approaching maximum power consumption  
<br>


### Installation

If you don't have a cross compile environment for the RaspberryPi installed on your PC, it is easiest to build the software directly on the RaspberryPi. Follow theses simple steps from the RPi console. Make sure you have the necessary packages installed (e.g. git).  

* Install the wiringPi library :  
Follow the instructions on the projects home page: http://wiringpi.com/download-and-install  

* Install CURL library :  
<pre>
    sudo apt-get install libcurl4-gnutls-dev
</pre>

* Clone git repository :  
<pre>
    git clone https://github.com/ondrej1024/emond
    cd emond
</pre>

* Alternatively get latest source code version :  
<pre>
    wget https://github.com/ondrej1024/emond/archive/master.zip
    unzip master.zip
    cd emond-master
</pre>

* Build and install :  
<pre>
    cd src
    make
    sudo make install
</pre>

* Install lcdproc :  
emond needs the LCDd server from the lcdproc project (http://www.lcdproc.org) to be installed and running on your system if you want to display the measurements on a local LCD diplay. However, emond can also be used without local display.  
<i>TODO</i>
<br>


### Configuration

You can customize the application to your needs via the config file emon.conf which should be placed in the /etc/ system folder. An example file is provided together with the programs source code.  

<pre>
# Pulse counter specific parameters
################################################
[counter]
pulse_input_pin = 25    # BCM pin number used for pulse input from energy meter
wh_per_pulse    = 100   # Wh per pulse (Energy meter setting)
max_power       = 3000  # max possible power (in W) provided by energy company

# LCD display specific parameters
################################################
[lcd]
lcdproc_port =  # Specify this if not using default lcdproc port
 
# WebAPI specific parameters
################################################
[webapi]
api_key     = 1234567890  # Personal EmonCMS API key 
node_number = 1           # Identifier of your node in EmonCMS
</pre>
  


### Run the program

During the installation process, an init script is automatically installed in /etc/init.d/ Therefore the emond program can be started via the following command:
<pre>
    sudo service emon start
</pre>

If you want to autostart the program at every system reboot, issue the following command:
<pre>
    update-rc.d emon defaults
</pre>
