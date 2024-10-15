# LilyGo-SIM7000-Adafruit-IO-Client
This is a program to demonstrate the use of SIM7000 MQTT commands to send
data to Adafruit IO.  You can set up a free Adafruit IO account to test it.

The SIM7000 MQTT commands are documented in the following SIMCom application note:
https://github.com/Xinyuan-LilyGO/LilyGO-T-SIM7000G/blob/098ee1f9f1405eb8e8e250e660815a50cbd76a51/docs/SIM7000/SIM7000%20Series_MQTT(S)_Application%20Note_V1.02.pdf

They are not documented in the SIM7000 AT command manual at:
https://github.com/Xinyuan-LilyGO/LilyGO-T-SIM7000G/blob/098ee1f9f1405eb8e8e250e660815a50cbd76a51/docs/SIM7000/SIM7000%20Series_AT%20Command%20Manual_V1.06.pdf

I strongly recommend you have a copy of the above two documents close at hand when
reading this program.

For the full set of SIM7000 documentation see:
https://github.com/Xinyuan-LilyGO/LilyGO-T-SIM7000G/tree/098ee1f9f1405eb8e8e250e660815a50cbd76a51/docs/SIM7000


I used the TinyGSM library code as a starting point for this program.  But the only functions
from that library that this program uses are the GPS-related functions.  I used a
trial-and-error process to figure out what AT commands are necessary to bring the SIM7000
online on an LTE CAT-M or NB-IoT network.  I then referred to the SIMCom SIM7000 MQTT
application note to figure out how to connect the SIM7000 to the Adafruit IO MQTT broker
and publish data to the broker.

This program is currently set up to work with a Hologram SIM.  It seems to work on the
T-Mobile and AT&T networks in the USA, or at least in Prescott and Sedona, Arizona.

This version of the program is set up to report the cellular network reference signal signal-to
noise ratio (RSSNR) and GPS speed on the Adafruit IO feeds.  You can easily change the program to report other parameters.
