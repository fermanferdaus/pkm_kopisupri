#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

struct Config {
  char ssid[32];
  char password[64];
  float suhuThreshold;
  float asapThreshold;
};

class ConfigPortal {
public:
  ConfigPortal();
  void begin();
  bool shouldStartPortal();
  void handleClient();
  void getConfig(Config &outConfig);
  void saveConfig(const Config &cfg);

public:
  WebServer server;
  Preferences prefs;
  Config config;
  void startAccessPoint();
  void setupRoutes();
  void handleRoot();
  void handleSave();
  void handleReset();
};

#endif
