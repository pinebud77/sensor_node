#define CC3000_TINY_DRIVER

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

#define IDLE_TIMEOUT_MS  10000

#define WLAN_SSID "leekwon2G"
#define WLAN_PASSWD "xnvkdlqm"
#define WLAN_SECURITY   WLAN_SEC_WPA2

char server[] PROGMEM = "galvanic-cirrus-841.appspot.com";
char dictString[] PROGMEM = "0123456789abcdef";
char msgHeaderFmt[] PROGMEM = "secure_key=%s&mac_address=%s";
char postFmt[] PROGMEM = "%s&type=%d&value=%d&rssi=%d";
char thrFmt[] PROGMEM = "%s&type=%d";
char inputUrl[] PROGMEM = "/sensor/input/";
char settingsUrl[] PROGMEM = "/sensor/settings/";
char ssettingsUrl[] PROGMEM = "/sensor/ssettings/";
char contStr[] PROGMEM = "Content-Type: application/x-www-form-urlencoded";
char owenStr[] PROGMEM = "owen77";
char youngStr[] PROGMEM = "young";
char apConStr[] PROGMEM = "connecting to AP..";
char apConFailStr[] PROGMEM = "AP connection failed";
char wireStr[] PROGMEM = "Couldn't begin()! Check your wiring?";
char svrConStr[] PROGMEM = "connecting to server..";
char svrCondStr[] PROGMEM = "connected";
char postLineStr[] PROGMEM = "POST %s HTTP/1.1";
char conCloseStr[] PROGMEM = "Connection: close";
char conLenStr[] PROGMEM = "Content-Length: %u\r\n";

int sensor_check_interval = 60;
int sensor_report_interval = 600;

char msgHeader[100];

char outBuf[100];
char postData[150];

int buildSecureKey(char* macString, char* secureKey) {
  uint8_t* hash;
  int i;
  
  Sha1.init();
  Sha1.print(owenStr);
  Sha1.print(macString);
  Sha1.print(youngStr);
  
  hash = Sha1.result();
  
  for (i = 0; i < 20; i++) {
    secureKey[i*2] = dictString[hash[i]>>4];
    secureKey[i*2+1] = dictString[hash[i]&0xf];
  }
  secureKey[i*2] = '\0';
  
  return 0;
}

int buildMacString(byte* mac, char* macString) {
  int i;
  int stringIndex = 0;
  
  for (i = 0; i < 6; i++) {
    macString[stringIndex++] = dictString[mac[i]>>4];
    macString[stringIndex++] = dictString[mac[i]&0xf];
    if (i != 5) macString[stringIndex++] = '-';
  }
  
  return 0;
}

int buildMacEncString(byte* mac, char* macString) {
  int i;
  int stringIndex = 0;
  
  for (i = 0; i < 6; i++) {
    macString[stringIndex++] = dictString[mac[i]>>4];
    macString[stringIndex++] = dictString[mac[i]&0xf];
    if (i != 5) {
      macString[stringIndex++] = '%';
      macString[stringIndex++] = '3';
      macString[stringIndex++] = 'A';
    }
  }
  
  return 0;
}

int buildMsgHeader() {
  byte mac[6];
  char macString[] = "00-00-00-00-00-01";
  char macEncString[] = "00%3A00%3A00%3A00%3A00%3A01";
  char secureKey[] = "cad59a94e39ff1904bc28ec9240d358cd1e86bd4";
  
  cc3000.getMacAddress(mac);
  buildMacString(mac, macString);
  Serial.print("mac: ");
  Serial.println(macString);
  buildMacEncString(mac, macEncString);
  buildSecureKey(macString, secureKey);
  
  sprintf(msgHeader, msgHeaderFmt, secureKey, macEncString);
  
  return 0;
}

void connectAp() {
  // Set up CC3000 and get connected to the wireless network.
  Serial.println(apConStr);
  if (!cc3000.begin())
  {
    Serial.println(wireStr);
    while(1);
  }
  
  cc3000.setDHCP();
  while (!cc3000.connectToAP(WLAN_SSID, WLAN_PASSWD, WLAN_SECURITY)) {
    Serial.println(apConFailStr);
  }
  while (!cc3000.checkDHCP())
  {
    delay(100);
  }
  
  delay(500);
}

void setup() {
  int i;
  
  Serial.begin(9600);
  
  /*
  while (!Serial) {
    ;
  }
  */
  
  connectAp();

  buildMsgHeader();
  
  dht.begin();
}

enum parseStatus {
  NONE_STATUS,
  FIRST_PLUS,
  FIRST_VALUE,
  FIRST_EQUAL,
  SECOND_PLUS,
  SECOND_VALUE,
  SECOND_EQUAL,
};

byte postPage(char* domainBuffer, int thisPort, char* page, char* thisData, int *val1=NULL, int *val2=NULL)
{
  int ret, isSigned;
  int * pVal;
  enum parseStatus parState = NONE_STATUS;
  
  Serial.print(svrConStr);  
  www.connect(domainBuffer, thisPort);
    
  if(www.connected())
  {
    
    Serial.println(svrCondStr);
    
    // send the header
    sprintf(outBuf, postLineStr,page);
    www.println(outBuf);
    sprintf(outBuf,"Host: %s",domainBuffer);
    www.println(outBuf);
    www.println(conCloseStr);
    www.println(contStr);
    sprintf(outBuf, conLenStr,strlen(thisData));
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
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    while (www.available()) {
      char c = www.read();
      Serial.print(c);
      lastRead = millis();
      
      if (c == '+') {
        isSigned = 0;
        if (parState == NONE_STATUS) {
          parState = FIRST_PLUS;
          pVal = val1;
        } else if (parState == FIRST_EQUAL) {
          parState = SECOND_PLUS;
          pVal = val2;
        }
      }
      if (parState == FIRST_VALUE || parState == SECOND_VALUE) {
        if (c == '-') {
          isSigned = 1;
        } if (c == 'n' && parState == FIRST_VALUE) {
          *pVal = -1000;
          parState = FIRST_EQUAL;
        } if (c == 'n' && parState == SECOND_VALUE) {
          *pVal = 1000;
          parState = SECOND_EQUAL;
        }else if (c == '=' && parState == FIRST_VALUE) {
          if (isSigned) *pVal = - *pVal;
          parState = FIRST_EQUAL;
        } else if (c == '=' && parState == SECOND_VALUE) {
          if (isSigned) *pVal = - *pVal;
          parState = SECOND_EQUAL;
        } else {
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

int report_data(int sensor_type, float value) {
  //connect to the AP
  int i;
  int rssi = -60;
  
  if (!cc3000.checkDHCP()) {
    connectAp();
  }
  
  sprintf(postData, postFmt, msgHeader, sensor_type, (int)value*10, rssi);
  return ! postPage(server, 80, inputUrl, postData);
}

unsigned long getReportPeriod() {
  int val;
  
  if (!cc3000.checkDHCP()) {
    connectAp();
  }  
  postPage(server, 80, settingsUrl, msgHeader, &val);
  
  return (unsigned long) val;
}

int getThreshold(int type, int * high, int * low) {
  if (!cc3000.checkDHCP()) {
    connectAp();
  }
  sprintf(postData, thrFmt, msgHeader, type);
  
  return !postPage(server, 80, ssettingsUrl, postData, low, high);;
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
  unsigned long reportPeriod;
  unsigned long measurePeriod;
  int value;
  
  dht.read();
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  report_data(0, temperature);
  report_data(1, humidity);
  
  while(true) {
    reportPeriod = getReportPeriod();
    if (reportPeriod < 120)
      reportPeriod = 120;
    measurePeriod = reportPeriod / 10;
    
    if (measurePeriod == 0) measurePeriod = 1;
      
    getThreshold(0, &lowTh0, &highTh0);
    getThreshold(1, &lowTh1, &highTh1);
     
    for (i = 0; i < 10; i++) {
      dht.read();
      float temperature = dht.readTemperature();
      float humidity = dht.readHumidity();
      
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
      delay(measurePeriod * 1000);
    } 

    if (!report_data(0, temperature)) {
      Serial.println("reported");
    }
    if (!report_data(1, humidity)) {
      Serial.println("reported");
    }
  }  
}
