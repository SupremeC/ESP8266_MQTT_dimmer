// 0-10v DIMMER over WIFI using MQTT and Home Assistant

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <math.h>


//================================================
//================================================
//   EDIT THESE LINES TO MATCH YOUR SETUP
//================================================
//================================================
#define MQTT_SERVER "192.168.1.170"  //you MQTT IP Address
#define HOSTNAME "esp8266-UpperStaircaseHndLight" //MaxLength=32
const char* ssid = "WIFI SSID";
const char* password = "SECRETE WIFI PWD";
const int dimmerPin = D1; // Data PIN Assignment on WEMOS D1 R2 https://www.wemos.cc/product/d1.html
char const* commandTopic = "home/staircase/upper/handlelight/set"; // Refer to Home Assistant YAML Configuration component entry
char const* stateTopic = "home/staircase/upper/handlelight/state"; // Refer to Home Assistant YAML Configuration component entry
char const* statusTopic = "home/staircase/upper/handlelight"; // Refer to Home Assistant YAML Configuration component entry



void callback(char* topic, byte* payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient client(MQTT_SERVER, 1883, callback, wifiClient);





void setup() {
  //initialize the PWN pin as an output and set to LOW (off)
  pinMode(dimmerPin, OUTPUT); // PWM output to OP-AMP circuit
  analogWrite(dimmerPin, 0); // turn off Dimmer on startup

  // Initialize the LED_BUILTIN pin as an output
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // onboard LED ON;  (Inversed)

  Serial.begin(115200);
  delay(100);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname(HOSTNAME);
  // No authentication by default
  // ArduinoOTA.setPassword("ampzilla");
  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
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
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //wait a bit before starting the main loop
  delay(2000);

  digitalWrite(LED_BUILTIN, HIGH); // onboard LED OFF;  (Inversed)
}





void loop() {
  //Mandatory to allow OTA uodates (Updates over the air)
  ArduinoOTA.handle();

  //reconnect if connection is lost
  if (!client.connected() && WiFi.status() == 3) {
    reconnect();
  }

  //maintain MQTT connection
  client.loop();

  //MUST delay to allow ESP8266 WIFI functions to run
  delay(50);
}




void callback(char* topic, byte* payload, unsigned int length) {
  digitalWrite(LED_BUILTIN, LOW); // onboard LED ON;  (Inversed)

  //convert to string to make it easier to work with
  String topicStr = topic;
  String pl = payloadToString(payload, length);
  String state = "";

  if (topicStr == commandTopic)
  {
    //Logic:
    // Lowest value of PWM output to turn light ON  is 82, highest is 1024
    const double rate = 9.32323232; //   1024-101/99
    int dimmerVal = 101 + (rate * (atof(pl.c_str()) - 1));
    if (dimmerVal < 101) {
      // The light is OFF = 0
      dimmerVal = 0;
      state = "0";
    }
    else {
      // The light is ON = 100
      state = "100";
    }
    if (dimmerVal > 1024) {
      // Max value
      dimmerVal = 1024;
    }


    //Print out some debugging info
    Serial.print("Topic: ");
    Serial.println(topicStr);
    Serial.print("Payload: ");
    Serial.println(pl);
    Serial.print("Payload Length: ");
    Serial.println(length);
    Serial.print("DimmerVal => ");
    Serial.println(dimmerVal);


    analogWrite(dimmerPin, dimmerVal);
    client.publish(statusTopic, pl.c_str(), true);
    client.publish(stateTopic, state.c_str(), true);
  }
  digitalWrite(LED_BUILTIN, HIGH); // onboard LED OFF;  (Inversed)
}


void reconnect() {
  //attempt to connect to the wifi if connection is lost
  if (WiFi.status() != WL_CONNECTED) {
    //debug printing
    Serial.print("Connecting to ");
    Serial.println(ssid);

    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    //print out some more debug once connected
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  //make sure we are connected to WIFI before attemping to reconnect to MQTT
  if (WiFi.status() == WL_CONNECTED) {
    // Loop until we're reconnected to the MQTT server
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");

      // Generate client name based on MAC address and last 8 bits of microsecond counter
      String clientName;
      clientName += "esp8266-";
      uint8_t mac[6];
      WiFi.macAddress(mac);
      clientName += macToStr(mac);

      //if connected, subscribe to the topic(s) we want to be notified about
      //EJ: Delete "mqtt_username", and "mqtt_password" here if you are not using any
      //if (client.connect((char*) clientName.c_str(),"mqtt_username", "mqtt_password")) {  //EJ: Update accordingly with your MQTT account
      if (client.connect((char*) clientName.c_str(), "" , "")) {
        Serial.print("\tMQTT Connected");
        client.subscribe(commandTopic);
      }

      //otherwise print failed for debugging
      else {
        Serial.println("\tFailed.");
        abort();
      }
    }
  }
}


String payloadToString(byte* payload, unsigned int length)
{
  String pl = "\0";
  if (length != 0)
  {
    payload[length] = '\0';
    pl = String((char*)payload);

    if (!isValidNumber(pl))
    {
      pl = "0\0";
    }
  }
  else
  {
    pl = "0\0";
  }

  return pl;
}


boolean isValidNumber(String str) {
  for (byte i = 0; i < str.length(); i++)
  {
    if (isDigit(str.charAt(i))) return true;
  }
  return false;
}


//generate unique name from MAC addr
String macToStr(const uint8_t* mac) {

  String result;

  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);

    if (i < 5) {
      result += ':';
    }
  }

  return result;
}

