#include <ESP8266WiFi.h>
#include <FirebaseArduino.h>
#include <SPI.h>
#include "MFRC522.h"
#include <RBD_Timer.h>
#include <NTPtimeESP.h>

#define RST_PIN  5  // RST-PIN for RC522 - RFID - SPI - Module GPIO5 
#define SS_PIN  4  // SDA-PIN for RC522 - RFID - SPI - Module GPIO4 

#define FIREBASE_HOST "bus-loading-system.firebaseio.com"
#define FIREBASE_AUTH "zdirbaAI1lf6WiDNKGQHgwWx7dAMl3ZDdIXsx53G"

#define WIFI_SSID "lenovo"     // change according to your Network - cannot be longer than 32 characters!
#define WIFI_PASS "password123" // change according to your Network

#define LOADING_GREEN 0 //green led; connected to D3 - pin 0; pin turns HIGH when a bus is loading
#define TENMINSLEFT_ORANGE 2 //orange led; connected to D4 - pin 2; pin turns HIGH when a bus has 10 minutes left
#define OVERTIME_RED 15 //red led; connected to D8 - pin 15; pin turns HIGH when a bus exceeds 30 minutes

NTPtime NTPph("ph.pool.ntp.org");

RBD::Timer overtime_timer(40000); //timer for checking if a bus exceeds 30 minutes
RBD::Timer getready_timer(20000); //timer for checking if a bus has 10 minutes left; equivalent to 20 minutes
RBD::Timer nocarddetection_timer(5000); //if no bus is read again for 10 seconds, it means bus has left

MFRC522 mfrc522(SS_PIN, RST_PIN); 
bool wasacardpresent = false;
bool overtime_done = false;
bool getready_done = false;
String previousUID = "";
String key = "";
String loading_bay = "E1";

void setup() {
  Serial.begin(115200);
  SPI.begin();           
  mfrc522.PCD_Init();  
  
  pinMode(LOADING_GREEN, OUTPUT);
  pinMode(TENMINSLEFT_ORANGE, OUTPUT);
  pinMode(OVERTIME_RED, OUTPUT);
  switchLEDsOff(); 
  
  delay(250);
  init_wifi(); 
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
}

void init_wifi() {
  Serial.println("Booting....");
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while ((WiFi.status() != WL_CONNECTED)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    
    Serial.println("Ready!");
    Serial.println("======================================================"); 
    Serial.println("Scan for Card and print UID:");
  }else{
    Serial.println("WiFi not connected");
  }
}

void loop() {
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    nocarddetection_timer.restart();
    update_alarm();
    String currUID = rfidDetails(mfrc522.uid.uidByte, mfrc522.uid.size);
    
    if(!previousUID.equals(currUID)) {              
      if(wasacardpresent) {
        reset_alarm();
        update_alarm();
        send_timeout();
        nocarddetection_timer.restart();
      }
      
      previousUID = currUID;
      String time_in = get_time_stamp();
      send_data(currUID, time_in);
      nocarddetection_timer.restart();
      wasacardpresent = true;
    }    
  } else {
    if(wasacardpresent) {
      if(nocarddetection_timer.onExpired()) {        
        send_timeout();
        wasacardpresent = false;
        previousUID = "";  
        reset_alarm();
      }
    } else {
      overtime_timer.restart();
      getready_timer.restart();
    }
  }
}

// Helper routine to dump the UID as hex values to Serial
String rfidDetails(byte *buffer, byte bufferSize) {
  String strID = "";
  for (byte i = 0; i < bufferSize; i++) {
    strID +=
    (buffer[i] < 0x10 ? "0" : "") +
    String(buffer[i], HEX) +
    (i!=3 ? ":" : "");
  }
  strID.toUpperCase();
  return strID;
}

void reset_alarm() {
  switchLEDsOff();
  overtime_timer.restart();
  getready_timer.restart();
  overtime_done = false;
  getready_done = false;
}

void switchLEDsOff(){
  digitalWrite(LOADING_GREEN, LOW);
  digitalWrite(TENMINSLEFT_ORANGE, LOW);
  digitalWrite(OVERTIME_RED, LOW);
}

void update_alarm() {
  if(getready_timer.onExpired()){
      switchOnOrange();
      getready_done = true;
    }
    
    if(overtime_timer.onExpired()){
      switchOnRed();
      overtime_done = true;
    }
    
    if(!getready_done && !overtime_done){
      switchOnGreen(); 
    }
}

void switchOnGreen(){
  digitalWrite(LOADING_GREEN, HIGH);
  digitalWrite(TENMINSLEFT_ORANGE, LOW);
  digitalWrite(OVERTIME_RED, LOW);
}

void switchOnOrange(){
  digitalWrite(TENMINSLEFT_ORANGE, HIGH);
  digitalWrite(LOADING_GREEN, LOW);
  digitalWrite(OVERTIME_RED, LOW);
}

void switchOnRed(){
  digitalWrite(OVERTIME_RED, HIGH);
  digitalWrite(LOADING_GREEN, LOW);
  digitalWrite(TENMINSLEFT_ORANGE, LOW);
}

void send_timeout() {
    String path = "RTData/" + key + "/time_out";
    String time_out = get_time_stamp();
    
    Firebase.setString(path, time_out);
    delay(3000);    
}

String get_time_stamp() {
  strDateTime dateTime = NTPph.getNTPtime(8, 1);

  while(!dateTime.valid) {
    dateTime = NTPph.getNTPtime(8, 1);    
  }  
  
  String year = String(dateTime.year);
  String month = double_digit(String(dateTime.month));
  String day = double_digit(String(dateTime.day));
  String hour = double_digit(String(dateTime.hour));
  String minute = double_digit(String(dateTime.minute));
  String second = double_digit(String(dateTime.second));
  return year + "-" + month + "-" + day + " " + hour + ":" + 
         minute + ":" + second;
}

String double_digit(String time_unit) {
  if(time_unit.length() == 1) {
    return "0" + time_unit;
  } else {
    return time_unit;  
  }
}

void send_data(String rfid, String time_in) {
    StaticJsonBuffer<200> jsonBuffer;
    String data_json = "{\"loading_bay\":\"" + loading_bay + "\",\"" +
                       "rfid\":\"" + rfid + "\",\"" +
                       "time_in\":\"" + time_in + "\"}";
    
    JsonObject& data_object = jsonBuffer.parseObject(data_json);
    if(!data_object.success()) {
      Serial.println("parseObject() failed");
      return;
    }
  
    JsonVariant data_variant = data_object; 
    key = Firebase.push("RTData", data_variant);
    delay(3000);  
}
