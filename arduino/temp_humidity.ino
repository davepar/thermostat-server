/*
*  Thermostat with Arduino, DHT11 temp/hum sensor, CC3000 wifi chip, and LCD display.
*  Part of the code is based on the work done by Adafruit on the CC3000 chip & the DHT11 sensor
*/

// Include required libraries
#include <avr/pgmspace.h>
#include <Adafruit_CC3000.h>
#include <SPI.h>
#include "DHT.h"
#include <LiquidCrystal.h>
#include <PinChangeInt.h>
#include <MemoryFree.h>

/*
*  Create a file called private.h containing:
*    #define WLAN_SSID       "mynetwork"    // Your wifi network name. Cannot be longer than 32 characters!
*    #define WLAN_PASS       "mypassword"   // Your wifi password.
*    #define WLAN_SECURITY   WLAN_SEC_WPA2  // This can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
*    #define KEY_ID          "mykey"        // A unique ID from the thermostat server at http://thermostat-server.appspot.com
*    #define KEY_PWD         "mypassword"   // The assigned password from the thermostat server.
*/
// Pull in private data
#include "private.h"

// Define CC3000 chip pins
#define ADAFRUIT_CC3000_IRQ   3
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10

// DHT11 sensor pins
#define DHTPIN 2
#define DHTTYPE DHT22

// Push button pins
#define TEMP_UP 8
#define TEMP_DOWN 9
#define HOLD_BTN 7

// Update intervals in milliseconds
#define UPDATE_INTERVAL 60000
#define UPDATE_AFTER_BUTTON 5000

prog_char host[] PROGMEM = "thermostat-server.appspot.com";
//prog_char host_path[] PROGMEM = "/postdata?id=" KEY_ID "&pwd=" KEY_PWD "&temp=";
prog_char host_path[] PROGMEM = "/postdata?id=" KEY_ID "&temp=";

// Timeout values
const unsigned long DHCP_TIMEOUT = 60L * 1000L; // Max time to wait for address from DHCP
const unsigned long DNS_TIMEOUT  = 15L * 1000L; // Max time to wait for DNS lookup
const unsigned long CONNECT_TIMEOUT = 15L * 1000L; // Max time to wait for a connection
const unsigned long RESPONSE_TIMEOUT = 10L * 1000L; // Max time to wait for data from server

// Create LCD
LiquidCrystal lcd(A5, A4, A3, A2, A1, A0);

// Degree symbol
byte degree[8] = {
  B01100,
  B10010,
  B10010,
  B01100,
  0,0,0
};

// Create CC3000 & DHT objects
DHT dht(DHTPIN, DHTTYPE);
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
int current_t = 0;
int current_h = 0;
volatile int set_temp = 70;
volatile boolean temp_hold = false;
boolean heat_on = false;

volatile unsigned long debounce_time = 0;
const int debounce_guard = 200;

volatile unsigned long last_update_time = 0;
volatile unsigned long update_interval = 0; // First time through update immediately
volatile boolean send_set_temp = false;

// Handle button push interrupt
void button_isr() {
  // First debounce the buttons to avoid spurious input
  unsigned long current_millis = millis();
  unsigned long debounce_elapsed = current_millis - debounce_time;
  if (debounce_elapsed > debounce_guard) {
    // Pretend we just updated in order to prevent updates for a few seconds to be more
    // responsive to repeated button presses.
    last_update_time = current_millis;
    update_interval = UPDATE_AFTER_BUTTON;
    send_set_temp = true;
    debounce_time = current_millis;

    // Get pushed button and increment or decrement temperature setting
    uint8_t int_pin = PCintPort::arduinoPin;
    if (int_pin == TEMP_UP) {
      set_temp++;
      set_temp = min(set_temp, 99);
    }
    if (int_pin == TEMP_DOWN) {
      set_temp--;
      set_temp = max(set_temp, 40);
    }
    if (int_pin == HOLD_BTN) {
      temp_hold = !temp_hold;
    }
  }
};

void setup(void)
{
  // Initialize LCD
  lcd.createChar(1, degree);
  lcd.begin(16, 2);
  lcd.print(F("Thermostat"));
  lcd.setCursor(0, 1);
  lcd.print(F("initializing..."));

  // Initialize push buttons and interrupts
  pinMode(TEMP_UP, INPUT_PULLUP);
  PCintPort::attachInterrupt(TEMP_UP, &button_isr, FALLING);
  pinMode(TEMP_DOWN, INPUT_PULLUP);
  PCintPort::attachInterrupt(TEMP_DOWN, &button_isr, FALLING);
  pinMode(HOLD_BTN, INPUT_PULLUP);
  PCintPort::attachInterrupt(HOLD_BTN, &button_isr, FALLING);

  // Initialize DHT sensor
  dht.begin();

  Serial.begin(115200);
  print_free_mem();
  Serial.print(F("Init connection..."));

  // Initialise the CC3000 module
  if (!cc3000.begin()) {
    fatal_error(F("begin"));
  }

  setup_cc3000();

  lcd.clear();
}

void setup_cc3000(void) {
  // Connect to  WiFi network
  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    fatal_error(F("connect"));
  }
  Serial.println(F("Connected to WiFi!"));

  // Check status of DHCP
  Serial.println(F("Request DHCP"));
  unsigned long start_time = millis();
  while (!cc3000.checkDHCP() && (millis() - start_time) < DHCP_TIMEOUT) {
    delay(1000);
  }
  if (!cc3000.checkDHCP()) {
    fatal_error(F("checkDHCP"));
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
    delay(1000);
  }
  if (ip == 0L) {
    fatal_error(F("getHost"));
  }
  cc3000.printIPdotsRev(ip);
  Serial.println();
  print_free_mem();
}

void loop(void)
{
  boolean send_data = false;
  // Determine if update is needed
  unsigned long current_time = millis();
  if ((current_time - last_update_time) >= update_interval) {
    send_data = true;
    // Update time value
    last_update_time = current_time;
    // Set the update interval back to the default
    update_interval = UPDATE_INTERVAL;

    // Measure the humidity & temperature
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    // Convert to fahrenheit and integers that include tenths
    current_t = int((t * 9 / 5 + 32) * 10);
    current_h = int(h * 10);
  }

  // Update LCD every time through loop in case user changed the set temp or hold
  int idx = 0;
  strcpy(buf, "Set ");
  idx += 4;
  itoa(set_temp, &buf[idx], 10);
  idx = strlen(buf);
  buf[idx++] = char(1);
  if (temp_hold) {
    strcpy(&buf[idx], " hold    ");
  } else {
    strcpy(&buf[idx], "         ");
  }
  buf[15] = heat_on ? '*' : ' ';

  // For debugging
  // itoa(set_temp, &buf[idx], 10);
  // idx = strlen(buf);
  // buf[idx++] = char(1);
  // buf[idx++] = ' ';
  // itoa(freeMemory(), &buf[idx], 10);
  // idx = strlen(buf);
  // buf[idx++] = ' ';
  // itoa(loop_count, &buf[idx], 10);

  lcd.setCursor(0, 0);
  lcd.print(buf);

  // Second line of LCD
  strcpy(buf, "Cur ");
  idx = 4;
  itoa((current_t + 5) / 10, &buf[idx], 10);
  idx = strlen(buf);
  buf[idx++] = char(1);
  buf[idx++] = ' ';
  buf[idx++] = ' ';
  // Temp could be -99 to 150, but always start humidity at same spot
  idx = 9;
  strcpy(&buf[idx], "Hum ");
  idx += 4;
  itoa((current_h + 5) / 10, &buf[idx], 10);
  idx = strlen(buf);
  buf[idx++] = '%';
  buf[idx++] = 0;

  lcd.setCursor(0, 1);
  lcd.print(buf);

  // Update server
  if (send_data) {
    // Send request
    strcpy(buf, "GET http://");
    strcat_P(buf, host);
    strcat_P(buf, host_path);
    idx = strlen(buf);
    itoa(current_t, &buf[idx], 10);
    strcat(buf, "&hum=");
    idx = strlen(buf);
    itoa(current_h, &buf[idx], 10);
    if (send_set_temp) {
      send_set_temp = false;
      strcat(buf, "&set=");
      idx = strlen(buf);
      itoa(set_temp * 10, &buf[idx], 10);
      strcat(buf, "&hold=");
      idx = strlen(buf);
      buf[idx++] = temp_hold ? 'y' : 'n';
      buf[idx++] = 0;
    }
    strcat(buf, " HTTP/1.0");
    send_request(buf);
  }

  delay(1);
}

// Function to send a TCP request and get the result as a string
void send_request (char *request) {
  int c = 0;
  int t = 0;

  Serial.println(F("Sending:"));
  Serial.println(request);

  // Connect
  Serial.println(F("Connecting to server..."));
  unsigned long start_time = millis();
  do {
    wifi_client = cc3000.connectTCP(ip, 80);
  }
  while (!wifi_client.connected() && (millis() - start_time) < CONNECT_TIMEOUT);

  // Send request
  if (!wifi_client.connected()) {
    Serial.println(F("Connection failed, reseting"));
    cc3000.reboot();
    setup_cc3000();
    goto final;
  }

  wifi_client.println(request);
  wifi_client.println(F("Connection: close"));
  wifi_client.println(F(""));
  Serial.println(F("Connected & Data sent"));

  // Skip over HTTP response headers
  int result;
  while ((result = readString('\n', 0, 0)) != 0) {
    if (result == -1) {
      Serial.println(F("Reading headers failed"));
      goto final;
    }
  }
  Serial.println(F("Headers skipped"));

  // Read set temperature
  if(readString(',', buf, 5) < 2) {
    Serial.println(F("Read set temp failed"));
    goto final;
  }
  t = atoi(buf);
  if (t > 0) {
    set_temp = t / 10;
  }
  Serial.print(F("Got temp: "));
  Serial.print(t / 10);

  // Get hold value
  c = timedRead();
  if (c < 1) {
    Serial.println(F("Read hold failed"));
    goto final;
  }
  temp_hold = (c == '1');
  Serial.print(F("  hold: "));
  Serial.print((char) c);

  // Get heat state
  // Skip comma
  timedRead();
  c = timedRead();
  if (c < 1) {
    Serial.println(F("Read heat failed"));
    goto final;
  }
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
void fatal_error(String msg) {
  lcd.clear();
  lcd.print("Error:");
  lcd.setCursor(0, 1);
  lcd.print(msg);
  while (true) ;
}

void print_free_mem() {
  Serial.print(F("Free memory: "));
  Serial.println(freeMemory());
}

