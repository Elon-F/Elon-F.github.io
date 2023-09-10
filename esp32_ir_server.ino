#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRac.h>
#include <WiFiClient.h>
#include <Preferences.h>

String wifiSSID;
String wifiPassword;
String UUID;
MDNSResponder mdns;

WebServer server(80);
#define HOSTNAME "esp32"
#define PREFIX "esp-irserver-"
#define AP_PASSWORD "DefaultPassword"

Preferences preferences;

const uint16_t kRecvPin = 32; // ESP GPIO pin to use for reading signals. should support hardware pwm.
const uint16_t kIrLed = 33; // ESP GPIO pin to use for emitting signals. should support hardware pwm.
const uint16_t kCaptureBufferSize = 128;
const uint8_t kTimeout = 50;
const long readTimeoutMS = 5000;

IRsend irsend(kIrLed); // Set the GPIO to be used to sending the message.
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results; // Somewhere to store the results

void handleRoot()
{
  Serial.println("GET request at: /");
  server.send(200, "application/json", "\"IR Receiver Device\"");
}

void handleSend()
{
  Serial.println("GET request at: /send");

  int protocol = 0;
  uint32_t code = 0;
  for (uint8_t i = 0; i < server.args(); i++)
  {
    if (server.argName(i) == "signal")
    {
      code = strtoul(server.arg(i).c_str(), NULL, 0);
      Serial.println(code);
    }
    else if (server.argName(i) == "protocol")
    {
      if (server.arg(i) == "COOLIX")
        protocol = 1;
      if (server.arg(i) == "NEC")
        protocol = 2;
    }
  }

  switch (protocol)
  {
  case 1:
    irsend.sendCOOLIX(code);
    break;
  case 2:
    irsend.sendNEC(code, 32);
    break;
  default:
    server.send(404, "application/json", "\"Invalid protocol\"");
  }

  server.send(200, "application/json", "\"Successfully sent a thing.\"");
}

void handleRead()
{
  Serial.println("GET request at: /read");
  for (uint8_t i = 0; i < server.headers(); i++)
  {
    Serial.println("Header " + server.headerName(i) + ": " + server.header(i));
  }
  int ms = millis();
  irrecv.resume(); // resets the internal state to clear previously received bits.
  while (!irrecv.decode(&results))
  {
    if (millis() - ms > readTimeoutMS)
    {
      Serial.println("Read timeout.");
      server.send(200, "application/json", R"({ "success": false, "error": "Read timed out / no signal found."})");
      return;
    }
    yield();
  }

  // Display the basic output of what we found.
  Serial.print(resultToHumanReadableBasic(&results));
  // Display any extra A/C info if we have it.
  String description = IRAcUtils::resultAcToString(&results);
  server.send(200, "application/json", "{ \"protocol\": \"" + typeToString(results.decode_type, results.repeat) + "\", \"code\":\"" + resultToHexidecimal(&results) + "\", \"description\":\"" + description + "\", \"success\": true}");
}

void handleStatus()
{
  Serial.println("GET request at: /status");
  server.send(200, "application/json", "{\"battery\": \"100.00\",\"capabilities\": [\"SEND_SIGNAL\",\"READ_SIGNAL\"]}");
}

void handleConfig()
// Send the preference page.
{
  Serial.println("Config request");
  for (uint8_t i = 0; i < server.headers(); i++)
  {
    if (server.headerName(i) == "Host" && server.header(i) != PREFIX + UUID + ".local")
    {
      Serial.println("Blocked attempt to edit config from non mdns address source.");
      return server.send(403, "text/plain");
    }
  }

  String prefPage = R"(<html><head><title>IR ESP Configuration</title><link rel="icon" href="data:,"><style>body {display: flex;justify-content: center;align-items: center;height: 100%;}label {padding-bottom: 1rem;}input {margin-bottom: 1rem;}input {padding: 0.25rem;}button {padding: 0.25reml}</style></head><body>
    <form method="POST">
        <label for="uuid">UUID:</label><br>
        <input type="text" id="uuid" name="uuid" value=")" +
                    UUID + R"("><br>
        <label for="ssid">SSID:</label><br>
        <input type="text" id="ssid" name="ssid" value=")" +
                    wifiSSID + R"("><br>
        <label for="pass">Password:</label><br>
        <input type="password" id="pass" name="pass"><br>
        <label for="ap_mode">Force AP mode:</label> <input type="checkbox" id="ap_mode" name="ap_mode"><br>
        <label for="reset">Reset all settings:</label> <input type="checkbox" id="reset" name="reset"><br>
        <button>Submit changes & restart device.</button>
        <p> Wait 1-2 minutes before reloading the page.</p>
    </form></body><html>)";

  server.send(200, "text/html", prefPage);
}

void handleConfigSet()
{
  Serial.println("Set config:");

  String ssid = server.arg("ssid");
  String password = server.arg("pass");
  String uuid = server.arg("uuid");
  bool ap_mode = false;
  bool reset = false;
  if (server.hasArg("ap_mode"))
    ap_mode = true;
  if (server.hasArg("reset"))
    reset = true;

  if (uuid != "")
  {
    Serial.print("UUID: ");
    Serial.println(server.arg("uuid"));
    preferences.putString("uuid", uuid);
  }

  if (ssid != "")
  {
    Serial.print("Wifi SSID: ");
    Serial.println(server.arg("ssid"));
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
  }

  server.sendHeader("Location", "/config"); // Add a header to respond with a new location for the browser to go to the config page again
  server.send(303);

  if (reset)
  {
    preferences.clear();
  }

  if (server.args() >= 1)
  {
    preferences.end();
    esp_restart();
  }
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  server.send(404, "text/plain", message);
}

void setup(void)
{
  preferences.begin("ir-server", false);

  String mac = WiFi.macAddress();
  String mac_concat = mac.substring(9, 11) + mac.charAt(12) + mac.charAt(13) + mac.charAt(15) + mac.charAt(16);

  // build uuid from last 6 chars of MAC address. keeps things consistent.
  Serial.begin(115200);

  UUID = preferences.getString("uuid", mac_concat);
  if (UUID == "")
    UUID = mac_concat;
  UUID.toLowerCase();
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("pass", "");
  Serial.print("wifiSSID: ");
  Serial.println(wifiSSID);

  if (wifiPassword != "")
  {
    Serial.print("Password len=");
    Serial.print(wifiPassword.length());
  }

  // if no SSID was defined, we create the AP
  if (wifiSSID != "")
  {
    WiFi.begin(wifiSSID, wifiPassword);

    Serial.println("");
    int c = 0;
    while ((WiFi.status() != WL_CONNECTED || WiFi.status() != WL_CONNECT_FAILED) && c < 60) // 30s timeout
    {
      c++;
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(wifiSSID);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP().toString());
    }
    else
    {
      WiFi.disconnect();
      Serial.println("Failed to connected to wifi");
    }
  }
  if (wifiSSID == "" || WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Starting AP");
    WiFi.softAP(PREFIX + UUID, AP_PASSWORD);
  }

  irsend.begin();
  irrecv.enableIRIn();
  assert(irutils::lowLevelSanityCheck() == 0);

  if (mdns.begin(PREFIX + UUID))
  {
    Serial.print("MDNS responder started at: ");
    Serial.println(PREFIX + UUID + ".local");
    // Announce http tcp service on port 80
    mdns.addService("http", "tcp", 80);
  }

  server.on("/", handleRoot);
  server.on("/read", handleRead);
  server.on("/send", handleSend);
  server.on("/status", handleStatus);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfigSet);
  server.on("/json", HTTP_POST, handleJson);

  server.onNotFound(handleNotFound);

  const char *headerkeys[] = {"User-Agent", "Host"};
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);

  server.collectHeaders(headerkeys, headerkeyssize);

  server.enableCORS(true);
  server.begin();
  Serial.println("HTTP server started");
}

void loop(void)
{
  server.handleClient();
}