#include "WebManager.h"

WebManager webManager;

WebManager::WebManager() : server(80) {
}

void WebManager::begin() {
  // Load auth settings
  preferences.begin("bank_data", true);
  authUser = preferences.getString("a_user", "admin");
  authPass = preferences.getString("a_pass", "admin");
  preferences.end();

  // Load saved accounts
  preferences.begin("bank_data", true);
  for(int i=0; i<3; i++) {
      String s = String(i);
      String prefix = "m" + s;
      
      String bin = preferences.getString((prefix + "bin").c_str(), "");
      String acc = preferences.getString((prefix + "acc").c_str(), "");
      String bn = preferences.getString((prefix + "bn").c_str(), "");
      String on = preferences.getString((prefix + "on").c_str(), "");

      strncpy(accounts[i].bin, bin.c_str(), sizeof(accounts[i].bin));
      strncpy(accounts[i].accNum, acc.c_str(), sizeof(accounts[i].accNum));
      strncpy(accounts[i].bankName, bn.c_str(), sizeof(accounts[i].bankName));
      strncpy(accounts[i].ownerName, on.c_str(), sizeof(accounts[i].ownerName));
  }
  preferences.end();

  // Define routes
  server.on("/", HTTP_GET, std::bind(&WebManager::handleRoot, this));
  server.on("/scan", HTTP_GET, std::bind(&WebManager::handleScan, this));
  server.on("/connect_wifi", HTTP_GET, std::bind(&WebManager::handleConnectWifi, this));
  server.on("/list_wifi", HTTP_GET, std::bind(&WebManager::handleListWifi, this));
  server.on("/del_wifi", HTTP_GET, std::bind(&WebManager::handleDelWifi, this));
  server.on("/updatefw", HTTP_GET, std::bind(&WebManager::handleUpdateFW, this));
  server.on("/save", HTTP_POST, std::bind(&WebManager::handleSave, this));
  server.on("/reset", HTTP_GET, std::bind(&WebManager::handleReset, this));
  server.on("/reboot", HTTP_GET, std::bind(&WebManager::handleReboot, this));
  server.on("/export", HTTP_GET, std::bind(&WebManager::handleExport, this));
  
  // API Routes
  server.on("/api/qr", HTTP_GET, std::bind(&WebManager::handleApiQR, this));
  server.on("/api/info", HTTP_GET, std::bind(&WebManager::handleApiInfo, this));
  server.on("/api/accounts", HTTP_GET, std::bind(&WebManager::handleApiAccounts, this));
  server.on("/api/mqtt_status", HTTP_GET, std::bind(&WebManager::handleApiMqttStatus, this));

  // OTA Update
  server.on("/update", HTTP_POST, std::bind(&WebManager::handleUpdate, this), [&]() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      // TODO: Show updating status on screen via DisplayManager?
      // displayManager.showUpdating(); 
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.println("Update Success");
      } else {
        Update.printError(Serial);
      }
    }
  });
}

void WebManager::update() {
  if (!isServerStarted && networkManager.isWifiConnected()) {
    server.begin();
    isServerStarted = true;
    if (MDNS.begin("qrstation")) {
      Serial.println("MDNS responder started");
    }
    Serial.println("Web Server started");
  }
  
  if (isServerStarted) {
    server.handleClient();
  }
}

void WebManager::startAP() {
  WiFi.softAP("QR Station", "88888888");
  server.begin();
  isServerStarted = true;
  Serial.println("AP Mode Server started");
}

void WebManager::stopAP() {
  server.stop();
  isServerStarted = false;
  WiFi.softAPdisconnect(true);
}

bool WebManager::checkAuth() {
  if (authUser.length() > 0 && !server.authenticate(authUser.c_str(), authPass.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// Handlers implementation
void WebManager::handleRoot() {
  if (!checkAuth()) return;
  
  String wifiOptions = "";
  if (networkManager.isWifiConnected()) {
    wifiOptions = "<option value='" + WiFi.SSID() + "'>" + WiFi.SSID() + " (Connected)</option>";
  } else {
    wifiOptions = "<option value=''>Select WiFi...</option>";
  }

  String html = config_html;
  for(int i=0; i<3; i++) {
      String s = String(i);
      html.replace("%BIN" + s + "%", accounts[i].bin);
      html.replace("%ACC" + s + "%", accounts[i].accNum);
      html.replace("%BN" + s + "%", accounts[i].bankName);
      html.replace("%ON" + s + "%", accounts[i].ownerName);
  }
  
  // Need to get pass from prefs again or cache it? handled by loading.
  // Ideally we don't send back password.
  // For now, keep as per original logic, though original logic had `wifi_pass` global.
  // We can fetch from preferences if needed, but let's leave blank for security or put placeholder.
  // Original sent it back. I need to get it.
  preferences.begin("bank_data", true);
  String wp = preferences.getString("w_pass", "");
  preferences.end();

  html.replace("%WIFI_LIST%", wifiOptions);
  html.replace("%WP%", wp);
  html.replace("%MS%", networkManager.mqttServer);
  html.replace("%MU%", networkManager.mqttUser);
  html.replace("%MP%", networkManager.mqttPass);
  html.replace("%AU%", authUser);
  html.replace("%AP%", authPass);
  html.replace("%MQTT_ENABLED%", networkManager.mqttEnabled ? "checked" : "");
  html.replace("%MQTT_ENABLED_VAL%", networkManager.mqttEnabled ? "1" : "0");

  server.send(200, "text/html", html);
}

void WebManager::handleScan() {
  if (!checkAuth()) return;
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; ++i) {
    json += "{\"s\":\"" + WiFi.SSID(i) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
    if (i < n - 1) json += ",";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void WebManager::handleConnectWifi() {
  if (!checkAuth()) return;
  String s = server.arg("s");
  String p = server.arg("p");
  
  WiFi.begin(s.c_str(), p.c_str());
  unsigned long startMs = millis();
  bool connected = false;
  
  while(millis() - startMs < 10000) {
    if(WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    delay(100);
  }
  
  if(connected) {
    networkManager.addOrUpdateWifi(s, p);
    networkManager.addWifi(s.c_str(), p.c_str());
    server.send(200, "text/plain", "Connected to " + s);
  } else {
    server.send(200, "text/plain", "Connect failed");
  }
}

void WebManager::handleListWifi() {
  if (!checkAuth()) return;
  server.send(200, "application/json", networkManager.getSavedWifiList());
}

void WebManager::handleDelWifi() {
  if (!checkAuth()) return;
  // Implementation of deleting wifi from json list
  // Complex to replicate exactly without Json helper here or in NetworkManager.
  // Let's assume Network Manager handles this if we move logic there?
  // Or handle here with transient doc.
  // For simplicity, let's implement basic here.
  int idx = server.arg("i").toInt();
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, networkManager.getSavedWifiList());
  JsonArray arr = doc.as<JsonArray>();
  if (idx >= 0 && idx < arr.size()) {
    arr.remove(idx);
    String output;
    serializeJson(doc, output);
    networkManager.saveWifiList(output);
  }
  server.send(200, "text/plain", "OK");
}

void WebManager::handleUpdateFW() {
  if (!checkAuth()) return;
  server.send(200, "text/html", update_html);
}

void WebManager::handleSave() {
  if (!checkAuth()) return;
  
  preferences.begin("bank_data", false);
  for(int i=0; i<3; i++) {
      String s = String(i);
      String prefix = "m" + s;
      
      String bin = server.arg("bin"+s);
      String acc = server.arg("acc"+s);
      String bn = server.arg("bn"+s);
      String on = server.arg("on"+s);

      preferences.putString((prefix + "bin").c_str(), bin);
      preferences.putString((prefix + "acc").c_str(), acc);
      preferences.putString((prefix + "bn").c_str(), bn);
      preferences.putString((prefix + "on").c_str(), on);
      
      // Update runtime structs
      strncpy(accounts[i].bin, bin.c_str(), sizeof(accounts[i].bin));
      strncpy(accounts[i].accNum, acc.c_str(), sizeof(accounts[i].accNum));
      strncpy(accounts[i].bankName, bn.c_str(), sizeof(accounts[i].bankName));
      strncpy(accounts[i].ownerName, on.c_str(), sizeof(accounts[i].ownerName));
  }
  
  if (server.hasArg("ws") && server.arg("ws") != "") {
    networkManager.addOrUpdateWifi(server.arg("ws"), server.arg("wp"));
    networkManager.addWifi(server.arg("ws").c_str(), server.arg("wp").c_str());
  }

  preferences.putString("m_serv", server.arg("ms"));
  preferences.putString("m_user", server.arg("mu"));
  preferences.putString("m_pass", server.arg("mp"));
  preferences.putBool("m_en", server.arg("me") == "1");
  preferences.putString("a_user", server.arg("au"));
  preferences.putString("a_pass", server.arg("ap"));
  preferences.end();
  
  // Reload settings in network manager
  networkManager.loadSettings();
  authUser = server.arg("au");
  authPass = server.arg("ap");
  
  server.send(200, "text/html", "<html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><body style='background:#0f0c29;color:#fff;text-align:center;padding-top:100px;font-family:sans-serif;'><h1 style='color: green;'>Saved</h1><a href='/' style='color:#00d2ff;'>Back</a></body></html>");
}

void WebManager::handleReset() {
  if (!checkAuth()) return;
  preferences.begin("bank_data", false);
  preferences.clear();
  preferences.end();
  ESP.restart();
}

void WebManager::handleReboot() {
  server.send(200, "text/html", "Rebooting...");
  delay(100);
  ESP.restart();
}

void WebManager::handleUpdate() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", (Update.hasError()) ? "Failed" : "Success");
  delay(100);
  ESP.restart();
}

void WebManager::handleApiQR() {
   if (!checkAuth()) return;
   
  String bin = server.arg("bin");
  String acc = server.arg("acc");
  String amt = server.arg("amt");
  String on = server.arg("on");
  String desc = server.arg("desc");

  if (bin == "" || acc == "") {
    server.send(400, "text/plain", "Missing bin or acc");
    return;
  }

  dynamicQR.bin = bin;
  dynamicQR.acc = acc;
  dynamicQR.amount = amt;
  dynamicQR.name = on;
  dynamicQR.desc = desc;
  dynamicQR.active = true;
  dynamicQR.startTime = millis();

  // Turn on screen logic
  if (!displayManager.isContentVisible()) {
    displayManager.togglePowerOnOnly();
  }
  
  // Force update display to show dynamic QR
  displayManager.displayMode(-1);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "QR Updated");
}

void WebManager::handleApiInfo() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"name\":\"QR Station\",\"status\":\"online\"}");
}

void WebManager::handleApiAccounts() {
    if (!checkAuth()) return;
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "[";
    for (int i = 0; i < 3; i++) {
        if (strlen(accounts[i].accNum) > 0) {
        json += "{\"bin\":\"" + String(accounts[i].bin) + "\",";
        json += "\"acc\":\"" + String(accounts[i].accNum) + "\",";
        json += "\"bank\":\"" + String(accounts[i].bankName) + "\",";
        json += "\"name\":\"" + String(accounts[i].ownerName) + "\"}";
        if (i < 2 && strlen(accounts[i+1].accNum) > 0) json += ",";
        }
    }
    json += "]";
    server.send(200, "application/json", json);
}

void WebManager::handleApiMqttStatus() {
    if (!checkAuth()) return;
    String status = "disconnected";
    if (networkManager.isMqttConnected()) {
      status = "connected";
    } else if (networkManager.mqttEnabled && networkManager.mqttServer.length() > 0) {
      status = "connecting";
    } else if (!networkManager.mqttEnabled) {
      status = "disabled";
    }
    String json = "{\"status\":\"" + status + "\",\"enabled\":" + String(networkManager.mqttEnabled ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

void WebManager::handleExport() {
    if (!checkAuth()) return;
    String j = "[";
    for(int i=0; i<3; i++) {
        j += "{\"bin\":\"" + String(accounts[i].bin) + "\",";
        j += "\"acc\":\"" + String(accounts[i].accNum) + "\",";
        j += "\"on\":\"" + String(accounts[i].ownerName) + "\"}";
        if(i<2) j += ",";
    }
    j += "]";
    server.sendHeader("Content-Disposition", "attachment; filename=config.json");
    server.send(200, "application/json", j);
}
