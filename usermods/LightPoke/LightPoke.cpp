#include "wled.h"

#ifndef USERMOD_LIGHTPOKE_HEARTBEAT_INTERVAL
#define USERMOD_LIGHTPOKE_HEARTBEAT_INTERVAL 1000
#endif

#ifndef USERMOD_LIGHTPOKE_SERVER
#define USERMOD_LIGHTPOKE_SERVER "http://192.168.0.150:3000"
#endif



class LightPokeAdditions : public Usermod {
  private:
    bool enabled = true;
    bool initDone = false;

    // string that are used multiple time (this will save some flash memory)
    static const char _name[];
    static const char _enabled[];
    static const char _apiKey[];

    String apiKey = "";

  public:
    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool enable) { enabled = enable; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() { return enabled; }

    void setup() override {
      // do your set-up here
      //Serial.println("Hello from my usermod!");
      initDone = true;
    }

    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() override {
      //Serial.println("Connected to WiFi!");
      return;
    }

    void loop() override {
      // if usermod is disabled or called during strip updating just exit
      // NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
      if (!enabled || strip.isUpdating()) return;
    }

    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void addToJsonState(JsonObject& root) override
    {
      if (!initDone || !enabled) return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));

      usermod["server"] = USERMOD_LIGHTPOKE_SERVER;
      usermod["interval"] = USERMOD_LIGHTPOKE_HEARTBEAT_INTERVAL;
      usermod["apiKey"] = apiKey;
    }

    bool readFromConfig(JsonObject& root) override
    {
      JsonObject top = root[FPSTR(_name)];

      bool configComplete = !top.isNull();

      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, enabled);
      configComplete &= getJsonValue(top[FPSTR(_apiKey)], apiKey, apiKey);

      return configComplete;
    }

    void addToConfig(JsonObject& root) override
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
      top[FPSTR(_apiKey)] = apiKey;
    }
};

// add more strings here to reduce flash memory usage
const char LightPokeAdditions::_name[]    PROGMEM = "LightPoke";
const char LightPokeAdditions::_enabled[] PROGMEM = "enabled";
const char LightPokeAdditions::_apiKey[] PROGMEM = "apiKey";