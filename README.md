# LilyGo-SIM7000-Adafruit-IO-Client
This is an Arduino sketch designed for the LilyGo TTGO SIM7000
to demonstrate the use of SIM7000 MQTT commands to send
data to Adafruit IO.  You can set up a free Adafruit IO account to test it.
Once you have an Adafruit IO account, you'll need to insert your IO_USERNAME
and your IO_KEY in the MQTT parameters in the sketch.  On your Adafruit IO account, you'll
need to set up feeds called "rssi" and "speed", or change the feed names in lines
72 and 73 of the sketch.

The SIM7000 MQTT commands are documented in the following SIMCom application note:
https://simcom.ee/documents/SIM7000x/SIM7000%20Series_MQTT_Application%20Note_V1.00.pdf

They are not documented in the SIM7000 AT command manual at:
https://simcom.ee/documents/SIM7000x/SIM7000%20Series_AT%20Command%20Manual_V1.04.pdf

I strongly recommend you have a copy of the above two documents close at hand when
reading this program.

For the full set of SIM7000 documentation see:
https://simcom.ee/documents/?dir=SIM7000x

I used the TinyGSM library code as a starting point for this program.  But the only functions
from that library that this program uses are the GPS-related functions.  I used a
trial-and-error process to figure out what AT commands are necessary to bring the SIM7000
online on an LTE CAT-M or NB-IoT network.  I then referred to the SIMCom SIM7000 MQTT
application note to figure out how to connect the SIM7000 to the Adafruit IO MQTT broker
and publish data to the broker.

This program is currently set up to work with a Hologram SIM.  It seems to work on the
T-Mobile and AT&T networks in the USA, or at least in Prescott and Sedona, Arizona.

This version of the program is set up to report the cellular network received signal strength
indicator (RSSI) on the Adafruit IO feed.  You can easily change the program to report
another value, for example speed as measured by the SIM7000 GNSS receiver.
