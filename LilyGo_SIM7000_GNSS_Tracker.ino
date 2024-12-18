/* 
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
noise ratio (RSSNR), LilyGo T-SIM7000G 18650 battery voltage, and GPS speed on the Adafruit IO feeds.   

Copyright 2024 Robert F. Fleming, III

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the “Software”), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice must be included in all copies
or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <ArduinoTrace.h>
#include <esp_task_wdt.h>

// define a constant for converting km per hr to miles per hr
constexpr float KMPHTOMPH = 0.6213712;

// set up a 45-second watchdog timer to restart if error or lost connection
constexpr unsigned long WDT_TIMEOUT = 45000;

#define LEDPIN 12

unsigned long restartESP;

// Defined the ESP32 pins that are connected to the SIM7000
#define PWR_PIN 4
#define PIN_RX 26
#define PIN_TX 27

#define TINY_GSM_MODEM_SIM7000

// Define some C string buffer sizes
#define BUFSIZE1 65
#define BUFSIZE2 35
#define BUFSIZE3 121

// Constants for battery voltage calculation
constexpr float VBATT_MIN = 1945.0;  // ADC value for 3.6V
constexpr float  VBATT_RANGE = 616.7; // ADC value range for 3.6V to 4.2V
constexpr float  VBATT_MIN_VOLTAGE = 3.6;

// Timeout constants
constexpr unsigned long  SERIAL_TIMEOUT = 2000;
constexpr unsigned long  MQTT_PUBLISH_INTERVAL = 19000;
constexpr unsigned long  CONNECTION_CHECK_INTERVAL = 89000;
constexpr unsigned long  BATTERY_CHECK_INTERVAL = 180000;
constexpr unsigned long  RECONNECT_TIMEOUT = 60000;

// MQTT details
// Sign up for a free Adafruit IO account.
// While signed into your account, you can click on the gold circle with the
// black key in the upper right corner of the Adafruit IO web page to get your
// IO_USERNAME (mqttUser[]) and IO_KEY (mqtt_Pass) and fill them in below.
// Also, you can change the mqttFeed names if you want.
const char* broker = "io.adafruit.com";
//const char* broker = "demo.thingsboard.io";
const uint16_t port = 1883;
const char mqttUser[] = "<your Adafruit IO IO_USERNAME>";
const char mqttPass[] = "<your Adafruit IO IO_KEY>";
const char mqttFeed1[] = "rssnr";
const char mqttFeed2[] = "speed";
const char mqttFeed3[] = "vbatt";

bool restarting = false;

// The TinyGSM library is only used for the GPS functions, which I'm too
// lazy to try to reinvent right now.
#include <TinyGsmClient.h>
TinyGsm modem(Serial1);
TinyGsmClient client(modem);

unsigned long t_zero, t_one, t_two;

// This function waits for a response from the SIM7000 of either "OK" or "ERROR".
// The SIM7000 uses a \r\n line terminator, so handle the \r
bool waitForOKorError(unsigned long waitTime) {
  unsigned long t_0 = millis();
  String str;

  while (millis() < t_0 + waitTime) {  //wait for OK or ERROR or timeout
    if (Serial1.available() > 0) {
      str = Serial1.readStringUntil('\n');
    }
    // Serial.println( "String received in waitForOKOrError = " + str + " ," + String(str.length()) );
    // Serial.println("Response wait time = " + String(millis() - t_0));
    if (str.indexOf("OK") != -1) { return true; }
    if (str.indexOf("ERROR") != -1) { return false; }
  }
  return false;
}

// This function waits for a response from the SIM7000 of either "> " or "ERROR".
// The MQTT publish command, AT+SMPUB, wants the command in two parts, and the
// SIM7000 issues a "> " prompt for the second part.
// The SIM7000 uses a \r\n line terminator, so handle the \r
bool waitForPromptOrError(unsigned long waitTime) {
  unsigned long t_0 = millis();
  String str;

  while (millis() < t_0 + waitTime) {  //wait for OK or ERROR or timeout
    if (Serial1.available() > 0) {
      str = Serial1.readStringUntil('\n');
    }

    //Serial.println("String received in waitForPromptOrError = " + str);
    //Serial.println("Response wait time = " + String(millis() - t_0));

    if ( str.indexOf("> ")!= -1 ) { return true; }
    if ( str.indexOf("ERROR") != -1 ) { return false; }
  }
  return false;
}

// Read and parse the response from the SIM7000.  If E1 command echo is turned on,
// the first token  returned by strtok() is the command just sent.
// In that case, we want to skip everything up to the first newline
// and then start processing.
// This function is used when we need a specific response value from the SIM7000.
char* getResponse(uint8_t tokenNr, char* delimiters, bool skipCmd, unsigned long waitTime) {
  unsigned long t_0 = millis();

  while ((Serial1.available() == 0) && (millis() < t_0 + waitTime)) {}  //wait for data available
  String response = Serial1.readString();

  // Serial.print("Response from Serial1 = ");
  // Serial.println(response);
  // Serial.print("Response length = ");
  // Serial.println(response.length());

  char resp[BUFSIZE3] = "";
  response.toCharArray(resp, BUFSIZE3);
  char* p_resp = resp;

  char newLine[2] = "\n";
  char* tok;

  if (skipCmd) {
    tok = strtok_r(p_resp, newLine, &p_resp);
  } else {
    tok = strtok_r(p_resp, delimiters, &p_resp);
    tokenNr--;
  }

  while ((tok != NULL) && (tokenNr > 0)) {
    tok = strtok_r(NULL, delimiters, &p_resp);  // next please!
    if (tok == NULL) { break; }
    tokenNr--;
  }

  // Serial.print("tok = ");
  // Serial.println(tok);

  static char staticTok[BUFSIZE1] = "";
  if (tok != NULL) { strncpy(staticTok, tok, BUFSIZE1); }
  return staticTok;
}

// When the SIM7000 first comes out of reset, it spits out some status reports
// and what may be some Chinese characters before it starts responding to AT commands
// with an "OK" response.  This function waits for the "OK".
bool areWeAwakeYet() {
  // Send the modem "AT".  Read the string from the SIM7000.  If it contains "OK",
  // the SIM7000 is awake.
  unsigned long t_0 = millis();

  // Give it a minute to wake up
  while (millis() < t_0 + 60000) {
    Serial1.flush();
    Serial1.println("AT");
    while (Serial1.available() == 0) {}  //wait for data available
    String response = Serial1.readString();
    Serial.println(response);

    if ( response.indexOf("OK") != -1 ) {
      return true;
    }
  }

  return false;
}

// This function issues the commands necessary to first connect the SIM7000
// to an LTE CAT-M or NB-IoT network, then issues the commands necessary
// to connect the SIM7000 to the Adafuit IO MQTT broker.
bool bringMQTTOnline(void) {
  char buf[BUFSIZE1];

  Serial1.flush();
  Serial1.println("ATE1");

  if (!waitForOKorError(2000)) {
    Serial.println("SIM7000 didn't respond with \"OK\" to \"ATE1\"");
    return false;
  } else {
    Serial.println("SIM7000 responded to \"ATE1\" with \"OK\"");
  }

  // Turn on GPS power
  Serial.println("Enabling GPS power...");

  Serial1.flush();
  Serial1.println("AT+SGPIO=0,4,1,1");

  if (waitForOKorError(2000)) {
    Serial.println("GPS power supply enabled!");
  } else {
    Serial.println("Problem enabling GPS power!");
  }

  delay(3000);

  if (!modem.enableGPS()) {
    Serial.println("Failed to enable GPS");
    // Continue anyway, but the results won't be spectacular
  }

  // AT+SAPBR=3,1,"APN","hologram"  Use the Hologram SIM for the APN
  Serial1.flush();
  Serial1.println("AT+SAPBR=3,1,\"APN\",\"hologram\"");
  printResponse(1000);

  // AT+CNMP=38  set preferred connection type to LTE
  Serial1.flush();
  Serial1.println("AT+CNMP=51");
  printResponse(1000);

  // AT+CMNB=1  set preferred connection type to CAT-M1.
  Serial1.flush();
  Serial1.println("AT+CMNB=1");
  printResponse(2000);

  // AT+CIPMUX=1
  Serial1.flush();
  Serial1.println("AT+CIPMUX=0");  // start a single IP connection
  printResponse(1000);

  // AT+CSTT="hologram","",""
  Serial1.flush();
  Serial1.println("AT+CSTT=\"hologram\",\"\",\"\"");
  printResponse(1000);

  // AT+CNACT=1,"hologram"  Make the network active using the supplied APN (I think)
  Serial1.flush();
  Serial1.println("AT+CNACT=1,\"hologram\"");
  printResponse(1000);

  // AT+COPS?"  query network information
  // a response like +COPS: 0,0,"AT&T Hologram",7 or +COPS: 0,0,"T-Mobile Hologram",7 seems good
  // the '7' in the response means User-specified LTE M1 A GB access technology
  // a '9' in the response would mean User-specified LTE NB S1 access technology
  Serial1.flush();
  Serial1.println("AT+COPS?");
  printResponse(1000);

  // AT+CGNAPN  query CAT-M1 or NB-IoT network after the successful registration of APN
  // Basically, if we get a response like +CGNAPN: 1,"hologram" back, we're connected
  Serial1.flush();
  Serial1.println("AT+CGNAPN");
  printResponse(1000);

  // AT+CGNSHOR=10  set the desired GNSS accuracy to 10 meters
  Serial1.flush();
  Serial1.println("AT+CGNSHOR=10");
  printResponse(1000);

  // AT+SMCONF="URL","io.adafruit.com"
  Serial1.flush();
  Serial1.println("AT+SMCONF=\"URL\",\"io.adafruit.com\"");
  printResponse(1000);

  // AT+SMCONF="USERNAME","<your Adafruit IO IO_USERNAME>"
  sprintf(buf, "AT+SMCONF=\"USERNAME\",\"%s\"", mqttUser);
  Serial1.flush();
  Serial1.println(buf);
  printResponse(1000);

  // AT+SMCONF="PASSWORD","<your Adafruit IO IO_KEY>"
  sprintf(buf, "AT+SMCONF=\"PASSWORD\",\"%s\"", mqttPass);
  Serial1.flush();
  Serial1.println(buf);
  printResponse(1000);

  // AT+SMCONN
  Serial1.flush();
  Serial1.println("AT+SMCONN");
  if (!waitForOKorError(10000)) {
    Serial.println("SIM7000 didn't connect to MQTT broker :-(");
    return false;
  } else {
    Serial.println("SIM7000 is connected to MQTT broker!");
  }
  return true;
}

// this function reads the battery voltage from ESP32 IO35 and converts to volts
// using calibration constants determined for my board using a voltmeter and
// variable voltage power supply
float readBatteryVoltage() {
  int vBatt = 0;
  const int SAMPLES = 100;
  
  for (int i = 0; i < SAMPLES; i++) {
    vBatt += analogRead(35);
    delay(1);
  }
  
  vBatt /= SAMPLES;
  return ((float)vBatt - VBATT_MIN) / VBATT_RANGE + VBATT_MIN_VOLTAGE;
}

// This function generates and publishes CSV feed messages to Adafruit IO
// containing various parameters and the locations at which the parameters were measured.
// The Adafruit IO documentation on how to send data with location can be found
// at https://io.adafruit.com/api/docs/mqtt.html#mqtt-data-format .
bool updateParameter() {
  // Variables to hold GPS data
  float latitude = 0.0;
  float longitude = 0.0;
  float speed = 0.0;
  float altitude = 0.0;

  static int errorCount;

  // Get GPS data
  //Serial1.readStringUntil('\n');
  Serial.println("Getting GPS data...");
  if (modem.getGPS(&latitude, &longitude, &speed, &altitude)) {
    Serial.println("GPS data acquired...");
  } else {
    Serial.println("Failed to get GPS data");
    errorCount++;
    // Don't publish anything but don't restart yet either
    return true;
  }

  if ( (latitude == 0.0) && (longitude == 0.0) ) {
    Serial.println("But the latitude and longitude values aren't valid :-(");
    errorCount++;
    // Don't publish anything but don't restart yet either
    return true;
  }

  // Get RSSNR
  Serial1.flush();
  Serial1.println("AT+CPSI?");
  char delimiters[2] = ",";

  char result[6] = "";
  strcpy(result, getResponse(14, delimiters, true, 2000));
  int rssnr = atoi(result);

  char buf1[BUFSIZE1] = "";
  snprintf(buf1, BUFSIZE1, "RSSNR = %d, lat = %2.6f, lon = %3.6f, alt = %4.1f", rssnr, latitude, longitude, altitude);

  // Prepare MQTT publish content for RSSNR
  // Refer to the SIMCom SIM7000 MQTT appnote to see how to do this.
  // First you issue the command, then the SIM7000 prompts for the data to publish.
  char buf2[BUFSIZE2] = "";
  int size2 = snprintf(buf2, BUFSIZE2, "%d,%2.6f,%3.6f,%4.1f", rssnr, latitude, longitude, altitude);
  strncpy(buf1, "", sizeof(buf1));
  snprintf(buf1, BUFSIZE1, "AT+SMPUB=\"%s/feeds/%s/csv\",\"%d\",1,1", mqttUser, mqttFeed1, size2);

  // Send MQTT publish
  Serial1.flush();
  Serial1.println(buf1);
  Serial.println(buf1);
  if (!waitForPromptOrError(5000)) {
    Serial.println("Error publishing first line of " + String(mqttFeed1));
    errorCount++;
  } 
  else {
    Serial1.flush();
    Serial1.println(buf2);
    Serial.println(buf2);
  }

  if (!waitForOKorError(5000)) {
    Serial.println("Error publishing  " + String(mqttFeed1));
    errorCount++;
  }

  delay(1000);

  // Prepare MQTT publish content for speed
  speed = speed * KMPHTOMPH;
  strcpy(buf2, "");
  size2 = snprintf(buf2, BUFSIZE2, "%3.2f,%2.6f,%3.6f,%4.1f", speed, latitude, longitude, altitude);
  strncpy(buf1, "", sizeof(buf1));
  snprintf(buf1, BUFSIZE1, "AT+SMPUB=\"%s/feeds/%s/csv\",\"%d\",1,1", mqttUser, mqttFeed2, size2);

  // Send MQTT publish
  Serial1.flush();
  Serial1.println(buf1);
  Serial.println(buf1);
  if (!waitForPromptOrError(5000)) {
    Serial.println("Error publishing first line of  " + String(mqttFeed2));
    errorCount++;
  } 
  else {
    Serial1.flush();
    Serial1.println(buf2);
    Serial.println(buf2);
  }

  if (!waitForOKorError(5000)) {
    Serial.println("Error publishing  " + String(mqttFeed2));
    errorCount++;
  }

  delay(1000);

  // Prepare MQTT publish content for VBatt
  // Average vBatt over 100 readings taken 1 ms apart
  float vBattF = readBatteryVoltage();

  strcpy(buf2, "");
  size2 = snprintf(buf2, BUFSIZE2, "%1.2f,%2.6f,%3.6f,%4.1f", vBattF, latitude, longitude, altitude);
  strncpy(buf1, "", sizeof(buf1));
  snprintf(buf1, BUFSIZE1, "AT+SMPUB=\"%s/feeds/%s/csv\",\"%d\",1,1", mqttUser, mqttFeed3, size2);

  // Send MQTT publish
  Serial1.flush();
  Serial1.println(buf1);
  Serial.println(buf1);
  if (!waitForPromptOrError(5000)) {
    Serial.println("Error publishing first line of  " + String(mqttFeed3));
    errorCount++;
  } 
  else {
    Serial1.flush();
    Serial1.println(buf2);
    Serial.println(buf2);
  }

  if (!waitForOKorError(5000)) {
    Serial.println("Error publishing  " + String(mqttFeed3));
    errorCount++;
  }

  if (errorCount > 2) {
    Serial.println("Errors detected when publishing data, will be attempting to reconnect to MQTT...");
    return false;
  }

  Serial.println();
  return true;
}

// This function is useful for looking at whatever serial data comes out of the SIM7000.
void printResponse(int delay) {
  t_zero = millis();
  while (millis() < t_zero + delay) {
    if (Serial1.available()) {       // If anything comes in Serial1 (pins 0 & 1)
      Serial.write(Serial1.read());  // read it and send it out Serial (USB)
    }
  }
}

bool attemptReconnect(unsigned long timeout_ms) {
  unsigned long t_start = millis();

  Serial.println("Reactivating network connection...");

  while (millis() < t_start + timeout_ms) {
    // Attempt to re-activate the network
    // Re-activate the cellular connection with the APN
    Serial1.flush();
    Serial1.println("AT+CNACT=1,\"hologram\"");
    if (!waitForOKorError(5000)) {
      Serial.println("Failed to activate network connection.");
      continue;
    }

    // Optionally, check network status
    Serial1.println("AT+COPS?");
    printResponse(2000);  // Just for debugging purposes

    // Now attempt to reconnect to the MQTT broker
    Serial.println("Reconnecting to MQTT broker...");
    Serial1.flush();
    Serial1.println("AT+SMCONN");

    if (waitForOKorError(10000)) {
      return true;
    }
  }

  // If we reach here, timeout has occurred
  Serial.println("Reconnect attempt timed out, restarting board...");
  restartBoard();  // Call to the existing restart function

  return false;  // to make the compiler happy that there's a return value from a bool function
}

void restartBoard(void) {
  Serial.println("Attempting to restart SIM7000 and ESP32");

  // first power down the SIM7000G
  Serial1.println("AT+CPOWD=1");

  // Now restart the ESP32
  ESP.restart();
  // The SIM7000 will be powered up in setup()
}

void powerDownBoard(void) {
  #define WAKEUP_PIN GPIO_NUM_34
  // first power down the SIM7000G
  Serial1.println("AT+CPOWD=1");

  // Now put the ESP32 in Deep Sleep
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 1);  // Wake up on rising edge
  // Start deep sleep
  esp_deep_sleep_start();
}

void setup() {
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, LOW);

  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(2000); // Allow time for serial monitor connection

  Serial.println("ESP32 is awake.");

  // Power cycle the SIM7000G
  // Note that there is an inverter between the ESP32 output and the SIM7000
  // PWRKEY pin.
  Serial.println("Powering up SIM7000...");
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(1250);
  // The SIM7000 needs the PWRKEY pin to be pulsed low for at least 1 s
  digitalWrite(PWR_PIN, LOW);
  delay(1250);
  digitalWrite(PWR_PIN, HIGH);
  // According to the datasheet the SIM7000 takes 4.5 seconds to become active after powerup
  delay(5000);

  // Wait for the SIM7000 to wake up
  if (areWeAwakeYet()) {
    Serial.println("SIM7000 woke up!  Yay!");
  } else {
    Serial.println("SIM7000 never woke up.  Groan.");
  }

  bringMQTTOnline();

  // +CPSI? is a good command for looking at network connection status
  Serial1.flush();
  Serial1.println("AT+CPSI?");
  printResponse(2000);

  t_zero = millis();
  t_one = millis();
  t_two = millis();

  digitalWrite(LEDPIN, HIGH);
}

void loop() {
  //The following two "if" statements are useful for manual control
  //of the SIM7000.
  // if (Serial.available()) {      // If anything comes in Serial (USB),
  //   Serial1.write(Serial.read());   // read it and send it out Serial1 (pins 0 & 1)
  // }

  // if (Serial1.available()) {     // If anything comes in Serial1 (pins 0 & 1)
  //   Serial.write(Serial1.read());   // read it and send it out Serial (USB)
  // }

  // Check the connection status every CONNECTION_CHECK_INTERVAL seconds
  if (millis() > t_zero + CONNECTION_CHECK_INTERVAL) {
    t_zero = millis();
    // Check the MQTT connection status
    Serial1.flush();
    Serial1.println("AT+SMSTATE?");
    char delimiters[3] = ":\n";

    char result[32] = "";
    strcpy(result, getResponse(2, delimiters, true, 2000));

    Serial.print("MQTT state response token = ");
    Serial.println(result);

    if ((strstr(result, "0") != NULL)) {
      Serial.println("MQTT connection lost, attempting to recconnect, if that fails, restarting!");
      if (!attemptReconnect(RECONNECT_TIMEOUT)) {
        restartBoard();
      }
    }
  }

  // Send a report every MQTT_PUBLISH_INTERVAL seconds (I like prime numbers)
  if (millis() > t_one + MQTT_PUBLISH_INTERVAL) {
    t_one = millis();
    digitalWrite(LEDPIN, LOW);
    if (!updateParameter()) {
      if (!attemptReconnect(RECONNECT_TIMEOUT)) {
        restartBoard();
      }
    }

    //Serial.println(String(analogRead(35)));  // battery voltage
    digitalWrite(LEDPIN, HIGH);
  }

  // On USB, battery voltage typically reads 0.45V; on battery, reads actual voltage
  // If the battery voltage drops below 3.0 VDC, shut down the board to
  // prevent battery undervoltage, which can damage the battery
  float batteryVoltage = 0.0;
  if (millis() > t_two + BATTERY_CHECK_INTERVAL) {
    t_two = millis();
    batteryVoltage = readBatteryVoltage();
    if ( (batteryVoltage > 1.0) && (batteryVoltage < VBATT_MIN_VOLTAGE) ) {
      Serial.println("Powering down board, battery voltage = " + String(batteryVoltage) );
      powerDownBoard();
    }
  }
}
