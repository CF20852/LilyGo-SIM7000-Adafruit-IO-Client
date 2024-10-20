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

// set up a 45-second watchdog timer to restart if error or lost connection
#define WDT_TIMEOUT 45000

#define CONFIG_FREERTOS_NUMBER_OF_CORES 2
esp_task_wdt_config_t twdt_config = {
  .timeout_ms = WDT_TIMEOUT,
  .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,  // Bitmask of all cores
  .trigger_panic = true,
};

// Defined the ESP32 pins that are connected to the SIM7000
#define PWR_PIN 4
#define PIN_RX 26
#define PIN_TX 27

#define TINY_GSM_MODEM_SIM7000

// Define some C string buffer sizes
#define BUFSIZE1 65
#define BUFSIZE2 35
#define BUFSIZE3 121

// MQTT details
// Sign up for a free Adafruit IO account.
// While signed into your account, you can click on the gold circle with the
// black key in the upper right corner of the Adafruit IO web page to get your
// IO_USERNAME (mqttUser[]) and IO_KEY (mqtt_Pass) and fill them in below.
// Also, you can change the mqttFeed names if you want.
const char* broker = "io.adafruit.com";

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

unsigned long t_zero, t_one;

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
    if (str == "OK\r") { return true; }
    if (str == "ERROR\r") { return false; }
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

    if (str == "> ") { return true; }
    if (str == "ERROR\r") { return false; }
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

// When the SIM7000 first comes out of reset, it spits out what may
// be some Chinese characters before it starts responding to AT commands
// with an "OK" response.  This function waits for the "OK".
bool areWeAwakeYet() {
  // Send the modem "AT".  Read the string from the SIM7000.  If it contains "OK",
  // the SIM7000 is awake.
  unsigned long t_0 = millis();

  // Give it a minute to wake up
  while (millis() < t_0 + 60000) {
    Serial1.println("AT");
    while (Serial1.available() == 0) {}  //wait for data available
    String response = Serial1.readString();

    char resp[BUFSIZE1] = "";
    response.toCharArray(resp, BUFSIZE1);

    if (strstr(resp, "OK") != NULL) {
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

  Serial1.println("ATE1");

  if (!waitForOKorError(2000)) {
    Serial.println("SIM7000 didn't respond with \"OK\" to \"ATE1\"");
    return false;
  } else {
    Serial.println("SIM7000 responded to \"ATE1\" with \"OK\"");
  }

  // Turn on GPS power
  Serial.println("Enabling GPS power...");

  Serial1.println("AT+SGPIO=0,4,1,1");

  if (waitForOKorError(2000)) {
    Serial.println("GPS power supply enabled!");
  }
  else {
    Serial.println("Problem enabling GPS power!");
  }

  delay(3000);

  if (!modem.enableGPS()) {
    Serial.println("Failed to enable GPS");
    // Continue anyway, but the results won't be spectacular
  }

  // AT+SAPBR=3,1,"APN","hologram"  Use the Hologram SIM for the APN
  Serial1.println("AT+SAPBR=3,1,\"APN\",\"hologram\"");
  printResponse(1000);

  // AT+CNMP=38  set preferred connection type to LTE
  Serial1.println("AT+CNMP=51");
  printResponse(1000);

  // AT+CMNB=1  set preferred connection type to CAT-M1, which supports tower handoff,
  // unlike NB-IoT.  NB-IoT is better suited to stationary devices.
  Serial1.println("AT+CMNB=1");
  printResponse(2000);

  // AT+CIPMUX=1
  Serial1.println("AT+CIPMUX=0");  // start a single IP connection
  printResponse(1000);

  // AT+CSTT="hologram","",""
  Serial1.println("AT+CSTT=\"hologram\",\"\",\"\"");
  printResponse(1000);

  // AT+CNACT=1,"hologram"  Make the network active using the supplied APN (I think)
  Serial1.println("AT+CNACT=1,\"hologram\"");
  printResponse(1000);

  // AT+COPS?"  query network information
  // a response like +COPS: 0,0,"AT&T Hologram",7 or +COPS: 0,0,"T-Mobile Hologram",7 seems good
  // the '7' in the response means User-specified LTE M1 A GB access technology
  // a '9' in the response would mean User-specified LTE NB S1 access technology
  Serial1.println("AT+COPS?");
  printResponse(1000);

  // AT+CGNAPN  query CAT-M1 or NB-IoT network after the successful registration of APN
  // Basically, if we get a response like +CGNAPN: 1,"hologram" back, we're connected
  Serial1.println("AT+CGNAPN");
  printResponse(1000);

  // AT+CGNSHOR=10  set the desired GNSS accuracy to 10 meters
  Serial1.println("AT+CGNSHOR=10");
  printResponse(1000);

  // AT+SMCONF="URL","io.adafruit.com"
  Serial1.println("AT+SMCONF=\"URL\",\"io.adafruit.com\"");
  printResponse(1000);

  // AT+SMCONF="USERNAME","<your Adafruit IO IO_USERNAME>"
  sprintf(buf, "AT+SMCONF=\"USERNAME\",\"%s\"", mqttUser);
  Serial1.println(buf);
  printResponse(1000);

  // AT+SMCONF="PASSWORD","<your Adafruit IO IO_KEY>"
  sprintf(buf, "AT+SMCONF=\"PASSWORD\",\"%s\"", mqttPass);
  Serial1.println(buf);
  printResponse(1000);

  // AT+SMCONN
  Serial1.println("AT+SMCONN");
  if (!waitForOKorError(10000)) {
    Serial.println("SIM7000 didn't connect to MQTT broker :-(");
    return false;
  } else {
    Serial.println("SIM7000 is connected to MQTT broker!");
  }
  return true;
}

// This function generates and publishes CSV feed messages to Adafruit IO
// containing various parameters and the locations at which the parameters were measured.
// The Adafruit IO documentation on how to send data with location can be found
// at https://io.adafruit.com/api/docs/mqtt.html#mqtt-data-format .
void updateParameter() {
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
    Serial.println("GPS data acquired");
  } else {
    Serial.println("Failed to get GPS data");
    errorCount++;
  }

  // Get RSSNR
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
  snprintf(buf1, BUFSIZE1, "AT+SMPUB=\"cf20855/feeds/%s/csv\",\"%d\",1,1", mqttFeed1, size2);

  // Send MQTT publish
  Serial1.println(buf1);
  Serial.println(buf1);
  if (!waitForPromptOrError(5000)) {
    Serial.println("Error publishing first line of " + String(mqttFeed1));
  } else {
    Serial1.println(buf2);
    Serial.println(buf2);
  }

  if (!waitForOKorError(5000)) {
    Serial.println("Error publishing  " + String(mqttFeed1));
    errorCount++;
  }

  delay(1000);

  // Prepare MQTT publish content for speed
  strcpy(buf2, "");
  size2 = snprintf(buf2, BUFSIZE2, "%3.2f,%2.6f,%3.6f,%4.1f", speed, latitude, longitude, altitude);
  strncpy(buf1, "", sizeof(buf1));
  snprintf(buf1, BUFSIZE1, "AT+SMPUB=\"cf20855/feeds/%s/csv\",\"%d\",1,1", mqttFeed2, size2);

  // Send MQTT publish
  Serial1.println(buf1);
  Serial.println(buf1);
  if (!waitForPromptOrError(5000)) {
    Serial.println("Error publishing first line of  " + String(mqttFeed2));
  } else {
    Serial1.println(buf2);
    Serial.println(buf2);
  }

  if (!waitForOKorError(5000)) {
    Serial.println("Error publishing  " + String(mqttFeed2));
    errorCount++;
  }

  if (errorCount > 2) {
    Serial.println("Errors detected when publishing data, waiting for watchdog timeout");
    while (1) {}
  }

  delay(1000);

  // Prepare MQTT publish content for VBatt
  // Average vBatt over 100 readings taken 1 ms apart
  // 3.6 VDC ~ 1945; 4.2 VDC ~ 2315.
  int vBatt = 0;
  int i;
  
  for (i = 0; i < 100; i++) {
    vBatt += analogRead(35);
    delay(1);
  }

  vBatt = vBatt / i;
  float vBattF = ((float)vBatt - 1945.0) / 616.7 + 3.6;

  strcpy(buf2, "");
  size2 = snprintf(buf2, BUFSIZE2, "%1.2f,%2.6f,%3.6f,%4.1f", vBattF, latitude, longitude, altitude);
  strncpy(buf1, "", sizeof(buf1));
  snprintf(buf1, BUFSIZE1, "AT+SMPUB=\"cf20855/feeds/%s/csv\",\"%d\",1,1", mqttFeed3, size2);

  // Send MQTT publish
  Serial1.println(buf1);
  Serial.println(buf1);
  if (!waitForPromptOrError(5000)) {
    Serial.println("Error publishing first line of  " + String(mqttFeed3));
  } else {
    Serial1.println(buf2);
    Serial.println(buf2);
  }

  if (!waitForOKorError(5000)) {
    Serial.println("Error publishing  " + String(mqttFeed3));
    errorCount++;
  }

  if (errorCount > 2) {
    Serial.println("Errors detected when publishing data, waiting for watchdog timeout");
    while (1) {}
  }

  Serial.println();
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

void setup() {
  esp_task_wdt_deinit();            //wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&twdt_config);  //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);           //add current thread to WDT watch

  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);

  Serial.println("ESP32 is awake.");

  // Power cycle the SIM7000G
  // Note that there is an inverter between the ESP32 output and the SIM7000
  // PWRKEY pin.
  Serial.println("Powering up SIM7000...");
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(2500);
  digitalWrite(PWR_PIN, LOW);
  delay(1200);
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
  Serial1.println("AT+CPSI?");
  printResponse(2000);

  t_zero = millis();
  t_one = millis();
  esp_task_wdt_reset();
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

  // Check the connection status every 23 seconds
  if (millis() > t_zero + 23000) {
    t_zero = millis();
    // Check the MQTT connection status
    Serial1.println("AT+SMSTATE?");
    char delimiters[3] = ":\n";

    char result[32] = "";
    strcpy(result, getResponse(2, delimiters, true, 2000));

    Serial.print("MQTT state response token = ");
    Serial.println(result);

    if ((strstr(result, "0") != NULL)) {
      Serial.println("MQTT connection lost, letting watchdog timer timeout!");
      while (1) {}
    }
  }

  // Send a report every 29 seconds (I like prime numbers)
  if (millis() > t_one + 29000) {
    t_one = millis();
    updateParameter();
    esp_task_wdt_reset();
    Serial.println(String(analogRead(35)));
  }
}
