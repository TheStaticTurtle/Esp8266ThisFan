#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

const int IO_INPUT_SPEED   = D1;
const int IO_INPUT_ONOFF   = D2;
const int IO_OUTPUT_SPEED1 = D7;
const int IO_OUTPUT_SPEED2 = D6;
const int IO_OUTPUT_SPEED3 = D5;
const int IO_OUTPUT_SPEED4 = D0;
const int IO_OUTPUT_FAN    = D4;

#ifndef STASSID
#define STASSID "------------"
#define STAPSK  "------------"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
const char* mqtt_server = "------------";
const char* mqtt_user = "------------";
const char* mqtt_pass = "------------";

const char* mqtt_state             = "/iot/fan/1/state";
const char* mqtt_command           = "/iot/fan/1/set";
const char* mqtt_speed_state       = "/iot/fan/1/speed/state";
const char* mqtt_speed_command     = "/iot/fan/1/speed/set";
const char* mqtt_speed_state_raw   = "/iot/fan/1/speed/state_raw";
//const char* mqtt_speed_command_raw = "/iot/fan/1/speed/set_raw";

int  fanSpeed = 0;
bool isFanRunning = false;

WiFiClient espClient;
PubSubClient client(espClient);

void writePercentageToLeds(int percentage, bool forceDisplay) {
  digitalWrite(IO_OUTPUT_SPEED1, percentage >  0   && (isFanRunning || forceDisplay));
  digitalWrite(IO_OUTPUT_SPEED2, percentage >= 50  && (isFanRunning || forceDisplay));
  digitalWrite(IO_OUTPUT_SPEED3, percentage >= 75  && (isFanRunning || forceDisplay));
  digitalWrite(IO_OUTPUT_SPEED4, percentage == 100 && (isFanRunning || forceDisplay));
}

void setFanSpeed(int speed) {
  if(speed == 0) {
    digitalWrite(IO_OUTPUT_FAN,LOW);
  } else {
    analogWriteFreq(15);
    analogWrite(IO_OUTPUT_FAN, map(speed,0,100,275,1023));
  }
}

void sendToMqtt() {
  if(!client.connected()) { return;}
  if(isFanRunning) {
    client.publish(mqtt_state, "ON");
  } else {
    client.publish(mqtt_state, "OFF");
  }
  if(fanSpeed == 100) {
    //client.publish(mqtt_speed_state, "ON"); //Homeassistant don't suport 4 states
    client.publish(mqtt_speed_state, "HIGH");
  } else if(fanSpeed >= 75) {
    client.publish(mqtt_speed_state, "HIGH");
  } else if(fanSpeed >= 50) {
    client.publish(mqtt_speed_state, "MEDIUM");
  } else if(fanSpeed > 0) {
    client.publish(mqtt_speed_state, "LOW");
  } else {
    client.publish(mqtt_speed_state, "OFF");
  }
  
  client.publish(mqtt_speed_state_raw, String(fanSpeed).c_str());
}



void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  
  if (strcmp(topic,mqtt_command)==0) {
    if (String((char*)payload).startsWith("ON"))  { fanSpeed=25;  isFanRunning = true;  }
    if (String((char*)payload).startsWith("OFF")) { fanSpeed=0;   isFanRunning = false; }
  } 
  else if (strcmp(topic,mqtt_speed_command)==0){
    if (String((char*)payload).startsWith("ON"))     { fanSpeed=100; isFanRunning = true;  }
    if (String((char*)payload).startsWith("HIGH"))   { fanSpeed=75;  isFanRunning = true;  }
    if (String((char*)payload).startsWith("MEDIUM")) { fanSpeed=50;  isFanRunning = true;  }
    if (String((char*)payload).startsWith("LOW"))    { fanSpeed=25;  isFanRunning = true;  }
    if (String((char*)payload).startsWith("OFF"))    { fanSpeed=0;   isFanRunning = false; }
  }
  
  /*if (strcmp(topic,mqtt_speed_command_raw)==0)
  
  }*/
  setFanSpeed(fanSpeed);
  sendToMqtt();
}

void mqtt_reconnect() {
  while (!client.connected()) {
    writePercentageToLeds(100,true);
    delay(50);
    writePercentageToLeds(0,true);
    delay(50);
    
    Serial.print("Attempting MQTT connection...");
    String clientId = "EspFan-01-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(),mqtt_user,mqtt_pass)) {
      Serial.println("Connected to mqtt server");
      client.subscribe(mqtt_command);
      client.subscribe(mqtt_speed_command);
      sendToMqtt();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(1000);
    }
  }
}


void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(IO_INPUT_SPEED  , INPUT);
  pinMode(IO_INPUT_ONOFF  , INPUT);
  pinMode(IO_OUTPUT_SPEED1, OUTPUT);
  pinMode(IO_OUTPUT_SPEED2, OUTPUT);
  pinMode(IO_OUTPUT_SPEED3, OUTPUT);
  pinMode(IO_OUTPUT_SPEED4, OUTPUT);
  pinMode(IO_OUTPUT_FAN   , OUTPUT);
  digitalWrite(IO_OUTPUT_SPEED1, LOW);
  digitalWrite(IO_OUTPUT_SPEED2, LOW);
  digitalWrite(IO_OUTPUT_SPEED3, LOW);
  digitalWrite(IO_OUTPUT_SPEED4, LOW);
  digitalWrite(IO_OUTPUT_FAN   , LOW);
  pinMode(D1, INPUT);


  Serial.begin(115200);
  Serial.println("Booting");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  ArduinoOTA.setHostname("EspFan-01");
  ArduinoOTA.onStart([]() {
    Serial.println("Start update");
    writePercentageToLeds(0,true);
  });
  ArduinoOTA.onEnd([]() {
    for(int i=0; i<10; i++) {
      writePercentageToLeds(100,true);
      delay(50);
      writePercentageToLeds(0,true);
      delay(50);
    }
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    writePercentageToLeds((progress / (total / 100)),true);
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
    
  writePercentageToLeds(0,true);

  
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);
}


void loop() {

  if (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    writePercentageToLeds(100,true);
    delay(50);
    writePercentageToLeds(0,true);
    delay(50);
  } else {
    ArduinoOTA.handle();
    if (!client.connected()) {
      mqtt_reconnect();
    }
    client.loop();
    writePercentageToLeds(fanSpeed,false);
  }
  
  if(digitalRead(IO_INPUT_SPEED) && isFanRunning) {
    fanSpeed += 25;
    if(fanSpeed >100) { fanSpeed = 25; }
    setFanSpeed(fanSpeed);
    writePercentageToLeds(fanSpeed,false);
    sendToMqtt();
    while(digitalRead(IO_INPUT_SPEED)) {}
    delay(10);
  }
  if(digitalRead(IO_INPUT_ONOFF)) {
    isFanRunning = !isFanRunning;
    fanSpeed = isFanRunning ? 25 :0;
    setFanSpeed(fanSpeed);
    writePercentageToLeds(fanSpeed,false);
    sendToMqtt();
    while(digitalRead(IO_INPUT_ONOFF)) {}
    delay(10);
  }
}
