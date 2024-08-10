#include <PubSubClient.h>
#include <InternalStorage.h>
#include <OTAStorage.h>
#include <SDStorage.h>
#include <WiFi101OTA.h>
#include <SPI.h>
#include <WiFi101.h>

#include "private.h" 

#define DEBUG         false // Set to true to enable Serial debug
#define TRIGGER_PIN   6  // Board pin tied to trigger pin on the ultrasonic sensor.
#define ECHO_PIN      7  // Board pin tied to echo pin on the ultrasonic sensor.
#define MAX_DISTANCE  300 // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.

const float SOUND_SPEED_CM_MICROS = 0.0343;
const int TRIGGER_PULSE_DELAY_MICROS = 10;
const int POST_TRIGGER_DELAY_MICROS = 2;
const int DISTANCE_PIN_DIVISOR = 166;
const float ERROR_VALUE = -1.0; // Error value to return on failure
const float PING_ERROR_VALUE = -1.0; // Error value to return on failure

int status = WL_IDLE_STATUS;     // the WiFi radio's status

/////// Wifi Settings ///////
const char* ssid = SECRET_SSID;      // your network SSID (name)
const char* pass = SECRET_PASS;   // your network password
const char* user = SECRET_OTAUSER;
const char* otapass = SECRET_OTAPASS;

/////// MQTT Settings ///////
const char* mqtt_ip_addr = MQTT_SERVER_IP;  
const char* mqtt_user = MQTT_USERNAME;
const char* mqtt_pass = MQTT_PASSWORD;

const char* ID = "Car_Sensor";  // Name of our device, must be unique
const char* TOPIC_STATUS = "home/garage/car/parkingsensor/status";  // Topic to publish to
const char* TOPIC_CURRENT_DISTANCE = "home/garage/car/parkingsensor/distance";  // Topic to publish to
const char* TOPIC_VARIABLE_DISTANCE = "home/garage/car/parkingsensor/var_distance";  // Topic to publish to
const char* TOPIC_DELTA_DISTANCE = "home/garage/car/parkingsensor/delta_distance";  // Topic to publish to
const char* TOPIC_POSMARGIN_DISTANCE = "home/garage/car/parkingsensor/posmargin_distance";  // Topic to publish to
const char* TOPIC_NEGMARGIN_DISTANCE = "home/garage/car/parkingsensor/negmargin_distance";  // Topic to publish to

// IO Pins
const int redLEDPin = 0;
const int yellowLEDPin = 1;
const int greenLEDPin = 2;
const int blueLEDPin = 3;
const int distancePin = 15;

// Variables
int blueLEDState = LOW;         // the current state of the output pin
int countDown = 0;
int deltaDistance;
float distance, varianceDistance, posMargin, negMargin;
String current_distance_payload, current_variable_payload,current_delta_payload, current_posmargin_payload, current_negmargin_payload;

// User configurable values
int setDistance = 70;

WiFiClient wclient;
PubSubClient mqtt_client(mqtt_ip_addr, 1883, wclient); // Setup MQTT client

float ping() {
  unsigned long duration;
  float distance;

  // Read the distance pin (potentiometer) to get the margin of error
  varianceDistance = analogRead(distancePin) / (float)DISTANCE_PIN_DIVISOR;
  
  // Trigger the ultrasonic pulse
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(POST_TRIGGER_DELAY_MICROS);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(TRIGGER_PULSE_DELAY_MICROS);
  digitalWrite(TRIGGER_PIN, LOW);
  
  // Measure the duration of the echo
  duration = pulseIn(ECHO_PIN, HIGH);

  // Check for timeout (no echo within the timeout period)
  if (duration == 0) {
    Serial.println("Ping timeout - no echo received");
    return ERROR_VALUE;
  }

  // Calculate the distance
  distance = (duration * SOUND_SPEED_CM_MICROS) / 2;

  // Check for out-of-range values
  if (distance < MIN_REASONABLE_DISTANCE || distance > MAX_REASONABLE_DISTANCE) {
    Serial.print("Unreasonable distance measured: ");
    Serial.println(distance);
    return ERROR_VALUE;
  }
  
  return distance;
}

void setup() {
  pinMode(TRIGGER_PIN, OUTPUT); 
  pinMode(ECHO_PIN, INPUT); 
  pinMode(redLEDPin, OUTPUT);
  pinMode(yellowLEDPin, OUTPUT);
  pinMode(greenLEDPin, OUTPUT);
  pinMode(blueLEDPin, OUTPUT);
  offLEDs();
 
  Serial.begin(9600);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }

  // attempt to connect to WiFi network:
  while ( status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }

  // you're connected now, so print out the data:
  Serial.print("You're connected to the network");
  WiFiOTA.begin(user, otapass, InternalStorage);
  printCurrentNet();
  printWiFiData();
  if (mqtt_client.connect(ID, mqtt_user, mqtt_pass)) {
    Serial.println("Client connected to MQTT Server.");
  } else {
    Serial.println("Client not connect to MQTT Server!");
  }
}

boolean reconnect() {
  if (mqtt_client.connect(ID, mqtt_user, mqtt_pass)) {
    Serial.println("Client connected to MQTT Server.");
  }
  return mqtt_client.connected();
}

void loop() {
  WiFiOTA.poll();

  if (!mqtt_client.connected())  // Reconnect if connection is lost
  {
    reconnect();
  }
  mqtt_client.loop();
  
  float currentDistance = ping();
  if (currentDistance == PING_ERROR_VALUE) {
    Serial.println("Error: Ping failed. Check sensor.");
    // Handle the error
    digitalWrite(redLEDPin, HIGH); // Turn on red LED to indicate error
    while (true); // Halt execution
  }
  
  deltaDistance = currentDistance - setDistance;
  
  negMargin = setDistance - varianceDistance;
  posMargin = setDistance + varianceDistance;
  
  Serial.print("Current Distance: ");
  Serial.println(currentDistance);
  current_distance_payload = (String)currentDistance;
  current_variable_payload = (String)varianceDistance;
  current_delta_payload = (String)deltaDistance;
  current_posmargin_payload = (String)posMargin;
  current_negmargin_payload = (String)negMargin;

  delay(50); // Wait 50ms between pings (about 20 pings/sec). 29ms should be the shortest delay between pings.

  Serial.print("Delta value: ");
  Serial.println(deltaDistance);
  if (countDown == 0) {
    mqtt_client.publish(TOPIC_CURRENT_DISTANCE, (char*)current_distance_payload.c_str());
    mqtt_client.publish(TOPIC_VARIABLE_DISTANCE, (char*)current_variable_payload.c_str());
    mqtt_client.publish(TOPIC_DELTA_DISTANCE, (char*)current_delta_payload.c_str());
    mqtt_client.publish(TOPIC_POSMARGIN_DISTANCE, (char*)current_posmargin_payload.c_str());
    mqtt_client.publish(TOPIC_NEGMARGIN_DISTANCE, (char*)current_negmargin_payload.c_str());
  }
  if ((deltaDistance > varianceDistance) && (deltaDistance <= 600)) { // To far away
    Serial.println("Too far away - Yellow LED");
    if (countDown == 0) {
      mqtt_client.publish(TOPIC_STATUS, "Yellow");
    }
    yellowLED();
  } else if (deltaDistance < 0) { // To close
    Serial.println("Too close - Red LED");
    if (countDown == 0) {
      mqtt_client.publish(TOPIC_STATUS, "Red");
    }
    redLED();
  //} else if ((deltaDistance >= 0) && (deltaDistance <= varianceDistance)) { // Correct distance
    //Serial.println("Just Right - Green LED");
  //  if (countDown == 0) {
  //    mqtt_client.publish(TOPIC_STATUS, "Green");
  //  }
  //  greenLED();
  } else if ((negMargin <= currentDistance) && (posMargin >= currentDistance)) {
    Serial.println("Just Right - Green LED");
    if (countDown == 0) {
      mqtt_client.publish(TOPIC_STATUS, "Green");
    }
    greenLED();
  } else { // Object is too far away
    Serial.println("Object too far away - Turning LEDs Off");
    if (countDown == 0) {
      mqtt_client.publish(TOPIC_STATUS, "None");
    }
    offLEDs();
  }
  if (countDown == 0) {
    countDown = 20;
  } else {
    countDown--;
  }
}

void redLED()
{
  offLEDs();
  if ( !digitalRead(redLEDPin) ) {
    digitalWrite(redLEDPin, HIGH);
  }
}

void yellowLED()
{
  offLEDs();
  if ( !digitalRead(yellowLEDPin) ) {
    digitalWrite(yellowLEDPin, HIGH);
  }
}

void greenLED()
{
  offLEDs();
  if ( !digitalRead(greenLEDPin) ) {
    digitalWrite(greenLEDPin, HIGH);
  }
}

void blueLED()
{
  offLEDs();
  if ( !digitalRead(blueLEDPin) ) {
    digitalWrite(blueLEDPin, HIGH);
  }
}

void offLEDs()
{
  if ( digitalRead(redLEDPin) ||  digitalRead(yellowLEDPin) || digitalRead(greenLEDPin) ) {
    Serial.println("Turning Off LEDs");
    digitalWrite(redLEDPin, LOW);
    digitalWrite(yellowLEDPin, LOW);
    digitalWrite(greenLEDPin, LOW);
  }
}

void printWiFiData() {
  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
  Serial.println(ip);

  // print your MAC address:
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC address: ");
  printMacAddress(mac);

}

void printCurrentNet() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print the MAC address of the router you're attached to:
  byte bssid[6];
  WiFi.BSSID(bssid);
  Serial.print("BSSID: ");
  printMacAddress(bssid);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.println(rssi);

  // print the encryption type:
  byte encryption = WiFi.encryptionType();
  Serial.print("Encryption Type:");
  Serial.println(encryption, HEX);
  Serial.println();
}

void printMacAddress(byte mac[]) {
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
    if (i > 0) {
      Serial.print(":");
    }
  }
  Serial.println();
}
