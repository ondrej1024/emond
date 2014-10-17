# emond
#### Smart Energy Monitor

### About
This software implements a Smart Energy Monitor to be run on the RaspberryPi. In short, it is a simplified version of a combination of the *emonTX*, *emonGLCD* and *emonBase* modules, developed by the OpenEnergyMonitor project (http://openenergymonitor.org).  
It connects to an energy meter via the SO (pulse) interface and measures/calculates the instant power consumption as well as the electrical energy on daily and monthly basis. The data is sent to EmonCMS (http://emoncms.org) which is the Web server used by the OpenEnergyMonitor project. EmonCMS can then be deployed to further process your data (store, manipulate, chart, ...).  
The easiest way to display the energy data is to use the "My Electrics" appliance in EmonCMS.  
A local LCD display is supported to have instant access to the latest measurements.  
<br>

### Features
- Instant power measurement using energy meter pulses
- Daily and monthly energy calculation
- Filtering of short glitches on the pulse counting GPIO line
- Display of measurements on local LCD display (via integrated lcdproc client)
- Transmission of measurements to EmonCMS (via WebAPI)
- Full compatibility with "My Electric" appliance in EmonCMS
- Easy customization of parameters via configuration file  
<br>

### Coming soon (to do)
- Save daily and monthly energy counters periodically to persistant storage and restore at restart
- Configurable WebAPI update rate limit
- Support for 1-wire temperatue sensor  
<br>

### Nice to have (wishlist)
- Display daily/monthly energy cost
- Command line tool for reading current power values and energy counters
- Alarm generation when approaching maximum power consumption
- Improved input filtering by measuring and validating pulse length  
<br>

### Hardware modules
#### Raspberry Pi
As base module a Raspberry Pi is used to run the software. This dependency is derived from the use of the wiringPi library which greatly simplifies the GPIO handling. However, if the GPIO programming is ported to a standard framework like Linux GPIO sysfs, then **emond** should be able to run also on other embedded Linux boards.  

#### Energy meter
Since **emond** uses the pulse counting method to calculate the instant power and electrical energy, an energy meter with a pulse output has to be used. There are basically two methods:  
- optical pulse counting (via energy meters LED)
- electrical puylse counting (via energy meters SO interface)

For more info on pulse counting see http://openenergymonitor.org/emon/buildingblocks/introduction-to-pulse-counting.  

**emond** is being developed and tested with this type of energy meter that was installed in addition to the one provided by the energy company:  

![Energy Meter](http://www.digitale-elektronik.de/shopsystem/images/WSZ230V-50A_large.jpg)


#### LCD display
The LCD display is optional. It is controlled via the *lcdproc* software. **emond** implements an lcdproc client which sends its data to lcdproc which eventually displays the data on the LCD. Therefore any display supported by lcdproc can be used. However **emond** is optimised for a 20x4 character display such this one:

![LCD display](http://store.melabs.com/graphics/00000001/CFAH2004AYYHJT.jpg)

On the RaspberryPi, lcdproc supports this kind of display connected via the GPIO lines.  
<br>

### Screenshot

This is a screenshot of the EmonCMS dashboard, showing a daily power consumption chart and the current power in a gauge.  

![Screenshot](https://raw.githubusercontent.com/ondrej1024/emond/bf3293137d0f1bc4acef06d7218ffeec1ce595ba/image/dashboard.png)


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
**emond** needs the LCDd server from the lcdproc project (http://www.lcdproc.org) to be installed and running on your system if you want to display the measurements on a local LCD diplay. However, emond can also be used without local display.  
<pre>
    sudo apt-get install lcdproc
</pre>
Then you need to configure the lcdproc server LCDd according to your display via its configuration file LCDd.conf  
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
api_key     = 1234567890  # Personal EmonCMS Write API key 
node_number = 1           # Identifier of your node in EmonCMS
</pre>

<br>


### Run the program

During the installation process, an init script is automatically installed in /etc/init.d/ Therefore the emond program can be started via the following command:
<pre>
    sudo service emon start
</pre>

If you want to autostart the program at every system reboot (recommended), issue the following command:
<pre>
    sudo update-rc.d emon defaults
</pre>


### Contributing

Any contribution like feedback, bug reports or code proposals are welcome and highly encouraged.  
Get in touch by e-mail to ondrej.wisniewski (at) gmail.com  
