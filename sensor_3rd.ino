#include <Adafruit_cc3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <sha1.h>
#include <DHT.h>
#include <avr/wdt.h>

#include <EEPROM.h>

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
Adafruit_CC3000_Client www = Adafruit_CC3000_Client();

/* wifi connection definitions and variables */
#define MAX_SSID 33
#define MAX_PASSWD 20
char ssid[MAX_SSID];
char passwd[MAX_PASSWD];
char security;

/* web connection definitions and variables */
#define WWW_TRIALS  2
#define IDLE_MEASURE_COUNT 10
char server[] = "galvanic-cirrus-841.appspot.com";

char msgHeader[100];
char outBuf[70];
char postData[140];
int firstReport = 1;


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
  //debug prints
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

/* ugly ugly */
/* get the line input : the buffer should be big enough to handle \r \n \0 */
void getLineInput(char * buffer, int len) {
  int i = 0;
  
  for (i = 0; i<len - 1; i++) {
    byte bytes;
    do {
      bytes = Serial.readBytes(&buffer[i], 1);
    } while (bytes == 0);
    if (buffer[i] == '\r' || buffer[i] == '\n') {
      char trashTrail;
      //Serial.println();
      buffer[i] = 0;
      Serial.setTimeout(100);
      Serial.readBytes(&trashTrail, 1);  //remove trailing \r or \n
      return;
    }
    //Serial.print(buffer[i]);
  }
  buffer[i] = 0;
  //Serial.println();
}

void getInput() {
  int i;
  char sec[3];
  
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

int connectAp(byte trials) {
  Serial.println(F("connecting to AP.. "));
  if (!cc3000.begin())
  {
    Serial.println(F("wiring problem?"));
    while(1);
  }
  
  cc3000.setDHCP();
  
  if (!cc3000.connectToAP(ssid, passwd, security, trials)) {
    Serial.println(F("AP connection failed"));
    return -1;
  }
  while (!cc3000.checkDHCP())
  {
    delay(100);
  }
  delay(500);
  Serial.println(F("AP connected"));
  
  return 0;
}

void scanNetworks() {
  uint32_t index;
  uint8_t valid, rssi, sec;
  char b;
  
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
    Serial.println(rssi);
    Serial.println('0'+sec);
  }
  
  cc3000.stopSSIDscan();
  
  Serial.setTimeout(0xffffff);
  Serial.readBytes(&b, 1);
}

void setup() {
  int i;
  int firstTrial = 1;
  char tempInput[1];
  byte connected = 0;
  
  /* disable watchdog for now */
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
                if (!connectAp(1)) {
                  connected = 1;
                  writeEeprom();
                  break;
                }
              } else if (tempInput[0] == 's') {
                scanNetworks();
              }
            }
        }while (! connected);
      } 
      
    }
  }
  
  /* read AP connections settings */
  readEeprom();
  
  while (connectAp(1)) {
    firstTrial = 0;
    while (!Serial) {
      ;
    }
    getInput();
  }
  
  /* save AP info if we connected by user input */
  if (!firstTrial) {
    writeEeprom();
  }
  
  /* initialize thermal/humidity sensor */
  dht.begin();
  
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

byte postPage(char* domainBuffer, int thisPort, char* page, char* thisData, int val[3])
{
  byte ret, isSigned, valIndex, i;
  int * pVal;
  int status;
  byte rx_byte = 0;
  enum parseStatus parState = NONE_STATUS;
  
  for (i = 0; i < WWW_TRIALS; i++) {
    Serial.print(F("connecting the server.."));
    wdt_reset();
    www.close();
    status = www.connect(domainBuffer, thisPort);
    wdt_reset();
    if (www.connected())
      break;
  }
      
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
    Serial.println(status);
    return 0;
  }
  
  Serial.print(F("posted : "));
  Serial.print(strlen(thisData));
  Serial.println(F(" bytes"));
  wdt_reset();

  int connectLoop = 0;

  unsigned long lastRead = millis();
  valIndex = 0;
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    while (www.available()) {
      char c = www.read();
      //block ?
      //Serial.print(c);
      rx_byte ++;
      lastRead = millis();
      
      if (parState != EXIT_STATUS && c == '+') {
        isSigned = 0;
        parState = VALUE_STATUS;
        pVal = &val[valIndex++];
        *pVal = 0;
      } else if (parState == VALUE_STATUS) {
        if (c == '-') {
          isSigned = 1;
        } else if (c == 'z') {
          wdt_disable();
          while(true);
        } else if (c == 'e') {
          *pVal = ERROR_VAL;
          parState = EXIT_STATUS;
        }else if (c == 'n') {
          *pVal = ERROR_VAL;
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
  www.close();
  Serial.print(F("read : "));
  Serial.print(rx_byte);
  Serial.println(F(" bytes"));
  wdt_reset();
  
  return 1;
}

/* report data through POST and get the setting values */
int report_data(int sensor_type, float value, unsigned long * report_period, int* high_threshold, int* low_threshold) {
  int wifiStatus;
  int ret;
  int val[3] = {ERROR_VAL, ERROR_VAL, ERROR_VAL};
  int rssi = -60;
   
  if (!cc3000.checkDHCP()) {
    Serial.println(F("disconnected"));
    wdt_disable();
    connectAp(2);
    wdt_enable(WDTO_8S);
  }
  
  sprintf(postData, "%s&type=%d&value=%d&rssi=%d&first=%d", msgHeader, sensor_type, (int)(value*10.0), rssi, firstReport);
  ret = !postPage(server, 80, "/sensor/input/", postData, val);
  
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
  if (*high_threshold == ERROR_VAL) *high_threshold = 1000;
  if (*low_threshold == ERROR_VAL) *low_threshold = -1000;
  
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
  int lowTh1, highTh1, lowTh0, highTh0;
  byte i, loop_count = IDLE_MEASURE_COUNT;
  unsigned long reportPeriod = 600;
  unsigned long measurePeriod;
  float temperature, humidity;
  int value;
  unsigned long first, last, target;
  
  /* start Watch dog. */
  wdt_enable(WDTO_8S);
  
  while(true) {
    first = millis();
    Serial.println();
    Serial.print(F("TS:")); Serial.println(first);
    
    dht.read();
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();

    wdt_reset();
    loop_count = IDLE_MEASURE_COUNT;
    if (temperature != NAN && !report_data(0, temperature, &reportPeriod, &highTh0, &lowTh0)) {
      Serial.println(F("reported"));
    } else {
      Serial.println(F("adjust loop count"));
      loop_count /= 2;
    }
    wdt_reset();
    if (humidity != NAN && !report_data(1, humidity, &reportPeriod, &highTh1, &lowTh1)) {
      Serial.println(F("reported"));
    } else {
      Serial.println(F("adjust loop count"));
      loop_count /= 2;
    }
    wdt_reset();
    
    if (reportPeriod < 120UL)
      reportPeriod = 120;
    measurePeriod = reportPeriod / (unsigned long) IDLE_MEASURE_COUNT;
    
    if (measurePeriod == 0) measurePeriod = 1;
    
    first = millis() - first;
      
    for (i = 0; i < loop_count; i++) {
      last = millis();
      Serial.println();
      Serial.print(F("TS:")); Serial.println(last);
      Serial.println(F("value"));
            
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
      if (last + first < (measurePeriod * 1000UL)){
        target = (measurePeriod*1000UL) - last - first;
        do {
          wdt_reset();
          target -= 4000UL;
          delay(4000UL);
          //Serial.println(target);
        } while (target >= 4000UL); 
        delay(target);        
      }
      first = 0;
    }
  }  
}

