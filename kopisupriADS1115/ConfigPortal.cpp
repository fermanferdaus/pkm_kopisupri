#include "ConfigPortal.h"

ConfigPortal::ConfigPortal() : server(80) {}

void ConfigPortal::begin() {
  prefs.begin("config", false);
  strlcpy(config.ssid, prefs.getString("ssid", "WiFi").c_str(), sizeof(config.ssid));
  strlcpy(config.password, prefs.getString("password", "12345678").c_str(), sizeof(config.password));
  config.suhuThreshold = prefs.getFloat("suhu", 127.0);
  config.asapThreshold = prefs.getFloat("asap", 80.0);
  prefs.end();
}

bool ConfigPortal::shouldStartPortal() {
  String ssidStr = String(config.ssid);
  ssidStr.trim();
  return ssidStr.length() == 0;
}

void ConfigPortal::startAccessPoint() {
  WiFi.softAP("SmartRoaster Config", "12345678");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);

  setupRoutes();
  server.begin();
}

void ConfigPortal::setupRoutes() {
  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/save", HTTP_POST, [this]() { handleSave(); });
}

void ConfigPortal::handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WiFi Setup</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f5f5f5;
      padding: 20px;
      margin: 0;
    }
    .container {
      background: white;
      padding: 20px;
      max-width: 400px;
      margin: auto;
      border-radius: 10px;
      box-shadow: 0 0 10px rgba(0,0,0,0.1);
    }
    h2 {
      text-align: center;
      color: #333;
    }
    input[type="text"],
    input[type="password"],
    input[type="number"] {
      width: 100%;
      padding: 10px;
      margin: 8px 0;
      border: 1px solid #ccc;
      border-radius: 5px;
      box-sizing: border-box;
    }
    .password-wrapper {
      position: relative;
    }
    .toggle-password {
      position: absolute;
      right: 10px;
      top: 50%;
      transform: translateY(-50%);
      cursor: pointer;
      width: 24px;
      height: 24px;
    }
    input[type="submit"] {
      width: 100%;
      padding: 10px;
      margin-top: 12px;
      background-color: #007bff;
      color: white;
      border: none;
      border-radius: 5px;
      font-weight: bold;
      cursor: pointer;
    }
    input[type="submit"]:hover {
      background-color: #0056b3;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Konfigurasi</h2>
    <form action="/save" method="POST">
      SSID:<br>
      <input type="text" name="ssid" value=")rawliteral" + String(config.ssid) + R"rawliteral(" required><br>

      Password:<br>
      <div class="password-wrapper">
        <input type="password" name="password" id="password" value=")rawliteral" + String(config.password) + R"rawliteral(" required>
        <svg id="eyeIcon" class="toggle-password" xmlns="http://www.w3.org/2000/svg" fill="#007bff" viewBox="0 0 24 24" onclick="togglePassword()">
          <path d="M12 4.5C7 4.5 2.73 8.11 1 12c1.73 3.89 6 7.5 11 7.5s9.27-3.61 11-7.5C21.27 8.11 17 4.5 12 4.5zm0 12a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9z"/>
          <circle cx="12" cy="12" r="2.5"/>
        </svg>
      </div>

      Ambang Suhu:<br>
      <input type="number" name="suhu" step="0.1" value=")rawliteral" + String((int)config.suhuThreshold) + R"rawliteral(" required><br>

      Ambang Asap:<br>
      <input type="number" name="asap" step="0.1" value=")rawliteral" + String((int)config.asapThreshold) + R"rawliteral(" required><br>

      <input type="submit" value="Simpan">
    </form>
  </div>

  <script>
    var showIcon = '<path d="M12 4.5C7 4.5 2.73 8.11 1 12c1.73 3.89 6 7.5 11 7.5s9.27-3.61 11-7.5C21.27 8.11 17 4.5 12 4.5zm0 12a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9z"/><circle cx="12" cy="12" r="2.5"/>';
    var hideIcon = '<path d="M12 4.5C7 4.5 2.73 8.11 1 12c1.73 3.89 6 7.5 11 7.5 1.43 0 2.8-.31 4.04-.87l2.61 2.61 1.41-1.41L3.1 2.1 1.69 3.51 4.3 6.13A11.04 11.04 0 0 0 1 12c1.73 3.89 6 7.5 11 7.5 2.03 0 3.96-.56 5.64-1.54l2.85 2.85 1.41-1.41L3.1 2.1z"/>';
    
    function togglePassword() {
      var pw = document.getElementById("password");
      var icon = document.getElementById("eyeIcon");
      if (pw.type === "password") {
        pw.type = "text";
        icon.innerHTML = hideIcon;
      } else {
        pw.type = "password";
        icon.innerHTML = showIcon;
      }
    }
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void ConfigPortal::handleSave() {
  strlcpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
  strlcpy(config.password, server.arg("password").c_str(), sizeof(config.password));
  config.suhuThreshold = server.arg("suhu").toFloat();
  config.asapThreshold = server.arg("asap").toFloat();

  saveConfig(config);

  server.send(200, "text/html", "<html><body><h3>Tersimpan. Restarting...</h3></body></html>");
  delay(2000);
  ESP.restart();
}

void ConfigPortal::saveConfig(const Config &cfg) {
  prefs.begin("config", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("password", cfg.password);
  prefs.putFloat("suhu", cfg.suhuThreshold);
  prefs.putFloat("asap", cfg.asapThreshold);
  prefs.end();
}

void ConfigPortal::handleClient() {
  server.handleClient();
}

void ConfigPortal::getConfig(Config &outConfig) {
  outConfig = config;
}
