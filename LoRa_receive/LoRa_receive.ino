#include <ArduinoJson.h>
#include <Config.h>
#include <FirebaseClient.h>

#include <EEPROM.h>
#include <SPI.h>
#include <LoRa.h>



#include <Wire.h>
#include "common.h"
#include <WiFiManager.h>
#include <WiFiClientSecure.h>

bool res = false;

#define DATABASE_URL "https://esp-sensor-station-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "AIzaSyARS11xUSzdiZJDQVcV2hiflRII8Pn7uw8"
#define DATABASE_SECRET "RmLjwuIq9T8fzua4ev3BBEqxyEKsLdr7BegTypcF"

FirebaseApp app;
RealtimeDatabase Database;
AsyncResult result;
LegacyToken dbSecret(DATABASE_SECRET);

WiFiClientSecure ssl;
DefaultNetwork network;
AsyncClientClass client(ssl, getNetwork(network));

String buf = "";
char id_received[10] = "";
float temp = -100;
float humid = -1;
int pressure = 0;
unsigned int wind_dir = 999, wind_speed = 2147483647, gust_dir = 999, gust_speed = 2147483647, rain = 2147483647;

String dummy = "";

char *firebase_path = "/%4s/push";
char firebase_path_formatted[20];

JsonDocument doc;
bool data_avail = false;

void blinkLED(int count = 3) {
  for (int k = 0; k < count; k++) {
    digitalWrite(2, LOW);
    delay(50);
    digitalWrite(2, HIGH);
    delay(50);
  }
}

void onReceive(int packetSize) {
  data_avail = true;

}

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  res = wm.autoConnect("ESP32_LoRa_Recv","huet2024");
  if(!res) {
    Serial.println("Failed to connect");
  // ESP.restart();
    } 
    else {
  //if you get here you have connected to the WiFi    
  Serial.println("connected...yeey :)");
    }
  // Blink led for indication
  blinkLED(2);
  //Firebase Set up
  ssl.setInsecure();
  initializeApp(client, app, getAuth(dbSecret));
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
  client.setAsyncResult(result);
  
  Serial.println("LoRa Receiver"); 
  //LoRa Init
  SPIClass * vspi = NULL; // throw predefined vspi out of the window :D
  vspi = new SPIClass(VSPI); // create a new one
  vspi->begin(SCK, MISO, MOSI, NSS); // assign custom pins
  LoRa.setSPI(*vspi); // tell LORA to use this custom spi
  LoRa.setPins(NSS, RST, DIO); // NSS RESET DIO0
  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    while (1) {
      blinkLED(1);
      delay(5000);
      }
  }
  //LoRa started up, setting up parameters
  LoRa.setSpreadingFactor(SF);
  LoRa.setSignalBandwidth(BW);
  LoRa.setCodingRate4(CDRATE);
  LoRa.setPreambleLength(15);
  LoRa.setSyncWord(0x35);
  //Set callback
  LoRa.onReceive(onReceive);
  LoRa.receive();
}

void loop() {
  // put your main code here, to run repeatedly:
  if (data_avail) {
  data_avail = false;
  // received a packet
  //making an array of char to store data
  Serial.print("Received packet '");
  // read packet
  while (LoRa.available())  {
    buf += (char)LoRa.read();
  }
  Serial.print(buf);
  Serial.print("' with RSSI ");
  Serial.println(LoRa.packetRssi());
  //spliting received data into group and extract data
  dummy.toCharArray(id_received,10);
  temp = -100;
  humid = -1;
  pressure = 0;
  wind_dir = 999; 
  wind_speed = 2147483647;
  gust_dir = 999;
  gust_speed = 2147483647;
  rain = 2147483647;
  sscanf(buf.c_str(),"I%4s T%4f H%4f P%4i W%3u%5u G%3u%5u R%3u",id_received,&temp,&humid,&pressure,&wind_dir,&wind_speed,&gust_dir,&gust_speed,&rain);
  buf = "";
  Serial.println("Getting info completed");
  Serial.flush();
  if (res == true) 
  {
    if (id_received != "") {
    doc["id"] = id_received;
    Serial.println("ID Available");
    Serial.flush();
    }
    if (temp >= -60) {
    doc["temperature"] = temp;
    Serial.println("Temp Available");
    Serial.flush();
    }
    if (humid >= 0) {
    doc["humidity"] = humid;
    Serial.println("Humid Available");
    Serial.flush();
    }
    if (pressure >= 600 and pressure <= 1400) {
    doc["pressure"] = pressure;
    Serial.println("Pres. Available");
    Serial.flush();
    }
    if (wind_dir >= 0 and wind_dir <= 360) {
    doc["sustain_windDir"] = wind_dir;
    Serial.println("Wind Dir Available");
    Serial.flush();
    }
    if (wind_speed < 20000000) {
    doc["sustain_windSpd"] = wind_speed;
    Serial.println("Wind Spd Available");
    Serial.flush();
    }
    if (gust_dir >= 0 and gust_dir <= 360) {
    doc["gust_windDir"] = gust_dir;
    Serial.println("Gust Dir Available");
    Serial.flush();
    }
    if (gust_speed < 20000000) {
    doc["gust_windSpd"] = gust_speed;
    Serial.println("Gust Spd Available");
    Serial.flush();
    }
    if (rain < 20000000) {
    doc["rain"] = rain;
    Serial.println("Rain Available");
    Serial.flush();
    }
    String Json_output;
    serializeJson(doc,Json_output);
    Serial.println(Json_output);
    Serial.flush();
    sprintf(firebase_path_formatted,firebase_path,id_received);
    //send to Firebase
    String name = Database.push<object_t>(client, firebase_path_formatted, object_t(Json_output));
  }
  }
}
