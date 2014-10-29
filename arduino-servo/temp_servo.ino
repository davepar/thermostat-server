/*

Arduino code for controlling a servo via calls to an HTTP
servo with the CC3000 wifi chip.

*/

// Include required libraries
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <Adafruit_CC3000.h>
#include <SPI.h>
#include <Servo.h>
#include <MemoryFree.h>

/*
*  Create a file called private.h containing:
*    #define WLAN_SSID       "mynetwork"    // Your wifi network name. Cannot be longer than 32 characters!
*    #define WLAN_PASS       "mypassword"   // Your wifi password.
*    #define WLAN_SECURITY   WLAN_SEC_WPA2  // This can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
*    #define KEY_ID          "mykey"        // A unique ID from the thermostat server at http://thermostat-server.appspot.com
*    #define KEY_TOKEN       "mytoken"      // The token assigned by the thermostat server.
*/
// Pull in private data
#include "private.h"

// Define CC3000 chip pins
#define ADAFRUIT_CC3000_IRQ   3
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10

// Update intervals in milliseconds
#define UPDATE_INTERVAL 30000

// Turn off heat after 5 minutes of no server contact
const unsigned long RESET_TIME = 10L * 60L * 1000L;
unsigned long last_server_contact = 0;

#define HOST "arduino-stat.appspot.com"
prog_char host[] PROGMEM = HOST;
prog_char host_hdr[] PROGMEM = "Host: " HOST;
prog_char host_path[] PROGMEM = "/getheat?id=" KEY_ID;

// Timeout values
const unsigned long DHCP_TIMEOUT = 15L * 1000L; // Max time to wait for address from DHCP
const unsigned long DNS_TIMEOUT  = 15L * 1000L; // Max time to wait for DNS lookup
const unsigned long RESPONSE_TIMEOUT = 10L * 1000L; // Max time to wait for data from server

// Servo constants
const int SERVO_PIN = 9;
const int SERVO_HEAT_ON = 600;
const int SERVO_HEAT_OFF = 2400;

// Create Servo and CC3000 objects
Servo myservo;
Adafruit_CC3000 cc3000 = Adafruit_CC3000(
  ADAFRUIT_CC3000_CS,
  ADAFRUIT_CC3000_IRQ,
  ADAFRUIT_CC3000_VBAT,
  SPI_CLOCK_DIV2
);
Adafruit_CC3000_Client wifi_client;

// String work area large enough to hold full server request
char buf[130];

// Local server IP
unsigned long ip = 0;

// State variables
int servo_pos = SERVO_HEAT_OFF;
boolean heat_on = false;

volatile unsigned long last_update_time = 0;
volatile unsigned long update_interval = 0; // First time through update immediately

// Forward declarations
void print_free_mem();
void fatal_error(const __FlashStringHelper* msg);
int timedRead(void);
int readString(int end_char, char *buf, int buf_len);
void send_request (char *request);

void setup(void)
{
  Serial.begin(115200);
  print_free_mem();
  Serial.print(F("Init connection..."));

  // Attach the servo on pin 9 to the servo object
  myservo.attach(SERVO_PIN, SERVO_HEAT_ON, SERVO_HEAT_OFF);
  myservo.writeMicroseconds(servo_pos);

  // Initialise the CC3000 module
  if (!cc3000.begin()) {
    fatal_error(F("begin"));
    while (true) ;
  }
  Serial.println(F("cc3000 initialized"));

  // Connect to  WiFi network
  while (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    fatal_error(F("connect"));
    cc3000.reboot(0);
  }
  Serial.println(F("Connected to WiFi!"));

  // Enable watchdog timer
  wdt_enable(WDTO_8S);

  // Check status of DHCP
  Serial.println(F("Request DHCP"));
  unsigned long start_time = millis();
  while (!cc3000.checkDHCP() && (millis() - start_time) < DHCP_TIMEOUT) {
    wdt_reset();
    delay(1000);
  }
  if (!cc3000.checkDHCP()) {
    fatal_error(F("checkDHCP"));
    // Trigger watch dog reset
    while (true) ;
  }

  // Look up server's IP address
  strcpy_P(buf, host);
  Serial.print(buf);
  Serial.print(F(" -> "));
  start_time = millis();
  while ((ip  ==  0L) && ((millis() - start_time) < DNS_TIMEOUT))  {
    cc3000.getHostByName(buf, &ip);
    if (ip != 0L) {
      break;
    }
    wdt_reset();
    delay(1000);
  }
  if (ip == 0L) {
    fatal_error(F("getHost"));
    // Trigger watch dog reset
    while (true) ;
  }
  cc3000.printIPdotsRev(ip);
  Serial.println();
  print_free_mem();
}

void loop(void)
{
  // Feed the watch dog
  wdt_reset();

  // Determine if update is needed
  unsigned long current_time = millis();
  if ((current_time - last_update_time) >= update_interval) {
    // Update time value
    last_update_time = current_time;
    // Set the update interval back to the default
    update_interval = UPDATE_INTERVAL;
    // Send request
    strcpy(buf, "GET http://");
    strcat_P(buf, host);
    strcat_P(buf, host_path);
    strcat(buf, " HTTP/1.0");
    send_request(buf);
  }

  // See if servo position needs to be updated
  int new_servo = heat_on ? SERVO_HEAT_ON : SERVO_HEAT_OFF;
  if (servo_pos != new_servo) {
    int delta = (new_servo - servo_pos) / 1800;
    while (new_servo != servo_pos) {
      servo_pos += delta;
      myservo.writeMicroseconds(servo_pos);
      wdt_reset();
      delay(5);
    }
  }

  // If there's been no update for a long time, trigger a watch dog reset
  current_time = millis();
  if ((current_time - last_server_contact) >= RESET_TIME) {
    while (true) ;
  }

  delay(1);
}

// Function to send a TCP request and get the result as a string
void send_request (char *request) {
  int c = 0;
  int t = 0;
  int result = 0;

  Serial.println(F("Sending:"));
  Serial.println(request);

  // Connect or let watchdog reset everything
  Serial.println(F("Connecting..."));
  unsigned long start_time = millis();
  do {
    wifi_client = cc3000.connectTCP(ip, 80);
  }
  while (!wifi_client.connected());

  Serial.println(F("Connection succeeded"));

  wifi_client.println(request);
  wifi_client.print(host_hdr);
  wifi_client.println(F("User-Agent: ArduinoWiFi/1.1"));
  wifi_client.println(F("Connection: close"));
  wifi_client.println();
  Serial.println(F("Connected & Data sent"));

  // Skip over HTTP response headers
  while ((result = readString('\n', 0, 0)) != 0) {
    if (result == -1) {
      Serial.println(F("Reading headers failed"));
      goto final;
    }
  }
  Serial.println(F("Headers skipped"));

  // Get heat state
  c = timedRead();
  if (c < 1) {
    Serial.println(F("Read heat failed"));
    goto final;
  }
  last_server_contact = millis();
  heat_on = (c == '1');
  Serial.print(F("  heat: "));
  Serial.println((char) c);

  final:
  Serial.println(F("Closing connection"));
  wifi_client.close();
  print_free_mem();
}

// Read string from WiFi and optionally store it.
// Returns length of string.
int readString(int end_char, char *buf, int buf_len) {
  int c;
  int idx = 0;
  int length = 0;
  while (true) {
    c = timedRead();
    if (c < 1) {
      return -1;
    }
    if (c == end_char) {
      break;
    }
    if (c == '\r') {
      // Skip carriage return
      continue;
    }
    if (buf && idx < (buf_len - 1)) {
      buf[idx++] = c;
    }
    length++;
  }
  if (buf) {
    // And null at end of string
    buf[idx++] = 0;
  }
  return length;
}

// Read from WiFi client stream with a timeout.
// Returns -1 on timeout.
int timedRead(void) {
  unsigned long start = millis();
  while((!wifi_client.available()) && ((millis() - start) < RESPONSE_TIMEOUT));
  return wifi_client.read();
}

// Write error message on LCD and loop indefinitely.
void fatal_error(const __FlashStringHelper* msg) {
  Serial.print(F("Fatal error: "));
  Serial.println(msg);
  strcpy_P(buf, (const prog_char*) msg);
}

void print_free_mem() {
  Serial.print(F("Free memory: "));
  Serial.println(freeMemory());
}
