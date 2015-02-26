//#define CC3000_TINY_DRIVER
//#define ECHO_ON
//#define PRINT_POST_RESULT
#define HAS_DUST_SENSOR

#ifdef HAS_DUST_SENSOR
#include <SharpDust.h>
#define DUST_LED_PIN 2
#define DUST_MEASURE_PIN 0
#endif

#include <Adafruit_cc3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <sha1.h>
#include <DHT.h>
#include <avr/wdt.h>
#include <EEPROM.h>

/* sensor type definition */
#define SENSOR_THERMAL_ID 0
#define SENSOR_HUMID_ID 1
#define SENSOR_CO2_ID 2
#define SENSOR_DUST_ID 3

/* sensor definitions and variables */
#define DHTPIN 7
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* wifi definitions and variables */
#define ADAFRUIT_CC3000_IRQ   3
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10
#define IDLE_TIMEOUT_MS  5000

Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT, SPI_CLOCK_DIV2);

/* wifi connection definitions and variables */
#define MAX_SSID 33
#define MAX_PASSWD 20

/* web connection definitions and variables */
#define SERVER_NAME "galvanic-cirrus-841.appspot.com"
#define SERVER_PORT 80
#define INPUT_PAGE "/sensor/input/"

char msgHeader[100];
char postData[40];
int firstReport = 1;

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
void readEeprom(char * ssid, char * passwd, char *security) {
  int i;

  Serial.println(F("reading EEPROM.."));
  for (i = 0; i < MAX_SSID - 1; i++) {
    ssid[i] = EEPROM.read(i);
  }
  ssid[MAX_SSID-1] = 0;
  for (i = 0; i < MAX_PASSWD - 1; i++) {
    passwd[i] = EEPROM.read(i + MAX_SSID);
  }
  passwd[MAX_PASSWD-1] = 0;
  *security = EEPROM.read(MAX_SSID + MAX_PASSWD);
}

void writeEeprom(char * ssid, char * passwd, char security) {
  int i;

  Serial.println("Writing EEPROM");

  for (i = 0; i < MAX_SSID; i++) {
    EEPROM.write(i, ssid[i]);
  }
  for (i = 0; i < MAX_PASSWD; i++) {
    EEPROM.write(i + MAX_SSID, passwd[i]);
  }
  EEPROM.write(MAX_SSID + MAX_PASSWD, security);
}

/* ugly ugly */
/* get the line input : the buffer should be big enough to handle \r \n \0 */
void getLineInput(char * buffer, int len) {
  int i = 0;

  for (i = 0; i<len - 1; i++) {
    byte bytes;
    do {
      bytes = Serial.readBytes(&buffer[i], 1);
    } 
    while (bytes == 0);
    if (buffer[i] == '\r' || buffer[i] == '\n') {
      char trashTrail;
#ifdef ECHO_ON
      Serial.println();
#endif
      buffer[i] = 0;
      Serial.setTimeout(100);
      Serial.readBytes(&trashTrail, 1);  //remove trailing \r or \n
      return;
    }
#ifdef ECHO_ON
    Serial.print(buffer[i]);
#endif
  }
  buffer[i] = 0;
#ifdef ECHO_ON
  Serial.println();
#endif
}

void getInput() {
  int i;
  char sec[3];
  
  char ssid[MAX_SSID];
  char passwd[MAX_PASSWD];
  char security;


  Serial.print(F("SSID : "));
  getLineInput(ssid, MAX_SSID);

  do {
    Serial.print(F("Security : "));
    getLineInput(sec, 3);
    security = sec[0] - '0';
  } while (security > 4 || security < 0);
  
  if (security != 0) {
    Serial.print(F("Password : "));
    getLineInput(passwd, MAX_PASSWD);
  }
  
  writeEeprom(ssid, passwd, security);
}

int buildSecureKey(char* macString, char* secureKey) {
  uint8_t* hash;
  int i;

  Sha1.init();
  Sha1.print("owen77");
  Sha1.print(macString);
  Sha1.print("young");
  
  hash = Sha1.result();

  for (i = 0; i < 20; i++) {
    secureKey[i*2] = "0123456789abcdef"[hash[i]>>4];
    secureKey[i*2+1] = "0123456789abcdef"[hash[i]&0xf];
  }
  secureKey[i*2] = 0;

  return 0;
}

int buildMacString(byte* mac, char* macString, byte sec) {
  int i;
  int stringIndex = 0;

  for (i = 0; i < 6; i++) {
    macString[stringIndex++] = "0123456789abcdef"[mac[i]>>4];
    macString[stringIndex++] = "0123456789abcdef"[mac[i]&0xf];
    if (i != 5) {
      if (!sec) {
        macString[stringIndex++] = '-';
      } 
      else {
        macString[stringIndex++] = '%';
        macString[stringIndex++] = '3';
        macString[stringIndex++] = 'A';
      }
    }
  }
  macString[stringIndex] = 0;

  return 0;
}

int buildMsgHeader() {
  byte mac[6];
  char macString[18];
  char macEncString[28];
  char secureKey[41];
  
  cc3000.getMacAddress(mac);
  buildMacString(mac, macString, 0);
  Serial.print(F("mac: "));
  Serial.println(macString);
  buildMacString(mac, macEncString, 1);
  buildSecureKey(macString, secureKey);
  
  sprintf(msgHeader, "secure_key=%s&mac_address=%s", secureKey, macEncString);
  
  return 0;
}

int connectAp(byte trials) {
  char ssid[MAX_SSID];
  char passwd[MAX_PASSWD];
  char security;
  wdt_disable();
  readEeprom(ssid, passwd, &security);

  Serial.println(F("connecting to AP.. "));
  if (!cc3000.begin())
  {
    Serial.println(F("wiring problem?"));
    while(1);
  }
  
  cc3000.setDHCP();
  
  if (!cc3000.connectToAP(ssid, passwd, security, trials)) {
    Serial.println(F("AP connected 0"));
    return -1;
  }
  while (!cc3000.checkDHCP())
  {
    delay(100);
  }
  delay(500);
  Serial.println(F("AP connected 1"));
  wdt_enable(WDTO_8S);

  return 0;
}

void scanNetworks() {
#ifndef CC3000_TINY_DRIVER
  uint32_t index;
  uint8_t valid, rssi, sec;
  char b;
  char ssid[MAX_SSID];
  
  if (!cc3000.begin())
  {
    Serial.println(F("wiring problem?"));
    while(1);
  }
  
  if(!cc3000.startSSIDscan(&index)){
    Serial.println(F("SSID scan failed!"));
    return;
  }
  
  Serial.println("Scan result : ");
  
  while(index){
    index --;
    
    valid = cc3000.getNextSSID(&rssi, &sec, ssid);
    Serial.println(ssid);
    Serial.print("-");
    Serial.println(128 - rssi);
    Serial.println(sec);
  }
  
  cc3000.stopSSIDscan();
  
  Serial.setTimeout(0xffffff);
  Serial.readBytes(&b, 1);
#endif
}

void setup() {
  int i;
  int firstTrial = 1;
  char tempInput[1];
  byte connected = 0;

  wdt_disable();

  Serial.begin(9600);
  
  /* check if user want update the input */
  if (Serial) {
    Serial.println(F("Press 'c' key to update AP settings : "));
    Serial.setTimeout(5000);
    if (Serial.readBytes(tempInput, 1)) {
      if (tempInput[0] == 'c') {
        do {
          Serial.println(F("Press 'a' to set AP settings, Press 's' to scan APs : "));
          Serial.setTimeout(0xffffff);
          if (Serial.readBytes(tempInput, 1)) {
            if (tempInput[0] == 'a') {
              getInput();
              if (!connectAp(2)) {
                connected = 1;
                break;
              }
              buildMsgHeader();
            } 
            else if (tempInput[0] == 's') {
              scanNetworks();
            } 
            else if (tempInput[0] == 'b') {
              break;
            }
          }
        }
        while (! connected);
      } 

    }
  }

  if (!connected) {
    while (connectAp(2)) {
        /* read AP connections settings */  
      wdt_enable(WDTO_8S);
      firstTrial = 0;
      while (!Serial) {
        ;
      }
      getInput();
    }
  }

  /* read AP connections settings */  
  wdt_enable(WDTO_8S);
  
  /* initialize thermal/humidity sensor */
  dht.begin();
  
#ifdef HAS_DUST_SENSOR
  SharpDust.begin(DUST_LED_PIN, DUST_MEASURE_PIN);
#endif

  /* build post message prefix from MAC string */
  buildMsgHeader();

  /* set first report flag */
  firstReport = 1;
}

#define ERROR_VAL 9874

enum parseStatus {
  NONE_STATUS,
  PLUS_STATUS,
  VALUE_STATUS,
  EQUAL_STATUS,
  EXIT_STATUS,
};

byte postPage(char* thisData, char* thatData, int val[3])
{
  byte ret, isSigned, valIndex, i;
  int * pVal;
  int status;
  byte rx_byte = 0;
  enum parseStatus parState = NONE_STATUS;
  Adafruit_CC3000_Client www;
  uint32_t ip = 0;
  
  while (ip == 0) {
    if (! cc3000.getHostByName(SERVER_NAME, &ip)) {
      Serial.println(F("Couldn't resolve!"));
    }
    delay(500);
  }

  wdt_reset();
  www = cc3000.connectTCP(ip, SERVER_PORT);
  wdt_reset();
  
  if(www.connected())
  {
    char length_buffer[10];
    Serial.println(F("connected"));
    wdt_reset();
    
    www.fastrprintln(F("POST " INPUT_PAGE " HTTP/1.1"));
    www.fastrprint(F("Host: "));
    www.fastrprintln(F(SERVER_NAME));
    www.fastrprintln(F("Connection: close"));
    www.fastrprint(F("Content-Type: application"));
    www.fastrprintln(F("/x-www-form-urlencoded"));
    www.fastrprint(F("Content-Length: "));
    sprintf(length_buffer, "%u", strlen(thisData) + strlen(thatData));
    www.fastrprintln(length_buffer);
    www.fastrprintln(F(""));
        
    int dataLen = strlen(thisData);
    char * pos = thisData;
    int written;
    do {
      written = www.write(pos, 10);
      dataLen -= written;
      pos += written;
      delay(10);
    }
    while (dataLen > 10);
    www.write(pos, dataLen);
    
    dataLen = strlen(thatData);
    pos = thatData;
    do {
      written = www.write(pos, 10);
      dataLen -= written;
      pos += written;
      delay(10);
    }
    while (dataLen > 10);
    www.write(pos, dataLen);
    
    www.fastrprintln(F(""));
  }
  else
  {
    Serial.print(F("failed"));
    Serial.println(status);
    return 0;
  }

  Serial.print(F("posted : "));
  Serial.print(strlen(thisData) + strlen(thatData));
  Serial.println(F(" bytes"));
  wdt_reset();

  int connectLoop = 0;

  unsigned long lastRead = millis();
  valIndex = 0;
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    wdt_reset();
    while (www.available()) {
      char c = www.read();
#ifdef PRINT_POST_RESULT
      Serial.print(c);
#endif
      rx_byte ++;
      lastRead = millis();

      if (parState != EXIT_STATUS && c == '+') {
        isSigned = 0;
        parState = VALUE_STATUS;
        pVal = &val[valIndex++];
        *pVal = 0;
      } 
      else if (parState == VALUE_STATUS) {
        if (c == '-') {
          isSigned = 1;
        } 
        else if (c == 'z') {
          //reset the device
          while(true);
        } 
        else if (c == 'e') {
          *pVal = ERROR_VAL;
          parState = EXIT_STATUS;
        }
        else if (c == 'n') {
          *pVal = ERROR_VAL;
          parState = EQUAL_STATUS;
        } 
        else if (c == '=') {
          if (isSigned) *pVal = - *pVal;
          parState = EQUAL_STATUS;
        } 
        else if (c == '.') {
          parState = EQUAL_STATUS;
        }
        else {
          *pVal *= 10;
          *pVal += c - '0';
        }
     } 
    }
  }
  www.close();
  Serial.print(F("read : "));
  Serial.print(rx_byte);
  Serial.println(F(" bytes"));
  wdt_reset();
  
  return 1;
}

/* report data through POST and get the setting values */
int report_data(int sensor_type, float value, unsigned long * report_period, int* high_threshold, int* low_threshold) {
  int ret;
  int val[3] = {
    ERROR_VAL, ERROR_VAL, ERROR_VAL    };
  int rssi = -60;
  
  wdt_reset();
  if (!cc3000.checkConnected()) {
    Serial.println(F("disconnected"));
    while(1);
  }
  wdt_reset();
  sprintf(postData, "&type=%d&value=%d&rssi=%d&first=%d", sensor_type, (int)(value*10.0), rssi, firstReport);
  ret = !postPage(msgHeader, postData, val);
  wdt_reset();

  if (ret) {
    return -1;
  }

  firstReport = 0;

  if (val[0] == ERROR_VAL) {
    Serial.println(F("Getting settings failed"));
    return -1;
  }
  *report_period = val[0];
  *high_threshold = val[1];
  *low_threshold = val[2];
  if (*high_threshold == ERROR_VAL) *high_threshold = 10000;
  if (*low_threshold == ERROR_VAL) *low_threshold = -10000;

  Serial.println(F("cfg"));
  Serial.println(sensor_type);
  Serial.println(*report_period);
  Serial.println(*high_threshold);
  Serial.println(*low_threshold);

  return ret;
}

int outRange(float value, int low, int high)
{
  int intVal = (int)(value + 0.5);

  if (intVal < low) return 1;
  if (intVal > high) return 1;
  return 0;
}

void loop() {
  int lowTh1, highTh1, lowTh0, highTh0;
#ifdef HAS_DUST_SENSOR
  int lowTh2, highTh2;
#endif
  unsigned long reportPeriod = 600;
  float value;
  unsigned long first, last, target;

  /* start Watch dog. */
  wdt_enable(WDTO_8S);

  while(true) {
    first = millis();
    Serial.println();
    Serial.print(F("TS measure:")); 
    Serial.println(first);

    wdt_reset();
    dht.read();
    wdt_reset();
    value = dht.readTemperature();
    wdt_reset();
    Serial.println();
    Serial.print(F("TS temper report:"));
    Serial.println(millis());
    Serial.println(value);
    if (value != NAN && !report_data(SENSOR_THERMAL_ID, value, &reportPeriod, &highTh0, &lowTh0)) {
      Serial.println(F("reported"));
    } 
    else {
      Serial.println(F("adjust loop count"));
      reportPeriod /= 2;
    }
    wdt_reset();
    Serial.print(F("TS delay 4000 : "));
    Serial.println(millis()); 
    delay(4000);
    wdt_reset();
    value = dht.readHumidity();
    wdt_reset();
    Serial.print(F("TS humid report:"));
    Serial.println(millis()); 
    Serial.println(value);
    if (value != NAN && !report_data(SENSOR_HUMID_ID, value, &reportPeriod, &highTh1, &lowTh1)) {
      Serial.println(F("reported"));
    } 
    else {
      Serial.println(F("adjust loop count"));
      reportPeriod /= 2;
    }
#ifdef HAS_DUST_SENSOR
    wdt_reset();
    Serial.print(F("TS delay 4000 :"));
    Serial.println(millis());
    delay(4000);
    wdt_reset();
    value = SharpDust.measure() * 1000.0;
    Serial.print(F("TS dust report:"));
    Serial.println(millis());
    Serial.println(value);
    if (!report_data(SENSOR_DUST_ID, value, &reportPeriod, &highTh2, &lowTh2)) {
      Serial.println(F("reported"));
    } else {
      Serial.println(F("adjust loop count"));
      reportPeriod /= 2;
    }
#endif
    Serial.print(F("TS report finish:")); 
    Serial.println(millis()); 
    wdt_reset();

    if (reportPeriod < 120UL)
      reportPeriod = 120;
    
    first = millis() - first;
    
    target = (reportPeriod*1000UL) - first;
    do {
      wdt_reset();
      target -= 4000UL;
      delay(4000UL);
    }
    while (target >= 4000UL); 
    wdt_reset();
    delay(target);        
  }
  wdt_reset();
  first = 0;
}
