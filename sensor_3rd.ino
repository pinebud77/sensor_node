#define CC3000_TINY_DRIVER

#include <Adafruit_cc3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <sha1.h>
#include <DHT.h>

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

#define SERVER_NAME "galvanic-cirrus-841.appspot.com"
char dictString[] = "0123456789abcdef";

int sensor_check_interval = 60;
int sensor_report_interval = 600;

char msgHeader[100];

char outBuf[100];
char postData[150];

int buildSecureKey(char* macString, char* secureKey) {
  uint8_t* hash;
  int i;
  
  Sha1.init();
  Sha1.print("owen77");
  Sha1.print(macString);
  Sha1.print("young");
  
  hash = Sha1.result();
  
  for (i = 0; i < 20; i++) {
    secureKey[i++] = "0123456789abcdef"[hash[i]>>4];
    secureKey[i++] = "0123456789abcdef"[hash[i]&0xf];
  }  
  
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
  
  sprintf(msgHeader, "secure_key=%s&mac_address=%s", secureKey, macEncString);
  
  return 0;
}

void connectAp() {
  // Set up CC3000 and get connected to the wireless network.
  Serial.println(F("Connecting to AP..."));
  if (!cc3000.begin())
  {
    Serial.println(F("Couldn't begin()! Check your wiring?"));
    while(1);
  }
  
  cc3000.setDHCP();
  while (!cc3000.connectToAP(WLAN_SSID, WLAN_PASSWD, WLAN_SECURITY)) {
    Serial.println(F("Connection Failed!"));
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

byte postPage(char* domainBuffer, int thisPort, char* page, char* thisData)
{
  int ret;
  
  Serial.print(F("connecting to the server... : "));  
  www.connect(domainBuffer, thisPort);
    
  if(www.connected())
  {
    
    Serial.println(F("connected"));
    
    // send the header
    sprintf(outBuf,"POST %s HTTP/1.1",page);
    www.println(outBuf);
    sprintf(outBuf,"Host: %s",domainBuffer);
    www.println(outBuf);
    www.println(F("Connection: close"));
    www.println(F("Content-Type: application/x-www-form-urlencoded"));
    sprintf(outBuf,"Content-Length: %u\r\n",strlen(thisData));
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
    Serial.print(F("failed : "));
    return 0;
  }
  
  Serial.println(F("POST written"));

  int connectLoop = 0;

  unsigned long lastRead = millis();
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    while (www.available()) {
      char c = www.read();
      Serial.print(c);
      lastRead = millis();
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
  
  sprintf(postData, "%s&type=%d&value=%d&rssi=%d", msgHeader, sensor_type, (int)value*10, rssi);
  return ! postPage(SERVER_NAME, 80, "/sensor/input/", postData);
}

unsigned long getReportPeriod() {
  //get the period from the server
  return 600;
}

int getThreshold(int type, int * high, int * low) {
  *high = 0;
  *low = 100;
  
  return 0;
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
  
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  report_data(0, temperature);
  report_data(1, humidity);
  
  while(true) {
    reportPeriod = getReportPeriod();
    measurePeriod = reportPeriod / 10;
    
    if (measurePeriod == 0) measurePeriod = 1;
      
    getThreshold(0, &lowTh0, &highTh0);
    getThreshold(1, &lowTh1, &highTh1);
     
    for (i = 0; i < 10; i++) {
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
      Serial.println("data reported");
    } else {
      Serial.println("data failed");
    }
    if (!report_data(1, humidity)) {
      Serial.println("data reported");
    } else {
      Serial.println("data failed");
    }
  }  
}
