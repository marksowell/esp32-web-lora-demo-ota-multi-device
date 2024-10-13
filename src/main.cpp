#include <WiFi.h>
#include <ETH.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <SPI.h>
#include <LoRa.h>
#include <deque>
#include <time.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// Define the device number (Will be loaded from preferences)
int deviceNumber = 1;

// Site ID (Will be loaded from preferences)
String siteID = "default_site";

// Wi-Fi credentials (Replace with your network credentials)
const char* ssid = "Your_SSID";
const char* password = "Your_Password";

// Time zone information
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -7 * 3600;  // GMT-7 (Mountain Time without daylight saving)
const int daylightOffset_sec = 3600;   // 1 hour for daylight saving time

// Web server on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");  // WebSocket endpoint

// NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000); // Update time every 60 seconds

// Preferences for persistent storage
Preferences preferences;

// Structure to hold log information
struct LogEntry {
  String type = "";       // "HTTP", "SYSTEM", or "LoRa"
  String message = "";    // Log message
  String srcIp = "";      // Source IP address (client IP)
  String destIp = "";     // Destination IP address (ESP32 IP)
  String timestamp = "";  // Timestamp of the log
};

// Deque to store logs with a fixed size limit
std::deque<LogEntry> logs;
const size_t MAX_LOGS = 100; // Maximum number of logs to store

// Logging settings
bool enableSystemLogs = true;
bool enableHttpLogs = true;
bool enableLoRaLogs = true;

// Function to get current time as a formatted string
String getFormattedTime() {
  time_t now;
  struct tm timeinfo;
  char buffer[80];

  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);

  return String(buffer);
}

// Function to add a system log
void addSystemLog(const String& message) {
  if (!enableSystemLogs) return;
  LogEntry entry;
  entry.type = "SYSTEM";
  entry.message = message;
  entry.srcIp = "";
  entry.destIp = "";
  entry.timestamp = getFormattedTime();

  if (logs.size() >= MAX_LOGS) {
    logs.pop_front(); // Remove the oldest log
  }
  logs.push_back(entry);
}

// Function to add an HTTP log
void addHttpLog(const String& clientIp, const String& destIp, const String& message) {
  if (!enableHttpLogs) return;
  LogEntry entry;
  entry.type = "HTTP";
  entry.srcIp = clientIp;
  entry.destIp = destIp;
  entry.message = message;
  entry.timestamp = getFormattedTime();

  if (logs.size() >= MAX_LOGS) {
    logs.pop_front(); // Remove the oldest log
  }
  logs.push_back(entry);
}

// Function to add a LoRa log
void addLoRaLog(const String& message) {
  if (!enableLoRaLogs) return;
  LogEntry entry;
  entry.type = "LoRa";
  entry.message = message;
  entry.srcIp = "";
  entry.destIp = "";
  entry.timestamp = getFormattedTime();

  if (logs.size() >= MAX_LOGS) {
    logs.pop_front(); // Remove the oldest log
  }
  logs.push_back(entry);
}

// Function to build the full URI, including query parameters
String buildFullUri(AsyncWebServerRequest *request) {
  String fullUri = request->url();
  if (request->params() > 0) {
    fullUri += "?";
    for (uint8_t i = 0; i < request->params(); i++) {
      AsyncWebParameter* p = request->getParam(i);
      fullUri += p->name() + "=" + p->value() + "&";
    }
    fullUri.remove(fullUri.length() - 1);  // Remove the trailing '&'
  }
  return fullUri;
}

// Ethernet event handler
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      ETH.setHostname("esp32-poe-iso");  // Set Hostname
      break;
    default:
      break;
  }
}

// Determine the destination IP for the current request
String getDestinationIp(AsyncWebServerRequest *request) {
  String destIp = "";
  IPAddress localIP = request->client()->localIP();
  if (localIP == WiFi.localIP()) {
    destIp = WiFi.localIP().toString();
  } else if (localIP == ETH.localIP()) {
    destIp = ETH.localIP().toString();
  }
  return destIp;
}

// Helper function to send files with CSP header
void sendFileWithCSP(AsyncWebServerRequest *request, const String& path, const String& contentType) {
  AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, contentType);
  response->addHeader("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline';");
  request->send(response);
}

// Function to validate the siteID
bool isValidSiteID(const String& siteID) {
  for (size_t i = 0; i < siteID.length(); ++i) {
    char c = siteID.charAt(i);
    if (!(isalnum(c) || c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
}

// Serve the index.html file
void handleRoot(AsyncWebServerRequest *request) {
  sendFileWithCSP(request, "/index.html", "text/html");

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);
}

// Serve the main.js file
void handleMainJS(AsyncWebServerRequest *request) {
  sendFileWithCSP(request, "/main.js", "application/javascript");

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);
}

// Serve the style.css file
void handleStyleCSS(AsyncWebServerRequest *request) {
  sendFileWithCSP(request, "/style.css", "text/css");

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);
}

// Serve the AJAX requests
void handleAjaxRequest(AsyncWebServerRequest *request) {
  if (!request->hasParam("action")) {
    request->send(400, "text/plain", "Missing action parameter");
    return;
  }

  String action = request->getParam("action")->value();

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);

  if (action == "get_logs") {
    // Estimate the size required for the JSON document
    const size_t capacity = JSON_ARRAY_SIZE(MAX_LOGS) + MAX_LOGS * JSON_OBJECT_SIZE(5) + 1024;
    StaticJsonDocument<capacity> doc;

    JsonArray logArray = doc.to<JsonArray>();

    for (size_t i = 0; i < logs.size(); ++i) {
      const LogEntry& log = logs[i];
      JsonObject logEntry = logArray.createNestedObject();
      logEntry["timestamp"] = log.timestamp;
      logEntry["type"] = log.type;
      logEntry["message"] = log.message;
      logEntry["srcIp"] = log.srcIp;
      logEntry["destIp"] = log.destIp;
    }

    // Serialize JSON to string
    String jsonString;
    serializeJson(doc, jsonString);

    request->send(200, "application/json", jsonString);
  }
  else if (action == "get_status") {
    StaticJsonDocument<1024> doc;

    doc["uptime"] = millis() / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["currentTime"] = getFormattedTime();
    doc["timeZone"] = "GMT-7 (Mountain Time)";
    doc["wifiIP"] = WiFi.localIP().toString();
    doc["ethIP"] = ETH.localIP().toString();

    String jsonString;
    serializeJson(doc, jsonString);

    request->send(200, "application/json", jsonString);
  }
  else if (action == "get_settings") {
    StaticJsonDocument<256> doc;

    doc["deviceNumber"] = deviceNumber;
    doc["siteID"] = siteID;
    doc["enableSystemLogs"] = enableSystemLogs;
    doc["enableHttpLogs"] = enableHttpLogs;
    doc["enableLoRaLogs"] = enableLoRaLogs;

    String jsonString;
    serializeJson(doc, jsonString);

    request->send(200, "application/json", jsonString);
  }
  else {
    request->send(400, "text/plain", "Bad Request");
  }
}

// Handle settings update
void handleUpdateSettings(AsyncWebServerRequest *request) {
  if (request->method() != HTTP_POST) {
    request->send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);

  if (request->hasParam("deviceNumber", true) && request->hasParam("siteID", true)) {
    String deviceNumberStr = request->getParam("deviceNumber", true)->value();
    String newSiteID = request->getParam("siteID", true)->value();

    // Validate siteID (letters, numbers, dash, underscore)
    if (!isValidSiteID(newSiteID)) {
      request->send(400, "text/plain", "Invalid Site ID");
      return;
    }

    deviceNumber = deviceNumberStr.toInt();
    siteID = newSiteID;

    // Handle logging settings
    enableSystemLogs = request->hasParam("enableSystemLogs", true);
    enableHttpLogs = request->hasParam("enableHttpLogs", true);
    enableLoRaLogs = request->hasParam("enableLoRaLogs", true);

    // Save to preferences
    preferences.begin("settings", false);
    preferences.putInt("deviceNumber", deviceNumber);
    preferences.putString("siteID", siteID);
    preferences.putBool("enableSystemLogs", enableSystemLogs);
    preferences.putBool("enableHttpLogs", enableHttpLogs);
    preferences.putBool("enableLoRaLogs", enableLoRaLogs);
    preferences.end();

    addSystemLog("Settings updated: Device Number=" + String(deviceNumber) + ", Site ID=" + siteID);

    request->send(200, "text/plain", "Settings Updated");
  } else {
    request->send(400, "text/plain", "Missing Parameters");
  }
}

// Handle sending LoRa messages
void handleSendLoRa(AsyncWebServerRequest *request) {
  if (request->method() != HTTP_POST) {
    request->send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);

  String loraMessage;

  if (request->hasParam("message", true) && request->getParam("message", true)->value().length() > 0) {
    loraMessage = request->getParam("message", true)->value();
  } else {
    loraMessage = "Hello from Device " + String(deviceNumber) + "!";
  }

  // Include siteID in the message
  loraMessage = siteID + ":" + loraMessage;

  // Send the LoRa message
  LoRa.beginPacket();
  LoRa.print(loraMessage);
  LoRa.endPacket();

  // Log the LoRa message sending
  Serial.println("LoRa message sent: " + loraMessage);
  addLoRaLog("LoRa message sent: " + loraMessage);

  // Respond to the user
  request->send(200, "text/plain", "LoRa message sent: " + loraMessage);
}

// Handle reboot
void handleReboot(AsyncWebServerRequest *request) {
  if (request->method() != HTTP_POST) {
    request->send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "200 OK - " + fullUri);

  addSystemLog("Reboot initiated by user.");

  request->send(200, "text/plain", "Device is rebooting...");

  delay(1000);

  ESP.restart();
}

// Handle 404 Not Found
void handleNotFound(AsyncWebServerRequest *request) {
  // Log the request
  String fullUri = buildFullUri(request);
  String destIp = getDestinationIp(request);
  addHttpLog(request->client()->remoteIP().toString(), destIp, "404 Not Found - " + fullUri);

  request->send(404, "text/plain", "404 Not Found");
}

#define LORA_SS 5     // SS (Slave Select) pin on UEXT
#define LORA_RST 14   // Reset pin on UEXT
#define LORA_DIO0 26  // DIO0 pin on UEXT

// Initialize LoRa Module
void initLoRa() {
  Serial.println("Initializing LoRa...");
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);  // Setup LoRa module with UEXT pins

  if (!LoRa.begin(915E6)) {  // Initialize LoRa with 915 MHz frequency
    Serial.println("LoRa initialization failed or module not present.");
    addSystemLog("LoRa initialization failed or module not present.");
    // Continue without halting the system if LoRa is not present
    return;
  }
  
  LoRa.setTxPower(20);
  // Set LoRa parameters for better RX sensitivity
  LoRa.setSpreadingFactor(12);  // Maximum spreading factor for best sensitivity
  LoRa.setSignalBandwidth(125E3);  // 125 kHz bandwidth, lower bandwidth improves sensitivity
  LoRa.setCodingRate4(5);  // Set coding rate to 4/5 for more robust communication

  addSystemLog("LoRa initialized successfully.");
  Serial.println("LoRa initialized successfully.");
}

// Initialize OTA functionality
void setupOTA() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    addSystemLog("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    addSystemLog("OTA Update Finished");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    String progressMsg = "OTA Progress: " + String(progress / (total / 100)) + "%";
    addSystemLog(progressMsg);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String errorMsg = "OTA Error: ";
    if (error == OTA_AUTH_ERROR) {
      errorMsg += "Auth Failed";
    } else if (error == OTA_BEGIN_ERROR) {
      errorMsg += "Begin Failed";
    } else if (error == OTA_CONNECT_ERROR) {
      errorMsg += "Connect Failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      errorMsg += "Receive Failed";
    } else if (error == OTA_END_ERROR) {
      errorMsg += "End Failed";
    }
    addSystemLog(errorMsg);
  });

  ArduinoOTA.begin();
}

// Function to delay logging until time is synchronized
void syncTimeAndLog() {
  // Synchronize time with a timeout
  int ntp_attempts = 0;
  const int max_ntp_attempts = 10;  // Number of attempts before timing out
  while (!timeClient.update() && ntp_attempts < max_ntp_attempts) {
    timeClient.forceUpdate();
    delay(1000);  // Wait 1 second between attempts
    ntp_attempts++;
  }

  if (ntp_attempts >= max_ntp_attempts) {
    addSystemLog("NTP synchronization failed or took too long.");
  } else {
    addSystemLog("NTP synchronization successful.");
  }
}

// Global variables for WebSocket client cleanup
unsigned long lastCleanupTime = 0;
const unsigned long cleanupInterval = 5000;  // Clean up every 5 seconds

void setup() {
  // Start the serial communication
  Serial.begin(115200);
  Serial.println("Serial communication started");

  // Set up Ethernet event handler
  WiFi.onEvent(WiFiEvent);

  // Initialize Ethernet (PoE)
  ETH.begin();

  // Initialize Wi-Fi
  Serial.println("Initializing Wi-Fi...");
  WiFi.begin(ssid, password);

  // Initialize NTP and time zone
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  timeClient.begin();

  // Wait for Wi-Fi to connect
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 5) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    wifi_attempts++;
  }

  // Wait for Ethernet to get an IP address
  int eth_attempts = 0;
  while (ETH.localIP().toString() == "0.0.0.0" && eth_attempts < 10) {
    delay(500);
    eth_attempts++;
  }

  // Initialize LoRa module (if present)
  initLoRa();

  // Load settings from preferences
  preferences.begin("settings", false);
  deviceNumber = preferences.getInt("deviceNumber", deviceNumber);
  siteID = preferences.getString("siteID", siteID);
  enableSystemLogs = preferences.getBool("enableSystemLogs", true);
  enableHttpLogs = preferences.getBool("enableHttpLogs", true);
  enableLoRaLogs = preferences.getBool("enableLoRaLogs", true);
  preferences.end();

  // Synchronize time and start logging
  syncTimeAndLog();

  // Log the IP addresses where the web server can be accessed
  if (WiFi.status() == WL_CONNECTED) {
    addSystemLog("Connected to WiFi - IP Address: " + WiFi.localIP().toString());
    addSystemLog("Web server started. Access via WiFi IP address: " + WiFi.localIP().toString());
  }
  if (ETH.localIP().toString() != "0.0.0.0") {
    addSystemLog("Connected to Ethernet - IP Address: " + ETH.localIP().toString());
    addSystemLog("Web server started. Access via Ethernet IP address: " + ETH.localIP().toString());
  }

  // Initialize OTA
  setupOTA();

  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  // Set up the web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/main.js", HTTP_GET, handleMainJS);
  server.on("/style.css", HTTP_GET, handleStyleCSS);
  server.on("/ajax", HTTP_GET, handleAjaxRequest);
  server.on("/update_settings", HTTP_POST, handleUpdateSettings);
  server.on("/sendlora", HTTP_POST, handleSendLoRa);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.onNotFound(handleNotFound);

  // Add WebSocket handler
  server.addHandler(&ws);

  // Attach WebSocket event handler
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.println("WebSocket client connected");
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.println("WebSocket client disconnected");
    } else if (type == WS_EVT_ERROR) {
      Serial.printf("WebSocket Error: %s\n", (char *)arg);
    }
  });

  server.begin();
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Update NTP client
  timeClient.update();

  // Check for LoRa messages
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // Received a packet
    String receivedMessage = "";
    while (LoRa.available()) {
      receivedMessage += (char)LoRa.read();
    }

    // Check if the message starts with the siteID
    if (receivedMessage.startsWith(siteID + ":")) {
      // Remove the siteID prefix
      String actualMessage = receivedMessage.substring(siteID.length() + 1);

      // Print the message and log it
      Serial.println("Received LoRa message: " + actualMessage);
      addLoRaLog("Received LoRa message: " + actualMessage);

      // Notify connected WebSocket clients
      StaticJsonDocument<256> doc;
      doc["type"] = "loraMessage";
      doc["message"] = actualMessage;
      String jsonString;
      serializeJson(doc, jsonString);

      // Log the message being sent over WebSocket
      Serial.println("Sending WebSocket message: " + jsonString);

      ws.textAll(jsonString);  // Send the message to all connected clients
    } else {
      Serial.println("Received LoRa message with mismatched site ID.");
    }
  }

  // Clean up WebSocket clients periodically
  if (millis() - lastCleanupTime > cleanupInterval) {
    ws.cleanupClients();
    lastCleanupTime = millis();
  }
}
