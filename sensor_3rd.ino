#define CC3000_TINY_DRIVER

#include <EEPROM.h>
#include <Adafruit_cc3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <sha1.h>
#include <DHT.h>
#include <avr/pgmspace.h>

#define DHTPIN 7
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!
// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10
// Use hardware SPI for the remaining pins
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT, SPI_CLOCK_DIV2);
Adafruit_CC3000_Client www = Adafruit_CC3000_Client();

#define IDLE_TIMEOUT_MS  5000

#define MAX_SSID 20
#define MAX_PASSWD 20

char ssid[MAX_SSID];
char passwd[MAX_PASSWD];
char security;

char server[] = "galvanic-cirrus-841.appspot.com";

int sensor_check_interval = 60;
int sensor_report_interval = 600;

char msgHeader[100];

char outBuf[70];
char postData[130];

void readEeprom() {
  int i;
  
  Serial.println(F("reading EEPROM.."));
  for (i = 0; i < MAX_SSID - 1; i++) {
    ssid[i] = EEPROM.read(i);
  }
  ssid[MAX_SSID] = 0;
  for (i = 0; i < MAX_PASSWD - 1; i++) {
    passwd[i] = EEPROM.read(i + MAX_SSID);
  }
  passwd[MAX_PASSWD] = 0;
  security = EEPROM.read(MAX_SSID + MAX_PASSWD + 1);
  
  /*
  Serial.print(F("read ssid : "));
  Serial.println(ssid);
  Serial.print(F("read passwd : "));
  Serial.println(passwd);
  Serial.print(F("read security : "));
  Serial.println((int)security);
  Serial.println();
  */
}

void writeEeprom() {
  int i;
  
  Serial.println("Writing EEPROM");
  
  for (i = 0; i < MAX_SSID; i++) {
    EEPROM.write(i, ssid[i]);
  }
  for (i = 0; i < MAX_PASSWD; i++) {
    EEPROM.write(i + MAX_SSID, passwd[i]);
  }
  EEPROM.write(MAX_SSID + MAX_SSID + 1, security);
}

void getLineInput(char * buffer, int len) {
  int i = 0;
  
  for (i = 0; i<len - 1; i++) {
    byte bytes;
    do {
      bytes = Serial.readBytes(&buffer[i], 1);
    } while (bytes == 0);
    if (buffer[i] == '\r') {
      i--;
      continue;
    } else if (buffer[i] == '\n') {
      Serial.println();
      buffer[i] = 0;
      return;
    }
    Serial.print(buffer[i]);
  }
  buffer[i] = 0;
  Serial.println();
}

void getInput() {
  int i;
  char sec[3];
  
  Serial.print(F("SSID : "));
  getLineInput(ssid, MAX_SSID);
  
  do {
    Serial.println(F(">None : 0"));
    Serial.println(F(">WEP : 1"));
    Serial.println(F(">WPA1 : 2"));
    Serial.println(F(">WPA2 : 3"));
    Serial.print(F("Security : "));
    getLineInput(sec, 3);
    security = sec[0] - '0';
  } while (security > 4 || security < 0);
  
  if (security != 0) {
    Serial.print(F("Password : "));
    getLineInput(passwd, MAX_PASSWD);
  }
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
      } else {
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

int connectAp() {
  // Set up CC3000 and get connected to the wireless network.
  Serial.print(F("connecting to AP.. "));
  Serial.println(ssid);
  if (!cc3000.begin())
  {
    Serial.println(F("wiring problem?"));
    while(1);
  }
  
  cc3000.setDHCP();
  if (!cc3000.connectToAP(ssid, passwd, security, 3)) {
    Serial.println(F("AP connection failed"));
    return -1;
  }
  while (!cc3000.checkDHCP())
  {
    delay(100);
  }
  
  delay(500);
  
  return 0;
}

void setup() {
  int i;
  int firstTrial = 1;
  
  Serial.begin(9600);
  
  readEeprom();
  
  while (connectAp()) {
    firstTrial = 0;
    while (!Serial) {
      ;
    }
    getInput();
  }
  if (!firstTrial) {
    writeEeprom();
  }

  buildMsgHeader();
  
  dht.begin();
}

enum parseStatus {
  NONE_STATUS,
  PLUS_STATUS,
  VALUE_STATUS,
  EQUAL_STATUS,
};

byte postPage(char* domainBuffer, int thisPort, char* page, char* thisData, int val[3])
{
  byte ret, isSigned, valIndex;
  int * pVal;
  enum parseStatus parState = NONE_STATUS;
  
  Serial.print(F("server.."));
  www.connect(domainBuffer, thisPort);
    
  if(www.connected())
  {
    Serial.println(F("connected"));
        
    // send the header
    sprintf(outBuf, "POST %s HTTP/1.1", page);
    www.println(outBuf);
    sprintf(outBuf,"Host: %s",domainBuffer);
    www.println(outBuf);
    www.println(F("Connection: close"));
    www.println(F("Content-Type: application/x-www-form-urlencoded"));
    sprintf(outBuf, "Content-Length: %u\r\n", strlen(thisData));
    www.println(outBuf);   
        
    int dataLen = strlen(thisData);
    char * pos = thisData;
    do {
      www.write(pos, 10);
      dataLen -= 10;
      pos += 10;
    } while (dataLen > 10);
    www.write(pos, dataLen);
  }
  else
  {
    Serial.print(F("failed"));
    return 0;
  }
  
  Serial.println(F("written"));

  int connectLoop = 0;

  unsigned long lastRead = millis();
  valIndex = 0;
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    while (www.available()) {
      char c = www.read();
      //block ?
      //Serial.print(c);
      lastRead = millis();
      
      if (c == '+') {
        isSigned = 0;
        parState = VALUE_STATUS;
        pVal = &val[valIndex++];
        *pVal = 0;
      } else if (parState == VALUE_STATUS) {
        if (c == '-') {
          isSigned = 1;
        } else if (c == 'n') {
          *pVal = 9999;
          parState = EQUAL_STATUS;
        } else if (c == '=') {
         if (isSigned) *pVal = - *pVal;
         parState = EQUAL_STATUS;
        } else if (c == '.') {
          parState = EQUAL_STATUS;
        }else {
         *pVal *= 10;
         *pVal += c - '0';
        }
     } 
    }
  }
  Serial.println("");
  www.close();
  
  return 1;
}

int report_data(int sensor_type, float value, unsigned long * report_period, int* high_threshold, int* low_threshold) {
  //connect to the AP
  int i;
  int ret;
  int val[3] = {9987, 9987, 9987};
  int rssi = -60;
  
  if (!cc3000.checkDHCP()) {
    connectAp();
  }
  
  sprintf(postData, "%s&type=%d&value=%d&rssi=%d", msgHeader, sensor_type, (int)(value*10), rssi);
  ret = !postPage(server, 80, "/sensor/input/", postData, val);
  
  if (ret) {
    return -1;
  }
  
  if (val[0] == 9987) {
    Serial.println(F("Getting period failed"));
    return -1;
  }
  *report_period = val[0];
  *high_threshold = val[1];
  *low_threshold = val[2];
  if (*high_threshold == 9999) *high_threshold = 1000;
  if (*low_threshold == 9999) *low_threshold = -1000;
  
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
  int outRangeReported = 0;
  int i;
  int lowTh1, highTh1, lowTh0, highTh0;
  unsigned long reportPeriod = 600;
  unsigned long measurePeriod;
  float temperature, humidity;
  int value;
  unsigned long first;
  
  while(true) {
    first = millis();
    Serial.println();
    Serial.println(first);
    
    dht.read();
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();

    if (temperature != NAN && !report_data(0, temperature, &reportPeriod, &highTh0, &lowTh0)) {
      Serial.println(F("reported"));
    }
    if (humidity != NAN && !report_data(1, humidity, &reportPeriod, &highTh1, &lowTh1)) {
      Serial.println(F("reported"));
    }
    
    if (reportPeriod < 120)
      reportPeriod = 120;
    measurePeriod = reportPeriod / 10;
    
    if (measurePeriod == 0) measurePeriod = 1;
    
    first = millis() - first;
      
    for (i = 0; i < 10; i++) {
      unsigned long last = millis();
      Serial.println();
      Serial.println(F("value"));
      Serial.println(last);
      
      dht.read();
      temperature = dht.readTemperature();
      humidity = dht.readHumidity();
      
      Serial.println(temperature);
      Serial.println(humidity);
      
      if (!outRangeReported && outRange(temperature, lowTh0, highTh0) || outRange(humidity, lowTh1, highTh1)) {
        outRangeReported = 1;
        break;
      }
      
      if (outRangeReported && !outRange(temperature, lowTh0, highTh0) && !outRange(humidity, lowTh1, highTh1)) {
        outRangeReported = 0;
        break;
      }
      last = millis() - last;
      Serial.print(F("first=")); Serial.println(first);
      Serial.print(F("last=")); Serial.println(last);
      Serial.print(F("measurePeriod*1000UL=")); Serial.println(measurePeriod*1000UL);
      if (last + first < (measurePeriod * 1000UL)){
        delay((measurePeriod*1000UL) - last - first);
      }
      first = 0;
    }
  }  
}

