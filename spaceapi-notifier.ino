/*
   This sketch displays a message on a LED matrix display when a given spaceapi endpoint tells a space is open
   Based off a Nodemcu 0.9 (or v3) and MAX72XX-based LED matrix display

   Pinout:
   D1 mini       Matrix display
      5V              VCC
      GND             GND
      D5              DIN
      D6              CS
      D7              CLK
   Yes, these pins are both sequential on both ends
*/

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#define ARDUINOJSON_DECODE_UNICODE 1
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>          // https://tttapa.github.io/ESP8266/Chap08%20-%20mDNS.html

#include <WiFiClientSecure.h>

//for LED status
#include <Ticker.h>
Ticker ledTicker;

const byte LED_PIN = LED_BUILTIN; // 13 for Sonoff S20, 2 for NodeMCU/ESP12 internal LED
const byte BUTTON_PIN = 0;

bool spaceIsOpen = false;

#include <MD_Parola.h> // https://github.com/MajicDesigns/MD_Parola/blob/main/examples/Parola_HelloWorld/Parola_HelloWorld.ino
#include <MD_MAX72xx.h>
#include <SPI.h>

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define DATA_PIN  D5  // or MOSI
#define CS_PIN    D6  // or SS
#define CLK_PIN   D7  // or SCK

// SPI hardware interface
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

void blinkLED() {
  //toggle state
  bool state = digitalRead(LED_PIN);  // get the current state of LED_PIN pin
  digitalWrite(LED_PIN, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode (for LED status)
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ledTicker.attach(0.5, blinkLED);
}


//define your default values here, if there are different values in config.json, they are overwritten.
String displayedMessage = "FUZ";
String spaceAPIEndpoint = "https://spaceapi.fuz.re/";

//flag for saving data
bool shouldSaveConfig = false;

//callback checking us of the need to save config
void saveConfigCallback () {
  Serial.println(F("Should save config"));
  shouldSaveConfig = true;
}


WiFiManager wifiManager;

#include <ESP8266HTTPClient.h>    // for https://github.com/matt-williams/matrix-esp8266/blob/master/matrix-esp8266.ino

ESP8266WebServer httpServer(80); // webserver on port 80 https://github.com/esp8266/Arduino/blob/14262af0d19a9a3b992d5aa310a684d47b6fb876/libraries/ESP8266WebServer/examples/AdvancedWebServer/AdvancedWebServer.ino
void handleRoot() {
  for (int i = 0; i < httpServer.args(); i++) {
    if (httpServer.argName(i) == "resetesp") {
      httpServer.sendHeader("Location", httpServer.uri(), true);
      httpServer.send(302, "text/plain", "");
      delay(500);
      ESP.restart();
      return;
    }
  }

  if (httpServer.method() == HTTP_POST) {
    for (int i = 0; i < httpServer.args(); i++) {
      if (httpServer.argName(i) == "displayedMessage") {
        displayedMessage = httpServer.arg(i);
        continue;
      }
      if (httpServer.argName(i) == "spaceAPIEndpoint") {
        spaceAPIEndpoint = httpServer.arg(i);
        continue;
      }
    }
    Serial.println(F("saving config"));
    const size_t capacity = JSON_OBJECT_SIZE(3) + 440; // https://arduinojson.org/v6/assistant/
    DynamicJsonDocument jsonBuffer(capacity);
    jsonBuffer["displayedMessage"] = displayedMessage;
    jsonBuffer["spaceAPIEndpoint"] = spaceAPIEndpoint;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    }

    serializeJson(jsonBuffer, Serial);
    Serial.println();
    serializeJson(jsonBuffer, configFile);
    configFile.close();
  }
  if (httpServer.args() > 0 || httpServer.method() == HTTP_POST) { // trim GET parameters and prevent resubmiting same form on refresh
    httpServer.sendHeader("Location", httpServer.uri(), true);
    return httpServer.send(302, "text/plain", "");
  }

  String html =
    String("<!DOCTYPE HTML><html>") +
    "<head><meta charset=utf-8><title>SpaceAPI notifier</title></head><body>" +
    "status: " + (spaceIsOpen ? "opened" : "closed") + "<br>" +
    "<a href='?resetesp'>Reboot ESP</a>" + "<br><br>" +
    "<form method='post'>" +
    "<div><label for='displayedMessage'>displayedMessage  </label><input name='displayedMessage' id='displayedMessage' value='" + displayedMessage + "'></div>" +
    "<div><label for='spaceAPIEndpoint'>spaceAPIEndpoint  </label><input name='spaceAPIEndpoint' id='spaceAPIEndpoint' value='" + spaceAPIEndpoint + "'></div>" +
    "<div><button>Submit</button></div></form>"
    "</body></html>";
  httpServer.send(200, "text/html", html);
}
void handleNotFound() {
  httpServer.send(404, "text/plain", httpServer.uri() + " not found");
}

BearSSL::WiFiClientSecure secureClient;
HTTPClient http;
bool checkSpaceIsOpen() {
  http.useHTTP10(true); // https://arduinojson.org/v6/how-to/use-arduinojson-with-httpclient/
  http.begin(secureClient, spaceAPIEndpoint);
  int httpCode = http.GET();
  //Serial.println("checkSpaceIsOpen body: " + http.getString());
  Serial.println("checkSpaceIsOpen return code: " + String(httpCode));

  ledTicker.detach();
  digitalWrite(LED_PIN, LOW); // ensure the LED is lit
  if (httpCode != 200) { // something is wrong, bad network or misconfigured credentials
    ledTicker.attach(0.1, blinkLED);
  }
  StaticJsonDocument<16> filter; // https://arduinojson.org/v6/assistant/
  filter["state"] = true;

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    ledTicker.attach(0.1, blinkLED);
    return false;
  } else {
    Serial.println(F("deserializeJson() successful:"));
  }

  bool state_open = doc["state"]["open"]; // false

  serializeJsonPretty(doc, Serial);
  Serial.println();
  http.end();

  bool m0 = doc["state"]["open"];
  return m0;
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  //set led pin as output
  pinMode(LED_PIN, OUTPUT);
  //set button pin as input
  pinMode(BUTTON_PIN, INPUT);

  // start ledTicker with 1 because we start in AP mode and try to connect
  ledTicker.attach(1.1, blinkLED);

  Serial.println(F("mounting FS..."));

  if (SPIFFS.begin()) {
    Serial.println(F("mounted file system"));
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println(F("reading config file"));
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println(F("opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        const size_t capacity = JSON_OBJECT_SIZE(3) + 440; // https://arduinojson.org/v6/assistant/
        DynamicJsonDocument jsonBuffer(capacity);
        auto error = deserializeJson(jsonBuffer, buf.get());
        if (error) {
          Serial.print(F("deserializeJson() failed with code "));
          Serial.println(error.c_str());
          return;
        } else {
          Serial.println(F("deserializeJson() successful:"));

          serializeJsonPretty(jsonBuffer, Serial);
          Serial.println();

          String m0 = jsonBuffer["displayedMessage"];
          displayedMessage = m0;
          String m1 = jsonBuffer["spaceAPIEndpoint"];
          spaceAPIEndpoint = m1;
        }
        configFile.close();
      }
    }
  } else {
    Serial.println(F("failed to mount FS"));
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter customDisplayedMessage("Displayed message", "Displayed message", displayedMessage.c_str(), 50);
  WiFiManagerParameter customSpaceAPIEndpoint("SpaceAPI endpoint", "SpaceAPI endpoint", spaceAPIEndpoint.c_str(), 200);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  //WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode (for status LED)
  wifiManager.setAPCallback(configModeCallback);

  //set config save check callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //  wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&customDisplayedMessage);
  wifiManager.addParameter(&customSpaceAPIEndpoint);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "SPACEAPI NOTIFIER"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("SPACEAPI NOTIFIER")) {
    Serial.println(F("failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println(F("connected...yeey :)"));
  ledTicker.detach();
  //keep LED on
  digitalWrite(LED_PIN, LOW);

  //read updated parameters
  displayedMessage = customDisplayedMessage.getValue();
  spaceAPIEndpoint = customSpaceAPIEndpoint.getValue();

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println(F("saving config"));
    const size_t capacity = JSON_OBJECT_SIZE(3) + 440; // https://arduinojson.org/v6/assistant/
    DynamicJsonDocument jsonBuffer(capacity);
    jsonBuffer["displayedMessage"] = displayedMessage;
    jsonBuffer["spaceAPIEndpoint"] = spaceAPIEndpoint;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    }

    serializeJson(jsonBuffer, Serial);
    Serial.println();
    serializeJson(jsonBuffer, configFile);
    configFile.close();
    //end save
  }

  Serial.println(F("local ip:"));
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.SSID());

  httpServer.on("/", handleRoot);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println("HTTP server started");
  if (!MDNS.begin("spaceapi-notifier")) { // https://github.com/esp8266/Arduino/blob/14262af0d19a9a3b992d5aa310a684d47b6fb876/libraries/ESP8266mDNS/examples/mDNS_Web_Server/mDNS_Web_Server.ino
    Serial.println("Error setting up MDNS responder!");
  } else  {
    Serial.println("mDNS responder started");
  }
  MDNS.addService("http", "tcp", 80);

  secureClient.setInsecure();
  P.begin();
  P.setIntensity(7); // yeah don't blind people
}

bool buttonState = HIGH;
bool previousButtonState = HIGH;
unsigned long pressedTime = millis();
void handleButtonPresses(int buttonState) {
  if (buttonState == LOW && previousButtonState == HIGH) { // long press handling, reset settings https://forum.arduino.cc/index.php?topic=276789.msg1947963#msg1947963
    Serial.println("Button pressed (longpress handling)");
    pressedTime = millis();
  }
  if (buttonState == LOW && previousButtonState == LOW && (millis() - pressedTime) > 5000) {
    Serial.println("Button STILL pressed (longpress handling)");
    wifiManager.resetSettings();
    SPIFFS.format();
    delay(500);
    ESP.restart();
  }
}

#define OFFSET 7.5
#define AMPLITUDE 7.5 // oscillate between 0 and 15
#define PERIOD 2500 // milliseconds
#define OMEGA 2*PI/PERIOD
#define PHASE 0
void fadeDisplay(unsigned long t) {
  uint8_t brightness = round(OFFSET + AMPLITUDE * (cos(OMEGA * t) + PHASE));
  Serial.print("fadeDisplay ");
  Serial.print(t);
  Serial.print(" ");
  Serial.println(brightness);
  P.setIntensity(brightness);
}


unsigned long monCulSurLaCommode = 0;

unsigned long fadeDisplayMillis = millis();
long previousMillis = 0;
const long checkInterval = 5000;
void loop() {
  MDNS.update();

  httpServer.handleClient();

  buttonState = digitalRead(BUTTON_PIN);
  handleButtonPresses(buttonState);

  unsigned long currentMillis = millis();
  long checkSpaceDelayMillis = 0;
  if (currentMillis - previousMillis > checkInterval) {
    previousMillis = currentMillis;
    spaceIsOpen = checkSpaceIsOpen();
    checkSpaceDelayMillis = millis() - currentMillis; // try to smooth the fade animation when HTTP calls happen
    
  }

  if (P.displayAnimate()) {
    if (spaceIsOpen) {
      P.displayText(displayedMessage.c_str(), PA_CENTER, P.getSpeed(), P.getPause(), PA_NO_EFFECT, PA_NO_EFFECT);
    } else {
      P.displayText(".", PA_CENTER, P.getSpeed(), P.getPause(), PA_NO_EFFECT, PA_NO_EFFECT);
    }
  }
  if (checkSpaceDelayMillis > 0) {
    Serial.print("millis | checkSpaceDelayMillis ");
    Serial.print(millis());
    Serial.print(" | ");
    Serial.println(checkSpaceDelayMillis);
  }
  fadeDisplayMillis = millis() - fadeDisplayMillis - checkSpaceDelayMillis + millis();

  monCulSurLaCommode+=2; // should be using millis()-based var, fadeDisplayMillis increment taking into account checkSpaceDelayMillis each time
  fadeDisplay(monCulSurLaCommode);

  previousButtonState = buttonState;
}
