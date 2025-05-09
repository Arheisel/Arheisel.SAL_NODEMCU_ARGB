#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiUdp.h>

#define MODEL 0xFA //250 = Glow V1
#define DEVICE "Glow"
#define PORT 7990
#define UDPDELAY 1000
const byte broadcastIP[] = {255, 255, 255, 255};
byte udpPacket[] = { 252, MODEL, 0, 0, 0 };
#define UDPPACKETSIZE 2

#define LED_PIN     D2
#define NUM_LEDS    39
#define BRIGHTNESS  255
#define LED_TYPE    WS2811
#define COLOR_ORDER BRG
CRGB leds[NUM_LEDS];

byte rxdata[1024];
byte txdata[255];
 
WiFiServer wifiServer(PORT);
WiFiUDP udp;

unsigned long udpTime = 0;
 
void setup() {
  EEPROM.begin(12);
  Serial.begin(115200);
  WiFi.hostname(DEVICE);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);

  byte mac[6];
  WiFi.macAddress(mac);
  udpPacket[2] = mac[3];
  udpPacket[3] = mac[4];
  udpPacket[4] = mac[5];
 
  if(EEPROM.read(0)){
    byte eep[12];
    for(int i = 0; i < 12; i++){
      eep[i] = EEPROM.read(i);
    }

    //Static IP address configuration
    IPAddress staticIP(eep[0], eep[1], eep[2], eep[3]); //ESP static ip
    IPAddress gateway(eep[4], eep[5], eep[6], eep[7]);   //IP Address of your WiFi Router (Gateway)
    IPAddress subnet(eep[8], eep[9], eep[10], eep[11]);  //Subnet mask
    WiFi.config(staticIP, gateway, subnet);
  }
 
  wifiServer.begin();
  udp.begin(PORT);

  delay( 3000 ); // power-up safety delay
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );

  SetColor(0, 0, 255, 0); //Show Green

  udpTime = millis();
}

void loop() {

  if(Serial.available()){
    if(Serial.read() == 252){
      while(Serial.available() == 0) {}
      byte lenb0 = Serial.read();
      while(Serial.available() == 0) {}
      byte lenb1 = Serial.read();
      unsigned int len = (lenb0 * 256) + lenb1;
      for(int i = 0; i < len; i++){
        while(Serial.available() == 0) {}
        rxdata[i] = Serial.read();
      }
      ProcessData(rxdata, len, false, NULL);
    }
  }

  if(WiFi.status() == WL_CONNECTED){
  
    WiFiClient client = wifiServer.available();
   
    if (client) {
   
      while (client.connected()) {
        if (client.available() > 0) {
          if(client.read() == 252){
            while(client.available() == 0) {}
            byte lenb0 = client.read();
            while(client.available() == 0) {}
            byte lenb1 = client.read();
            unsigned int len = (lenb0 * 256) + lenb1;
            while(client.available() < len) {}
            client.readBytes(rxdata, len);
            ProcessData(rxdata, len, true, &client);
          }
        }
      }
      client.stop(); 
    }
    else{
      if(millis() > udpTime + UDPDELAY){
        udp.beginPacket(broadcastIP, PORT);
        udp.write(udpPacket, UDPPACKETSIZE);
        udp.endPacket();
        udpTime = millis();
      }
    }
  }
}

void ProcessData(byte * data, unsigned int len, bool isWifi, WiFiClient * client){
  switch(data[0]){
    case 1: //Identify
      txdata[0] = MODEL;
      txdata[1] = NUM_LEDS; // 4 Ch model TODO: implement actual channel detection
      SendData(txdata, 2, isWifi, client);
      break;
    case 2: //scan SSIDs
      ScanNetworks(isWifi, client);
      break;
    case 3: //receive SSID
      SetupWifi(data, len, isWifi, client);
      break;
    case 4: //return IP
      {
        String ip = WiFi.localIP().toString();
        ip.getBytes(txdata, 255);
        SendData(txdata, ip.length(), isWifi, client);
      }
      break;
    case 5: //return MAC
      {
        String mac = WiFi.macAddress();
        mac.getBytes(txdata, 255);
        SendData(txdata, mac.length(), isWifi, client);
      }
      break;
    case 99: //Array of colors
      DecodeColors(data, len);
      break;
  }
  if(data[0] >= 100 && len == 4){
    SetColor(data[0] - 100, data[1], data[2], data[3]);
  }
}

void SetColor(int ch, int r, int g, int b){
  if(ch > NUM_LEDS) return;
  
  if(ch == 0){
    for( int i = 0; i < NUM_LEDS; i++) {
      leds[i].r = r;
      leds[i].g = g;
      leds[i].b = b;
    }
  }
  else{
    leds[ch - 1].r = r;
    leds[ch - 1].g = g;
    leds[ch - 1].b = b;
  }
  noInterrupts();
  FastLED.show();
  interrupts();
}

void DecodeColors(byte * data, int len){
  if(len != NUM_LEDS * 3 + 1) return;
  for( int i = 0; i < NUM_LEDS; i++) {
    leds[i].r = data[i * 3 + 1];
    leds[i].g = data[i * 3 + 2];
    leds[i].b = data[i * 3 + 3];
  }
  noInterrupts();
  FastLED.show();
  interrupts();
}

void ScanNetworks(bool isWifi, WiFiClient * client){
  int n = WiFi.scanNetworks(false, false);
  if(n > 255) n = 255;
  txdata[0] = (byte)n;
  SendData(txdata, 1, isWifi, client);
  for(int i = 0; i < n; i++){
    String ssid = WiFi.SSID(i);
    ssid.getBytes(txdata, 255);
    SendData(txdata, ssid.length(), isWifi, client);
  }
}

void SetupWifi(byte * data, int len, bool isWifi, WiFiClient * client){
  int offset = 2;
  char *ssid = (char*) malloc(data[1] + 1);
  ByteToChar(data, ssid, 2, data[1]);
  offset += data[1];
  char *passwd = (char*) malloc(data[offset] + 1);
  ByteToChar(data, passwd, offset + 1, data[offset]);
  offset += data[offset] + 1;

  WiFi.disconnect();
  
  if(data[offset] > 0){
    for(int i = 0; i < 12; i++){
      EEPROM.write(i, data[offset + i]);   
    }
    EEPROM.commit();
    //Static IP address configuration
    IPAddress staticIP(data[offset], data[offset + 1], data[offset + 2], data[offset + 3]); //ESP static ip
    IPAddress gateway(data[offset + 4], data[offset + 5], data[offset + 6], data[offset + 7]);   //IP Address of your WiFi Router (Gateway)
    IPAddress subnet(data[offset + 8], data[offset + 9], data[offset + 10], data[offset + 11]);  //Subnet mask
    WiFi.config(staticIP, gateway, subnet);
  }
  else{
    EEPROM.write(0, 0);
    EEPROM.commit();
  }
  WiFi.hostname(DEVICE);      // DHCP Hostname (useful for finding device for static lease)
  WiFi.begin(ssid, passwd);
  WiFi.mode(WIFI_STA); //WiFi mode station (connect to wifi router only)
  WiFi.setAutoConnect(true);
  
  free(ssid);
  free(passwd);
}

void SendData(byte * data, unsigned int len, bool isWifi, WiFiClient * client){
  if(isWifi){
    client->write((byte)252);
    client->write((byte)len);
    for(int i = 0; i < len; i++){
      client->write(data[i]);
    }
  }
  else{
    Serial.write((byte)252);
    Serial.write((byte)len);
    for(int i = 0; i < len; i++){
      Serial.write(data[i]);
    }
  }
}


void ByteToChar(byte * data, char * target, int offset, int len){
  for(int i = 0; i < len; i++){
    target[i] = (char)data[i + offset];
  }
  target[len] = '\0';
}
 
