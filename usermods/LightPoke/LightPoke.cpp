#include <ESP8266WiFi.h>

#include "wled.h"

// How many failed connection attempts to make before resetting the connection
#define LIGHT_POKE_RESET_CONNECTION_TIMEOUT 10
// How long to wait between incrementing failed connection attempt counter
#define LIGHT_POKE_RESET_CONNECTION_MS 1000

bool serverIsHTTPS(String server)
{
  // All servers will be https:// or http:// because of validation rules
  return server.startsWith("https://");
}

bool isValidServerURL(String server)
{
  // Only allow server if on preapproved list otherwise set default
  return server.equals("https://lightpoke.bumblebea.me") || server.equals("http://lightpoke.bumblebea.me") || server.startsWith("http://192.168.") || server.startsWith("http://10.");
}

struct ServerURLParts
{
  bool isHTTPS;
  String origin;
  uint16_t port;
};

ServerURLParts serverURLParts(String server)
{
  bool isHTTPS = serverIsHTTPS(server);
  String newS;
  if (isHTTPS)
  {
    newS = server.substring(8); // remove https://
  }
  else
  {
    newS = server.substring(7); // remove http://
  }

  int trailingSlash = newS.indexOf('/');
  // Remove everything after the first slash
  if (trailingSlash != -1)
  {
    newS = newS.substring(0, trailingSlash);
  }

  int colon = newS.indexOf(':');
  String origin;
  String port;

  if (colon != -1)
  {
    origin = newS.substring(0, colon);
    port = newS.substring(colon + 1);
  }
  else
  {
    origin = newS;
    port = isHTTPS ? "443" : "80";
  }
  ServerURLParts parts;
  parts.isHTTPS = isHTTPS;
  parts.origin = origin;
  parts.port = port.toInt();
  return parts;
}

class LightPokeAdditions : public Usermod
{
private:
  bool enabled = true;
  bool initDone = false;

  // string that are used multiple time (this will save some flash memory)
  static const char _name[];
  static const char _enabled[];
  static const char _apiKey[];
  static const char _server[];
  static const char _defaultServerUrl[];
  static const char _presetName[];

  String server = _defaultServerUrl;
  String apiKey = "";

  WiFiClient client;
  void setupSSE();
  void sendPoke();
  void handlePoke();

  unsigned long pokeSustainMs;
  unsigned long lastPokeTime = 0;
  uint8_t lightPokePresetId = 249;
  bool applyPokeFlag = false;
  StaticJsonDocument<1024> pokeDataDoc;

  bool sendPokeFlag = false;
  int notConnectedCounter = 0;
  unsigned long lastNotConnectedTime = 0;

public:
  /**
   * Enable/Disable the usermod
   */
  inline void enable(bool enable) { enabled = enable; }

  /**
   * Get usermod enabled/disabled state
   */
  inline bool isEnabled() { return enabled; }

  void setup() override
  {
    // do your set-up here
    // Serial.println("Hello from my usermod!");
    initDone = true;
  }

  /*
   * connected() is called every time the WiFi is (re)connected
   * Use it to initialize network interfaces
   */
  void connected() override
  {
    // Serial.println("Connected to WiFi!");
    setupSSE();
    return;
  }

  void loop() override
  {
    // if usermod is disabled or called during strip updating just exit
    // NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
    if (!enabled || strip.isUpdating() || apiKey.length() == 0 || server.length() == 0)
      return;

    if (sendPokeFlag)
    {
      sendPokeFlag = false;
      sendPoke();
    }

    // Handle applying and restoring from poke
    if (applyPokeFlag)
    {
      applyPokeFlag = false;
      pokeSustainMs = pokeDataDoc["sustainS"].as<uint16>() * 1000;
      deserializeState(pokeDataDoc["state"], CALL_MODE_NOTIFICATION, 0);

      lastPokeTime = millis();
    }

    if (lastPokeTime > 0 && millis() - lastPokeTime > pokeSustainMs)
    {
      applyPreset(lightPokePresetId, CALL_MODE_NOTIFICATION);
      lastPokeTime = 0;
      pokeSustainMs = 0;
    }

    if (client.connected() && client.available())
    {
      notConnectedCounter = 0;
      String line = client.readStringUntil('\n');
      if (line.startsWith("data:"))
      {
        String data = line.substring(5);
        // Process the data received from the server
        // For example, you can print it to the Serial Monitor
        DEBUG_PRINTF("POKE %s\n", data.c_str());

        deserializeJson(pokeDataDoc, data);
        handlePoke();
      }
    }
    else
    {
      // Reset the connection if not connected
      if (millis() - lastNotConnectedTime > LIGHT_POKE_RESET_CONNECTION_MS)
      {
        notConnectedCounter++;
        lastNotConnectedTime = millis();
      }

      if (notConnectedCounter > LIGHT_POKE_RESET_CONNECTION_TIMEOUT)
      {
        notConnectedCounter = 0;
        lastNotConnectedTime = 0;
        setupSSE();
      }
    }
  }

  void addToJsonInfo(JsonObject &root) override
  {
    if (!enabled)
      return;

    JsonObject user = root["u"];
    if (user.isNull())
      user = root.createNestedObject("u");

    user[FPSTR(_name)] = client.connected() ? "Connected" : "Disconnected";
  }

  /*
   * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
   * Values in the state object may be modified by connected clients
   */
  void addToJsonState(JsonObject &root) override
  {
    if (!initDone || !enabled)
      return; // prevent crash on boot applyPreset()

    JsonObject usermod = root[FPSTR(_name)];
    if (usermod.isNull())
      usermod = root.createNestedObject(FPSTR(_name));

    usermod[FPSTR(_server)] = server;
    usermod[FPSTR(_apiKey)] = apiKey;
  }

  void readFromJsonState(JsonObject &root) override
  {
    if (!initDone)
      return; // prevent crash on boot applyPreset()

    JsonObject usermod = root[FPSTR(_name)];
    if (!usermod.isNull())
    {
      String checkServer;
      getJsonValue(usermod[FPSTR(_server)], checkServer, server);

      if (isValidServerURL(checkServer))
      {
        server = checkServer;
      }
      else
      {
        server = _defaultServerUrl;
      }

      getJsonValue(usermod[FPSTR(_apiKey)], apiKey, apiKey);

      // Trigger poke action
      sendPokeFlag = usermod["poke"].as<bool>();
    }
  }

  bool readFromConfig(JsonObject &root) override
  {
    JsonObject top = root[FPSTR(_name)];

    bool configComplete = !top.isNull();

    String checkServer;
    configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, enabled);
    configComplete &= getJsonValue(top[FPSTR(_apiKey)], apiKey, apiKey);
    configComplete &= getJsonValue(top[FPSTR(_server)], checkServer, server);

    // Only allow server if on preapproved list otherwise set default
    if (isValidServerURL(checkServer))
    {
      server = checkServer;
    }
    else
    {
      server = _defaultServerUrl;
    }

    return configComplete;
  }

  void addToConfig(JsonObject &root) override
  {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;
    top[FPSTR(_apiKey)] = apiKey;
    top[FPSTR(_server)] = server;
  }
};

void LightPokeAdditions::setupSSE()
{
  if (!enabled)
    return;

  if (client.connected())
  {
    client.stop();
  }

  DEBUG_PRINTF("Connecting to %s with API Key %s\n", server.c_str(), apiKey.c_str());

  ServerURLParts parts = serverURLParts(server);
  if (client.connect(parts.origin.c_str(), parts.port))
  {
    DEBUG_PRINTLN("Connected to server");

    // Construct the HTTP GET request with the correct path
    client.print(F("GET /api/device/"));
    client.print(apiKey); // Insert the API key dynamically
    client.println(F("/poke/stream HTTP/1.1"));
    client.print(F("Host: "));
    client.println(parts.origin);                   // Add the Host header
    client.println(F("Accept: text/event-stream")); // Specify SSE content type
    client.println(F("Connection: keep-alive"));    // Keep the connection open
    client.println();                               // End the HTTP headers
  }
  else
  {
    DEBUG_PRINTLN("Failed to connect to server");
  }
}

void LightPokeAdditions::sendPoke()
{
  if (!enabled)
    return;

  client.stop();

  ServerURLParts parts = serverURLParts(server);
  if (client.connect(parts.origin.c_str(), parts.port))
  {
    client.print(F("POST /api/device/"));
    client.print(apiKey); // Insert the API key dynamically
    client.println(F("/poke HTTP/1.1"));
    client.print(F("Host: "));
    client.println(parts.origin);           // Add the Host header
    client.println(F("Connection: close")); // Close the connection after the request
    client.println(F("Content-Length: 0"));
    client.println();

    client.stop();
  }
  else
  {
    DEBUG_PRINTLN("Failed to connect to server for poke");
  }

  setupSSE();
}

void LightPokeAdditions::handlePoke()
{
  if (!enabled)
    return;

  if (pokeDataDoc.isNull())
    return;
  if (pokeDataDoc["type"] != "poke")
    return;

  // Save the current lantern state to a preset and restore after poke effect
  // Only save current state if not already in a temporary state
  if (lastPokeTime == 0)
  {
    cacheInvalidate++;
    savePreset(lightPokePresetId, _presetName);
  }

  // Can't apply the poke effect yet because presets will only be saved next tick

  applyPokeFlag = true;
}

// add more strings here to reduce flash memory usage
const char LightPokeAdditions::_name[] PROGMEM = "LightPoke";
const char LightPokeAdditions::_enabled[] PROGMEM = "enabled";
const char LightPokeAdditions::_apiKey[] PROGMEM = "apiKey";
const char LightPokeAdditions::_server[] PROGMEM = "server";
const char LightPokeAdditions::_defaultServerUrl[] PROGMEM = "https://lightpoke.bumblebea.me";
const char LightPokeAdditions::_presetName[] PROGMEM = "LightPoke Restore";

static LightPokeAdditions lightPoke_usermod;
REGISTER_USERMOD(lightPoke_usermod);