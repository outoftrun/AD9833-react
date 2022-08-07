/*
  Graph - A web-based Graph display of ESP8266 data

  This file is part of the ESP8266WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  See readme.md for more information.
*/

////////////////////////////////

// Select the FileSystem by uncommenting one of the lines below

//#define USE_SPIFFS
#define USE_LITTLEFS
//#define USE_SDFS

////////////////////////////////

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SPI.h>
#include <MD_AD9833.h>
#include <ArduinoJson.h>

#if defined USE_SPIFFS
#include <FS.h>
FS* fileSystem = &SPIFFS;
SPIFFSConfig fileSystemConfig = SPIFFSConfig();
#elif defined USE_LITTLEFS
#include <LittleFS.h>
FS* fileSystem = &LittleFS;
LittleFSConfig fileSystemConfig = LittleFSConfig();
#elif defined USE_SDFS
#include <SDFS.h>
FS* fileSystem = &SDFS;
SDFSConfig fileSystemConfig = SDFSConfig();
// fileSystemConfig.setCSPin(chipSelectPin);
#else
#error Please select a filesystem first by uncommenting one of the "#define USE_xxx" lines at the beginning of the sketch.
#endif


#define DBG_OUTPUT_PORT Serial

#ifndef STASSID
#define STASSID "dlink-018E"
#define STAPSK  "qwertyui"
#endif

// Indicate which digital I/Os should be displayed on the chart.
// From GPIO16 to GPIO0, a '1' means the corresponding GPIO will be shown
// e.g. 0b11111000000111111
unsigned int gpioMask;

const char* ssid = STASSID;
const char* password = STAPSK;
const char* host = "graph";

ESP8266WebServer server(80);

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";
static void ses() ;
////////////////////////////////
// Utils to return HTTP codes

void replyOK() {
  server.send(200, FPSTR(TEXT_PLAIN), "");
}

void replyOKWithMsg(String msg) {
  server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void replyNotFound(String msg) {
  server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void replyBadRequest(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

////////////////////////////////
// Request handlers

/*
   Read the given file from the filesystem and stream it back to the client
*/
bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println(String("handleFileRead: ") + path);

  if (path.endsWith("/")) {
    path += "index.html";
  }

  String contentType = mime::getContentType(path);

  if (!fileSystem->exists(path)) {
    // File not found, try gzip version
    path = path + ".gz";
  }
  if (fileSystem->exists(path)) {
    File file = fileSystem->open(path, "r");
    if (server.streamFile(file, contentType) != file.size()) {
      DBG_OUTPUT_PORT.println("Sent less data than expected!");
    }
    file.close();
    return true;
  }

  return false;
}


/*
   The "Not Found" handler catches all URI not explicitly declared in code
   First try to find and return the requested file from the filesystem,
   and if it fails, return a 404 page with debug information
*/
void handleNotFound() {
  String uri = ESP8266WebServer::urlDecode(server.uri()); // required to read paths with blanks

  if (handleFileRead(uri)) {
    return;
  }

  // Dump debug data
  String message;
  message.reserve(100);
  message = F("Error: File not found\n\nURI: ");
  message += uri;
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += '\n';
  for (uint8_t i = 0; i < server.args(); i++) {
    message += F(" NAME:");
    message += server.argName(i);
    message += F("\n VALUE:");
    message += server.arg(i);
    message += '\n';
  }
  message += "path=";
  message += server.arg("path");
  message += '\n';
  DBG_OUTPUT_PORT.print(message);

  return replyNotFound(message);
}


//const int cs = 0;   // gpio16
//const int data = 5;    // gpio14
//const int clock_ = 6; //gpio12
//const int fsync = 7; //gpio13
//const int   gnd_ = 8; // gpio15
// Pins for SPI comm with the AD9833 IC
#define DATA  14  ///< SPI Data pin number  gpio14
#define CLK   12  ///< SPI Clock pin number gpio12
#define FSYNC 13  ///< SPI Load pin number (FSYNC in AD9833 usage) gpio13
const int ledPin = 2;

//MD_AD9833  AD(FSYNC);  // Hardware SPI
MD_AD9833  AD(DATA, CLK, FSYNC); // Arbitrary SPI pins

void setup(void)
{
  pinMode(ledPin, OUTPUT);

  AD.begin();
  AD.reset();
  delay(1000);


  ////////////////////////////////
  // SERIAL INIT
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);

  AD.setFrequency( MD_AD9833::CHAN_0, 1000);
  ////////////////////////////////
  // FILESYSTEM INIT

  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);
  bool fsOK = fileSystem->begin();
  DBG_OUTPUT_PORT.println(fsOK ? F("Filesystem initialized.") : F("Filesystem init failed!"));

  ////////////////////////////////
  // PIN INIT
  pinMode(4, INPUT);
  //  pinMode(12, OUTPUT);
  //  pinMode(13, OUTPUT);
  //  pinMode(15, OUTPUT);

  ////////////////////////////////
  // WI-FI INIT
  DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }
  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print(F("Connected! IP address: "));
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  ////////////////////////////////
  // MDNS INIT
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.print(F("Open http://"));
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(F(".local to open the graph page"));
  }
  DBG_OUTPUT_PORT.print("\nActiveFrequency =");
  DBG_OUTPUT_PORT.println(AD.getActiveFrequency());
  ////////////////////////////////
  // WEB SERVER INIT

  //get heap status, analog input value and all GPIO statuses in one json call
  server.on("/espData", HTTP_GET, []() {
    String json;
    json.reserve(88);
    json = "{\"time\":";
    json += millis();
    json += ", \"heap\":";
    json += ESP.getFreeHeap();
    json += ", \"analog\":";
    json += analogRead(A0);
    json += ", \"gpioMask\":";
    json += gpioMask;
    json += ", \"gpioData\":";
    json += (uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16));
    json += "}";
    server.send(200, "text/json", json);
  });
  server.on(F("/ad9833"), HTTP_POST, set);
  // Default handler for all URIs not defined above
  // Use it to read files from filesystem
  server.onNotFound(handleNotFound);


  // Start server
  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");

  DBG_OUTPUT_PORT.println("Please pull GPIO4 low (e.g. press button) to switch output mode:");
  DBG_OUTPUT_PORT.println(" 0 (OFF):    outputs are off and hidden from chart");
  DBG_OUTPUT_PORT.println(" 1 (AUTO):   outputs are rotated automatically every second");
  DBG_OUTPUT_PORT.println(" 2 (MANUAL): outputs can be toggled from the web page");

}


static const char * phaseString = "phase";
static const char * frequencyString = "frequency";
static const char * typeString = "type";
static const char * channelString = "channel";

// Serving Hello world
static void set() {
  String errorString = F("No data found,");
  String postBody = server.arg("set");
  Serial.println(postBody);

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, postBody);
  if (error) {
    // if the file didn't open, print an error:
    Serial.print(F("Error parsing JSON "));
    Serial.println(error.c_str());

    String msg = error.c_str();

    server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);

  } else {

    JsonObject postObj = doc.as<JsonObject>();


    Serial.print(F("HTTP Method: "));
    Serial.println(server.method());

    if (server.method() == HTTP_POST) {
      if (postObj.containsKey(channelString)) {
        float frequency ;
        MD_AD9833::channel_t channel;
        MD_AD9833::mode_t type;

        // Here store data or doing operation
        unsigned char cha = postObj[channelString];
        Serial.printf("%s \t=\t%d\n", channelString, channel);
        if (cha == 0) {
          channel = MD_AD9833::CHAN_0;
        }
        else if (cha == 1) {
          channel = MD_AD9833::CHAN_0;
        }
        else {
          errorString = F("incorrect Channel");
          goto error;
        }

        if (postObj.containsKey(typeString)) {
          String typeToken = postObj[typeString];

          if (typeToken.equalsIgnoreCase( F("Square"))) {
            type = MD_AD9833:: MODE_SQUARE1;
          }
          else if (typeToken.equalsIgnoreCase( F("Square1"))) {
            type =  MD_AD9833::MODE_SQUARE1;
          }
          else if (typeToken.equalsIgnoreCase( F("Square2"))) {
            type = MD_AD9833:: MODE_SQUARE2;
          }
          else if (typeToken.equalsIgnoreCase(F("Sine"))) {
            type = MD_AD9833::MODE_SINE;
          }
          else if (typeToken.equalsIgnoreCase(F("Triangle"))) {
            type =   MD_AD9833::MODE_TRIANGLE;
          }
          else if (typeToken.equalsIgnoreCase(F("Off"))) {
            type = MD_AD9833::MODE_OFF;
          }
          else {
            errorString = F("incorrect type");
            goto error;
          }
          Serial.printf("%s \t\t=\t%d\n" , typeToken.c_str(), type);

          AD.setMode(type);

        }

        if (postObj.containsKey(frequencyString)) {
          frequency = postObj[frequencyString];

          Serial.printf("%s \t=\t%f\n", frequencyString, frequency);
          AD.setFrequency(channel, frequency);
        }
        if ( postObj.containsKey(phaseString)) {
          float phase = postObj[phaseString];
          Serial.printf("%s \t\t=\t%f\n", phaseString, phase);
          AD.setPhase(channel, (((int)phase) * 10) % 3600 );

        }

done:
        // Create the response
        // To get the status of the result you can get the http status so
        // this part can be unusefully
        Serial.println(F("done."));
        DynamicJsonDocument doc(512);
        doc["status"] = "OK";

        Serial.print(F("Stream..."));
        String buf;
        serializeJson(doc, buf);

        server.send(201, F("application/json"), buf);
        Serial.print(F("done."));
        return;
      }
error:
      DynamicJsonDocument doc(512);
      doc["status"] = "KO";
      doc["message"] = errorString;

      Serial.print(F("Stream..."));
      String buf;
      serializeJson(doc, buf);

      server.send(400, F("application/json"), buf);
      Serial.print(F("done."));

    }
  }
}
// Return default GPIO mask, that is all I/Os except SD card ones
unsigned int defaultMask() {
  unsigned int mask = 0b11111111111111111;
  for (auto pin = 0; pin <= 16; pin++) {
    if (isFlashInterfacePin(pin)) {
      mask &= ~(1 << pin);
    }
  }
  return mask;
}

int rgbMode = 1; // 0=off - 1=auto - 2=manual
int rgbValue = 0;
esp8266::polledTimeout::periodicMs timeToChange(1000);
bool modeChangeRequested = false;

void loop(void) {
  server.handleClient();
  MDNS.update();

  if (digitalRead(4) == 0) {
    // button pressed
    modeChangeRequested = true;
  }

  // see if one second has passed since last change, otherwise stop here
  if (!timeToChange) {
    return;
  }

  // see if a mode change was requested
  if (modeChangeRequested) {
    // increment mode (reset after 2)
    rgbMode++;
    if (rgbMode > 2) {
      rgbMode = 0;
    }

    modeChangeRequested = false;
  }

  // act according to mode
  switch (rgbMode) {
    case 0: // off
      gpioMask = defaultMask();
      gpioMask &= ~(1 << 12); // Hide GPIO 12
      gpioMask &= ~(1 << 13); // Hide GPIO 13
      gpioMask &= ~(1 << 15); // Hide GPIO 15

      // reset outputs
      digitalWrite(12, 0);
      digitalWrite(13, 0);
      digitalWrite(15, 0);
      break;

    case 1: // auto
      gpioMask = defaultMask();

      // increment value (reset after 7)
      rgbValue++;
      if (rgbValue > 7) {
        rgbValue = 0;
      }

      // output new values
      digitalWrite(12, rgbValue & 0b001);
      digitalWrite(13, rgbValue & 0b010);
      digitalWrite(15, rgbValue & 0b100);
      break;

    case 2: // manual
      gpioMask = defaultMask();

      // keep outputs unchanged
      break;
  }
}
