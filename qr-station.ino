/*
 * Project: QR Station
 * Open Source: https://github.com/mhqb365/qr-station
 * Developer: mhqb365.com
 * Description: ESP32-C3 Based Bank QR Display
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <qrcode_st7735.h>
#include "driver/gpio.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// Thông tin ngân hàng
struct BankAccount {
  char bin[10];
  char accNum[20];
  char bankName[30];
  char ownerName[50];
};

BankAccount accounts[3];
Preferences preferences;
bool isPowerOn = true;
WebServer server(80);

// MQTT & WiFi
WiFiClient espClient;
WiFiMulti wifiMulti;
PubSubClient mqttClient(espClient);
char wifi_ssid[32] = "";
char wifi_pass[64] = "";
char mqtt_server[64] = "";
char mqtt_user[32] = "";
char mqtt_pass[32] = "";
char auth_user[32] = "admin";
char auth_pass[32] = "admin";
unsigned long lastMqttRetry = 0;

int wifiRetries = 0;
int mqttRetries = 0;
bool connectionDisabled = false;
const int MAX_RETRIES = 5;
unsigned long lastWifiRetry = 0;
String savedWifiList = "[]"; 

void saveWifiList(String json) {
  preferences.begin("bank_data", false);
  preferences.putString("w_list", json);
  preferences.end();
  savedWifiList = json;
}

void addOrUpdateWifi(String ssid, String pass) {
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, savedWifiList);
  JsonArray arr = doc.as<JsonArray>();
  
  bool found = false;
  for (JsonObject obj : arr) {
    if (obj["s"] == ssid) {
      obj["p"] = pass;
      found = true;
      break;
    }
  }
  
  if (!found) {
    JsonObject obj = arr.createNestedObject();
    obj["s"] = ssid;
    obj["p"] = pass;
  }
  
  String output;
  serializeJson(doc, output);
  saveWifiList(output);
}

void initApiRoutes(); // Forward declaration

bool isShowingNotification = false;
unsigned long notificationStart = 0;
const char* mqtt_topic = "transfers";
bool isServerStarted = false;

struct DynamicQR {
  String bin;
  String acc;
  String amount;
  String name;
  String desc;
  bool active = false;
  unsigned long startTime = 0;
};
DynamicQR dynamicQR;

// Forward Declarations
void displayMode(int mode);
void printCenteredWrapped(const String &text, int maxChars, int &currentY);
void handleApiQR();
void togglePowerOnOnly();
void drawStatusDots();
void recordActivity();
void setupWiFi();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void showNotification(long amount, const char* content, const char* gateway, const char* account);
void handleIPDisplay();


void loadSettings() {
  preferences.begin("bank_data", false);
  for(int i=0; i<3; i++) {
    String prefix = "m" + String(i);
    String b = preferences.getString((prefix + "bin").c_str(), "");
    String a = preferences.getString((prefix + "acc").c_str(), "");
    String bn = preferences.getString((prefix + "bn").c_str(), "");
    String on = preferences.getString((prefix + "on").c_str(), "");
    
    memset(accounts[i].bin, 0, sizeof(accounts[i].bin));
    memset(accounts[i].accNum, 0, sizeof(accounts[i].accNum));
    memset(accounts[i].bankName, 0, sizeof(accounts[i].bankName));
    memset(accounts[i].ownerName, 0, sizeof(accounts[i].ownerName));

    strncpy(accounts[i].bin, b.c_str(), sizeof(accounts[i].bin) - 1);
    strncpy(accounts[i].accNum, a.c_str(), sizeof(accounts[i].accNum) - 1);
    strncpy(accounts[i].bankName, bn.c_str(), sizeof(accounts[i].bankName) - 1);
    strncpy(accounts[i].ownerName, on.c_str(), sizeof(accounts[i].ownerName) - 1);
  }
  
  String s = preferences.getString("w_ssid", "");
  String p = preferences.getString("w_pass", "");
  String m = preferences.getString("m_serv", "");
  String mu = preferences.getString("m_user", "");
  String mp = preferences.getString("m_pass", "");
  String au = preferences.getString("a_user", "admin");
  String ap = preferences.getString("a_pass", "admin");
  savedWifiList = preferences.getString("w_list", "[]");

  // Migration for legacy single WiFi
  if (s != "" && savedWifiList == "[]") {
    StaticJsonDocument<256> doc;
    JsonArray arr = doc.to<JsonArray>();
    JsonObject obj = arr.createNestedObject();
    obj["s"] = s;
    obj["p"] = p;
    serializeJson(doc, savedWifiList);
    preferences.putString("w_list", savedWifiList);
  }
  
  memset(wifi_ssid, 0, sizeof(wifi_ssid));
  memset(wifi_pass, 0, sizeof(wifi_pass));
  memset(mqtt_server, 0, sizeof(mqtt_server));
  memset(mqtt_user, 0, sizeof(mqtt_user));
  memset(mqtt_pass, 0, sizeof(mqtt_pass));
  memset(auth_user, 0, sizeof(auth_user));
  memset(auth_pass, 0, sizeof(auth_pass));

  strncpy(wifi_ssid, s.c_str(), sizeof(wifi_ssid) - 1);
  strncpy(wifi_pass, p.c_str(), sizeof(wifi_pass) - 1);
  strncpy(mqtt_server, m.c_str(), sizeof(mqtt_server) - 1);
  strncpy(mqtt_user, mu.c_str(), sizeof(mqtt_user) - 1);
  strncpy(mqtt_pass, mp.c_str(), sizeof(mqtt_pass) - 1);
  strncpy(auth_user, au.c_str(), sizeof(auth_user) - 1);
  strncpy(auth_pass, ap.c_str(), sizeof(auth_pass) - 1);

  
  preferences.end();
}

void saveAccount(int i, String bin, String acc, String bName, String oName) {
  preferences.begin("bank_data", false);
  String prefix = "m" + String(i);
  preferences.putString((prefix + "bin").c_str(), bin);
  preferences.putString((prefix + "acc").c_str(), acc);
  preferences.putString((prefix + "bn").c_str(), bName);
  preferences.putString((prefix + "on").c_str(), oName);
  preferences.end();
  
  strncpy(accounts[i].bin, bin.c_str(), sizeof(accounts[i].bin));
  strncpy(accounts[i].accNum, acc.c_str(), sizeof(accounts[i].accNum));
  strncpy(accounts[i].bankName, bName.c_str(), sizeof(accounts[i].bankName));
  strncpy(accounts[i].ownerName, oName.c_str(), sizeof(accounts[i].ownerName));
}


// Cấu hình chân ESP32-C3 Super Mini
#define TFT_CS    10
#define TFT_RST   9
#define TFT_DC    8
#define TFT_SDA   5
#define TFT_SCL   4
#define TFT_BL    7

#define BUTTON1   1
#define BUTTON2   2
#define BUTTON3   3
#define BUTTON4   6

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
QRcode_ST7735 qrcode (&tft);

// Trạng thái nút nhấn
int currentMode = 1;
const int QR_TOP_MARGIN = 5;
const unsigned long INACTIVITY_TIMEOUT_MS = 120000UL;
unsigned long lastInputMs = 0;

void showSplashScreen() {
  tft.fillScreen(ST77XX_BLACK);
  digitalWrite(TFT_BL, HIGH);

  tft.drawCircle(64, 80, 40, 0x2104);
  tft.drawCircle(64, 80, 50, 0x1022);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);

  int16_t x, y;
  uint16_t w, h;
  String title = "QR Station";
  tft.getTextBounds(title, 0, 0, &x, &y, &w, &h);
  tft.setCursor((128 - w) / 2, 60);
  tft.print(title);

  for(int i=0; i<10; i++) {
    tft.fillRect(34, 100, 60, 4, ST77XX_BLACK);
    tft.drawRect(34, 100, 60, 4, 0x5AEB);       
    tft.fillRect(34, 100, (i+1)*6, 4, 0x00D2FF);  // Màu Cyan sáng cho loading bar
    delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 ST7735S Hardware SPI");
  setCpuFrequencyMhz(80);
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  pinMode(BUTTON3, INPUT_PULLUP);
  pinMode(BUTTON4, INPUT_PULLUP);
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW); 
 
  SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS); 
  
  tft.initR(INITR_BLACKTAB);
  
  qrcode.init();
  qrcode.setTopMargin(QR_TOP_MARGIN);

  loadSettings(); 
  showSplashScreen(); 
  displayMode(currentMode);
  
  digitalWrite(TFT_BL, HIGH); 
  Serial.println("Display ready");
  lastInputMs = millis();

  setupWiFi();
}

void initApiRoutes() {
  server.on("/api/qr", HTTP_GET, []() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    handleApiQR();
  });
  server.on("/api/info", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"name\":\"QR Station\",\"status\":\"online\"}");
  });
  
  server.on("/api/accounts", HTTP_GET, []() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) {
      return server.requestAuthentication();
    }
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
  });
}

// Giao diện trang cấu hình
const char* config_html = 
"<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>body{font-family:system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif;background:#0f0c29;background:linear-gradient(45deg,#24243e,#302b63,#0f0c29);color:#fff;margin:0;padding:20px;min-height:100vh;}"
".container{max-width:600px;margin:0 auto;}.card{background:rgba(255,255,255,0.05);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1);padding:25px;border-radius:20px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.5);}"
"h2{color:#00d2ff;margin-top:0;}h3{color:#3a7bd5;background:#fff;display:inline-block;padding:2px 10px;border-radius:5px;font-size:0.9em;margin-bottom:15px;}"
".form-group{margin-bottom:15px;text-align:left;}label{display:block;margin-bottom:5px;font-size:0.85em;color:#aaa;}input,select{width:100%;padding:12px;background:rgba(0,0,0,0.2);border:1px solid rgba(255,255,255,0.2);border-radius:10px;color:#fff;box-sizing:border-box;transition:0.3s;cursor:pointer;font-size:16px;-webkit-appearance:none;}input:focus,select:focus{outline:none;border-color:#00d2ff;background:rgba(0,0,0,0.4);}option{background:#302b63;color:#fff;}"
".btn{background:linear-gradient(to right,#00d2ff,#3a7bd5);border:none;color:white;padding:15px;font-size:16px;cursor:pointer;border-radius:10px;font-weight:bold;width:100%;margin-top:10px;box-shadow:0 5px 15px rgba(0,210,255,0.3);}"
".btn:active{transform:scale(0.98);}.nav{display:flex;gap:10px;margin-bottom:25px;}.nav a{flex:1;text-decoration:none;background:rgba(255,255,255,0.1);color:#fff;padding:10px;border-radius:10px;text-align:center;font-size:0.9em;border:1px solid transparent;}.nav a.active{background:rgba(0,210,255,0.2);border-color:#00d2ff;}"
".tabs-nav{display:flex;gap:5px;margin-bottom:15px;}.tab-btn{flex:1;padding:10px;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.1);color:#fff;border-radius:10px;cursor:pointer;font-size:0.9em;transition:0.3s;font-weight:bold;}.tab-btn.active{background:rgba(0,210,255,0.2);border-color:#00d2ff;color:#00d2ff;}.tab-pane{display:none;}.tab-pane.active{display:block;}</style></head><body>"
"<div class='container'><div class='nav'><a href='/' class='active'>Cài đặt</a><a href='/updatefw'>Nâng cấp Firmware</a></div>"
"<h2 style='text-align: center;'>QR Station</h2>"
"<form action='/save' method='POST'>"
"<div id='g1'>"
"<div class='tabs-nav'>"
"<button type='button' class='tab-btn active' onclick='openTab(\"g1\",0)'>QR 1</button>"
"<button type='button' class='tab-btn' onclick='openTab(\"g1\",1)'>QR 2</button>"
"<button type='button' class='tab-btn' onclick='openTab(\"g1\",2)'>QR 3</button>"
"</div>"
"<div class='tab-pane active'><div class='card'><h3>QR 1</h3>"
"<input type='hidden' name='bn0' id='bnV0' value='%BN0%'>"
"<div class='form-group'><label>Ngân hàng</label><select name='bin0' id='bin0' onchange='upd(0)'></select></div>"
"<div class='form-group'><label>Số tài khoản</label><input type='text' name='acc0' value='%ACC0%'></div>"
"<div class='form-group'><label>Chủ tài khoản</label><input type='text' name='on0' value='%ON0%'></div></div></div>"
"<div class='tab-pane'><div class='card'><h3>QR 2</h3>"
"<input type='hidden' name='bn1' id='bnV1' value='%BN1%'>"
"<div class='form-group'><label>Ngân hàng</label><select name='bin1' id='bin1' onchange='upd(1)'></select></div>"
"<div class='form-group'><label>Số tài khoản</label><input type='text' name='acc1' value='%ACC1%'></div>"
"<div class='form-group'><label>Chủ tài khoản</label><input type='text' name='on1' value='%ON1%'></div></div></div>"
"<div class='tab-pane'><div class='card'><h3>QR 3</h3>"
"<input type='hidden' name='bn2' id='bnV2' value='%BN2%'>"
"<div class='form-group'><label>Ngân hàng</label><select name='bin2' id='bin2' onchange='upd(2)'></select></div>"
"<div class='form-group'><label>Số tài khoản</label><input type='text' name='acc2' value='%ACC2%'></div>"
"<div class='form-group'><label>Chủ tài khoản</label><input type='text' name='on2' value='%ON2%'></div></div></div>"
"</div>"
"<h4 style='text-align: center;margin: 20px 0;'>OR</h4>"
"<div class='card'><h3>Cài đặt nhanh</h3>"
"<div class='form-group'><label>Cài đặt nhanh các thông tin ngân hàng bằng file JSON</label></div>"
"<div style='display:flex;gap:10px;'>"
"<button type='button' class='btn' style='flex:1;background:#ff416c;' onclick='document.getElementById(\"fi\").click()'>Nhập file</button>"
"<a href='/export' style='flex:1;text-decoration:none;'><button type='button' class='btn' style='background:#3a7bd5;'>Xuất file</button></a>"
"</div><input type='file' id='fi' style='display:none' accept='.json' onchange='imp(this)'></div>"
"<div id='g2'>"
"<div class='tabs-nav'>"
"<button type='button' class='tab-btn active' onclick='openTab(\"g2\",0)'>WiFi</button>"
"<button type='button' class='tab-btn' onclick='openTab(\"g2\",1)'>MQTT</button>"
"<button type='button' class='tab-btn' onclick='openTab(\"g2\",2)'>Admin</button>"
"</div>"
"<div class='tab-pane active'>"
"<div class='card'>"
"<div class='form-group'><label>Tên WiFi</label>"
"<div style='display:flex;gap:10px;'>"
"<select name='ws' id='ws' style='flex:1;'>%WIFI_LIST%</select>"
"<button type='button' id='scb' class='btn' style='width:auto;margin:0;padding:0 15px;background:#3a7bd5;' onclick='sc()'>TÌM KIẾM</button>"
"</div></div>"
"<div class='form-group'><label>Mật khẩu</label><input type='password' name='wp' id='wp' value='%WP%'></div>"
"<button type='button' id='cnb' class='btn' style='background:#f39c12;margin-top:10px;' onclick='cn()'>KẾT NỐI</button>"
"<div id='wfl' style='margin-top:20px;'></div>"
"</div></div>"
"<div class='tab-pane'>"
"<div class='card'>"
"<div class='form-group'><label>MQTT server (IP/Tên miền)</label><input type='text' name='ms' value='%MS%'></div>"
"<div class='form-group'><label>Tài khoản</label><input type='text' name='mu' value='%MU%'></div>"
"<div class='form-group'><label>Mật khẩu</label><input type='password' name='mp' value='%MP%'></div>"
"</div></div>"
"<div class='tab-pane'>"
"<div class='card'>"
"<div class='form-group'><label>Tài khoản</label><input type='text' name='au' value='%AU%'></div>"
"<div class='form-group'><label>Mật khẩu</label><input type='password' name='ap' value='%AP%'></div>"
"</div></div>"
"</div>"


"<button type='submit' class='btn' style='background:linear-gradient(to right,#00b894,#00d2ff);margin-bottom:20px;'>LƯU THAY ĐỔI</button>"

"<div class='card' style='border-color:rgba(255,118,117,0.3);margin-top:10px;'>"
"<div style='display:flex;gap:10px;'>"
"<button type='button' class='btn' style='flex:1;background:#3a7bd5;' onclick='rb()'>Khởi động lại</button>"
"<button type='button' class='btn' style='flex:1;background:#d63031;' onclick='rst()'>Khôi phục gốc</button>"
"</div></div>"
"</form>"
"<script>"
"const banks=[{b:'970436',n:'Vietcombank'},{b:'970415',n:'VietinBank'},{b:'970418',n:'BIDV'},{b:'970405',n:'Agribank'},{b:'970407',n:'Techcombank'},{b:'970422',n:'MBBank'},{b:'970423',n:'TPBank'},{b:'970416',n:'ACB'},{b:'970432',n:'VPBank'},{b:'sacom',b:'970403',n:'Sacombank'},{b:'970441',n:'VIB'},{b:'970425',n:'AnBinhBank'},{b:'970437',n:'HDBank'}];"
"const vals=['%BIN0%','%BIN1%','%BIN2%'];"
"function openTab(p,i){"
"const g=document.getElementById(p);"
"g.querySelectorAll('.tab-pane').forEach(e=>e.classList.remove('active'));"
"g.querySelectorAll('.tab-btn').forEach(e=>e.classList.remove('active'));"
"g.querySelectorAll('.tab-pane')[i].classList.add('active');"
"g.querySelectorAll('.tab-btn')[i].classList.add('active');"
"}"
"function upd(i){const s=document.getElementById('bin'+i);document.getElementById('bnV'+i).value=s.options[s.selectedIndex].text;}"
"function imp(e){const f=e.files[0];if(!f)return;const r=new FileReader();r.onload=function(x){const d=JSON.parse(x.target.result);d.forEach((a,i)=>{if(i>2)return;document.getElementById('bin'+i).value=a.bin;document.getElementsByName('acc'+i)[0].value=a.acc;document.getElementsByName('on'+i)[0].value=a.on;upd(i);});};r.readAsText(f);}"
"function rb(){if(confirm('Khởi động lại thiết bị?')){window.location.href='/reboot';}}"
"function rst(){if(confirm('Bạn có chắc chắn? Tất cả dữ liệu sẽ bị xóa!')){window.location.href='/reset';}}"
"function sc(){const b=document.getElementById('scb');b.innerHTML='...';b.disabled=true;"
"fetch('/scan').then(r=>r.json()).then(d=>{"
"const s=document.getElementById('ws');s.innerHTML='';"
"d.forEach(w=>{const o=document.createElement('option');o.value=w.s;o.text=w.s+' ('+w.r+'dBm)';s.appendChild(o);});"
"b.innerHTML='SCAN';b.disabled=false;});}"
"function cn(){const s=document.getElementById('ws').value;const p=document.getElementById('wp').value;const b=document.getElementById('cnb');"
"if(!s){alert('Vui lòng chọn WiFi trước');return;}b.innerHTML='Đang kết nối';b.disabled=true;"
"fetch('/connect_wifi?s='+encodeURIComponent(s)+'&p='+encodeURIComponent(p)).then(r=>r.text()).then(t=>{alert(t);if(t.includes('Connected'))lw();b.innerHTML='KẾT NỐI';b.disabled=false;});}"
"function lw(){fetch('/list_wifi').then(r=>r.json()).then(d=>{"
"const l=document.getElementById('wfl');l.innerHTML='<p style=\"font-size:0.8em;color:#aaa;border-bottom:1px solid rgba(255,255,255,0.1);padding-bottom:5px;margin-bottom:10px;\">Danh sách đã lưu</p>';"
"d.forEach((w,i)=>{"
"const v=document.createElement('div');v.className='form-group';v.style='display:flex;justify-content:space-between;align-items:center;background:rgba(255,255,255,0.05);padding:8px 12px;border-radius:8px;margin-bottom:5px;';"
"v.innerHTML=`<span style='font-size:0.9em;'>${w.s}</span><button type='button' style='background:none;border:none;color:#ff416c;cursor:pointer;font-size:1.2em;padding:0 5px;' onclick='dw(${i})'>&times;</button>`;"
"l.appendChild(v);});});}"
"function dw(i){if(confirm('Xóa mạng này?')){fetch('/del_wifi?i='+i).then(()=>lw());}}"
"for(let i=0;i<3;i++){const s=document.getElementById('bin'+i);banks.forEach(bk=>{const o=document.createElement('option');o.value=bk.b;o.text=bk.n;if(bk.b===vals[i])o.selected=true;s.appendChild(o);});upd(i);}"
"lw();"
"</script></div></body></html>";

// Giao diện trang update firmware
const char* update_html = 
"<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>body{font-family:sans-serif;background:#0f0c29;color:#fff;text-align:center;padding:30px;}"
".card{background:rgba(255,255,255,0.05);backdrop-filter:blur(10px);padding:30px;border-radius:20px;display:inline-block;border:1px solid rgba(255,255,255,0.1);}"
".btn{background:linear-gradient(to right,#ff416c,#ff4b2b);border:none;color:white;padding:12px 24px;font-size:16px;margin:20px 0;cursor:pointer;border-radius:8px;font-weight:bold;width:100%;}"
"input[type=file]{margin:20px 0;background:#3d3d3d;padding:15px;border-radius:10px;width:100%;box-sizing:border-box;color:#fff;font-size:16px;}.nav{margin-bottom:30px;}</style></head><body>"
"<div class='nav'><a href='/' style='color:#00d2ff;text-decoration:none;'>Quay lại</a></div>"
"<div class='card'><h2>Nâng cấp Firmware</h2>"
"<form method='POST' action='/update' enctype='multipart/form-data'>"
"<input type='file' name='update' accept='.bin'><br>"
"<input type='submit' value='NÂNG CẤP' class='btn'>"
"</form></div></body></html>";

void handleOTA() {
  if (!isPowerOn) {
    digitalWrite(TFT_BL, HIGH);
    tft.sendCommand(0x29);
    isPowerOn = true;
  }
  
  WiFi.softAP("QR Station", "88888888");
  IPAddress IP = WiFi.softAPIP();
  
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.println("CONFIG MODE");
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 30);
  tft.println("1. Connect WiFi");
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 42);
  tft.println("SSID: QR Station");
  tft.setCursor(10, 52);
  tft.println("Pass: 88888888");
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 70);
  tft.println("2. Access IP");
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 82);
  tft.print(IP);
  
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(10, 110);
  tft.println("Hold K4 to exit");

  server.on("/", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) {
      return server.requestAuthentication();
    }
    String wifiOptions = "";
    if (strlen(wifi_ssid) > 0) {
      wifiOptions = "<option value='" + String(wifi_ssid) + "'>" + String(wifi_ssid) + " (Đã lưu)</option>";
    } else {
      wifiOptions = "<option value=''>Tìm WiFi để kết nối</option>";
    }

    String html = config_html;
    for(int i=0; i<3; i++) {
        String s = String(i);
        html.replace("%BIN" + s + "%", accounts[i].bin);
        html.replace("%ACC" + s + "%", accounts[i].accNum);
        html.replace("%BN" + s + "%", accounts[i].bankName);
        html.replace("%ON" + s + "%", accounts[i].ownerName);
    }
    html.replace("%WIFI_LIST%", wifiOptions);
    html.replace("%WP%", wifi_pass);
    html.replace("%MS%", mqtt_server);
    html.replace("%MU%", mqtt_user);
    html.replace("%MP%", mqtt_pass);
    html.replace("%AU%", auth_user);
    html.replace("%AP%", auth_pass);

    server.send(200, "text/html", html);
  });

  server.on("/scan", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      json += "{\"s\":\"" + WiFi.SSID(i) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
      if (i < n - 1) json += ",";
    }
    json += "]";
    WiFi.scanDelete();
    server.send(200, "application/json", json);
  });

  server.on("/connect_wifi", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    String s = server.arg("s");
    String p = server.arg("p");
    
    WiFi.begin(s.c_str(), p.c_str());
    unsigned long startMs = millis();
    bool connected = false;
    
    while(millis() - startMs < 15000) {
      if(WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      delay(500);
    }
    
    if(connected) {
      addOrUpdateWifi(s, p);
      wifiMulti.addAP(s.c_str(), p.c_str()); // Add to multi-wifi immediately
      server.send(200, "text/plain", "Connected to " + s);
    } else {
      server.send(200, "text/plain", "Connect failed");
    }
  });

  server.on("/export", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
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
  });

  server.on("/list_wifi", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    server.send(200, "application/json", savedWifiList);
  });

  server.on("/del_wifi", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    int idx = server.arg("i").toInt();
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, savedWifiList);
    JsonArray arr = doc.as<JsonArray>();
    if (idx >= 0 && idx < arr.size()) {
      arr.remove(idx);
      String output;
      serializeJson(doc, output);
      saveWifiList(output);
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/updatefw", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    server.send(200, "text/html", update_html);
  });

  server.on("/save", HTTP_POST, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    for(int i=0; i<3; i++) {
        String s = String(i);
        saveAccount(i, server.arg("bin"+s), server.arg("acc"+s), server.arg("bn"+s), server.arg("on"+s));
    }
    
    if (server.hasArg("ws") && server.arg("ws") != "") {
      addOrUpdateWifi(server.arg("ws"), server.arg("wp"));
      wifiMulti.addAP(server.arg("ws").c_str(), server.arg("wp").c_str());
    }

    preferences.begin("bank_data", false);
    preferences.putString("m_serv", server.arg("ms"));
    preferences.putString("m_user", server.arg("mu"));
    preferences.putString("m_pass", server.arg("mp"));
    preferences.putString("a_user", server.arg("au"));
    preferences.putString("a_pass", server.arg("ap"));
    preferences.end();
    
    loadSettings();
    
    server.send(200, "text/html", "<html><body style='background:#0f0c29;color:#fff;text-align:center;padding-top:100px;font-family:sans-serif;'>"
                                  "<h1 style='color: green;'>Đã lưu!</h1>"
                                  "<a href='/' style='color:#00d2ff;'>Quay lại</a></body></html>");
  });

  server.on("/reset", HTTP_GET, [&]() {
    if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
    preferences.begin("bank_data", false);
    preferences.clear();
    preferences.end();
    loadSettings();
    server.send(200, "text/html", "<html><body style='background:#0f0c29;color:#fff;text-align:center;padding-top:100px;font-family:sans-serif;'>"
                                  "<h1 style='color: red;'>Đã xóa!</h1>"
                                  "<a href='/' style='color:#00d2ff;'>Quay lại</a></body></html>");
  });

  server.on("/reboot", HTTP_GET, [&]() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", "<html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><body style='background:#0f0c29;color:#fff;text-align:center;padding-top:100px;font-family:sans-serif;'>"
                                  "<p>Đang khởi động lại, bạn có thể đóng cửa sổ này</p></body></html>");
    delay(1000);
    ESP.restart();
  });
  
  server.on("/update", HTTP_POST, [&]() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", (Update.hasError()) ? "<html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><body style='background:#0f0c29;color:#fff;text-align:center;padding-top:100px;font-family:sans-serif;'>Update thất bại!</body></html>" : "<html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><body style='background:#0f0c29;color:#fff;text-align:center;padding-top:100px;font-family:sans-serif;'>"
                                  "<p>Update thành công!<br>Đang khởi động lại, bạn có thể đóng cửa sổ này</p></body></html>");
    delay(2000);
    ESP.restart();
  }, [&]() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      tft.fillScreen(ST77XX_BLACK);
      tft.setTextColor(ST77XX_YELLOW);
      tft.setCursor(10, 60);
      tft.println("Updating...");
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
  
  server.begin();
  unsigned long exitHoldStart = 0;
  while(true) {
    server.handleClient();

    if (digitalRead(BUTTON4) == LOW) {
      if (exitHoldStart == 0) exitHoldStart = millis();
      if (millis() - exitHoldStart > 2000) {
        server.stop();
        WiFi.softAPdisconnect(true);
        setupWiFi();
        Serial.println("Initial mode display");
        displayMode(currentMode);

        // Khởi tạo Web Server cho API
        server.on("/api/qr", HTTP_GET, []() {
          if (strlen(auth_user) > 0 && !server.authenticate(auth_user, auth_pass)) return server.requestAuthentication();
          handleApiQR();
        });
        server.begin();
        Serial.println("API Server started");
        while(digitalRead(BUTTON4) == LOW);
        delay(200);
        return;
      }
    } else {
      exitHoldStart = 0;
    }
    delay(1);
  }
}


void handleApiQR() {
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

  // Bật màn hình nếu đang tắt
  if (!isPowerOn) {
    togglePowerOnOnly();
  }

  displayMode(-1); // Mode -1 để báo hiệu vẽ QR động
  recordActivity();

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "QR Updated");
}

void togglePower() {
  if (isPowerOn) {
    digitalWrite(TFT_BL, LOW);
    tft.sendCommand(0x28);
    isPowerOn = false;
    Serial.println("Power OFF");
  } else {
    tft.sendCommand(0x29);
    digitalWrite(TFT_BL, HIGH);
    isPowerOn = true;
    Serial.println("Power ON");
  }
  delay(100);
}

void togglePowerOnOnly() {
  tft.sendCommand(0x29);
  digitalWrite(TFT_BL, HIGH);
  isPowerOn = true;
  recordActivity();
}

void setupWiFi() {
  connectionDisabled = false;
  wifiRetries = 0;
  mqttRetries = 0;
  lastWifiRetry = millis();

  WiFi.mode(WIFI_STA);
  
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, savedWifiList);
  JsonArray arr = doc.as<JsonArray>();
  
  if (arr.size() == 0) {
    Serial.println("No saved WiFi networks. Skipping connection");
    return;
  }
  
  for (JsonObject obj : arr) {
    const char* s = obj["s"];
    const char* p = obj["p"];
    wifiMulti.addAP(s, p);
    Serial.print("Added AP: "); Serial.println(s);
  }
  
  Serial.println("Connecting to best available WiFi...");
  wifiMulti.run();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] length: ");
  Serial.println(length);
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return;
  }
  
  // Case 1: JSON has "transactions" array (Pay2S format)
  if (doc.containsKey("transactions") && doc["transactions"].is<JsonArray>()) {
    JsonArray transactions = doc["transactions"].as<JsonArray>();
    for (JsonObject trans : transactions) {
      long amount = trans["transferAmount"] | trans["amount"] | 0;
      const char* content = trans["content"] | trans["description"] | trans["desc"] | "No content";
      const char* gateway = trans["gateway"] | "";
      const char* account = trans["accountNumber"] | "";
      if (amount > 0) {
        showNotification(amount, content, gateway, account);
      }
    }
  } 
  // Case 2: JSON is a flat object
  else if (doc.containsKey("amount") || doc.containsKey("transferAmount")) {
    long amount = doc["amount"] | doc["transferAmount"] | 0;
    const char* content = doc["content"] | doc["description"] | doc["desc"] | "No content";
    const char* gateway = doc["gateway"] | "";
    const char* account = doc["accountNumber"] | "";
    if (amount > 0) {
      showNotification(amount, content, gateway, account);
    }
  }
  // Case 3: JSON is a simple array
  else if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject trans : arr) {
      long amount = trans["amount"] | trans["transferAmount"] | 0;
      const char* content = trans["content"] | trans["description"] | trans["desc"] | "No content";
      const char* gateway = trans["gateway"] | "";
      const char* account = trans["accountNumber"] | "";
      if (amount > 0) {
        showNotification(amount, content, gateway, account);
      }
    }
  }
}


void reconnectMQTT() {
  if (connectionDisabled) return;
  if (strlen(mqtt_server) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  static unsigned long mqttRetryInterval = 5000;

  if (!mqttClient.connected()) {
    if (millis() - lastMqttRetry > mqttRetryInterval) {
      lastMqttRetry = millis();
      Serial.print("Attempting MQTT connection (Attempt ");
      Serial.print(mqttRetries + 1);
      Serial.print("/");
      Serial.print(MAX_RETRIES);
      Serial.println(")...");
      
      String serverStr = String(mqtt_server);
      String host = serverStr;
      int port = 1883;
      int colonIndex = serverStr.indexOf(':');
      if (colonIndex != -1) {
        host = serverStr.substring(0, colonIndex);
        port = serverStr.substring(colonIndex + 1).toInt();
      }

      mqttClient.setServer(host.c_str(), port);
      mqttClient.setBufferSize(2048);
      mqttClient.setCallback(mqttCallback);
      
      String clientId = "qr_station_";
      clientId += String(random(0xffff), HEX);
      
      bool connected = false;
      if (strlen(mqtt_user) > 0) {
        connected = mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass);
      } else {
        connected = mqttClient.connect(clientId.c_str());
      }

      if (connected) {

        Serial.println("MQTT connected");
        mqttClient.subscribe(mqtt_topic);
        mqttClient.subscribe("transfers"); // Support legacy topic from README
        mqttRetries = 0;
        mqttRetryInterval = 5000;
      } else {
        mqttRetries++;
        Serial.print("MQTT failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again in a moment");
        
        if (mqttRetries >= MAX_RETRIES) {
          Serial.println("MQTT connection failed persistent trials. Slowing down retries.");
          mqttRetryInterval = 30000; 
          mqttRetries = 0; 
        } else {
          mqttRetryInterval = 5000;
        }
      }
    }
  }
}

// Hiển thị thông báo
void showNotification(long amount, const char* content, const char* gateway, const char* account) {
  recordActivity();
  if (!isPowerOn) {
    tft.sendCommand(0x29);
    digitalWrite(TFT_BL, HIGH);
    isPowerOn = true;
  }
  
  tft.fillScreen(ST77XX_WHITE);
  tft.fillRect(0, 0, 128, 25, 0x03E0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds("GIAO DICH MOI", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((128 - w) / 2, 8);
  tft.print("GIAO DICH MOI");

  tft.setTextColor(ST77XX_BLACK);
  tft.setTextSize(2);
  String amtStr = "+" + String(amount);
  tft.getTextBounds(amtStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((128 - w) / 2, 45);
  tft.print(amtStr);

  tft.setTextSize(1);
  tft.setTextColor(0x0000);
  String bankInfo = String(gateway);
  if (strlen(account) > 0) bankInfo += " (" + String(account) + ")";
  
  if (bankInfo.length() > 0) {
    tft.getTextBounds(bankInfo, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((128 - w) / 2, 75);
    tft.print(bankInfo);
  }

  tft.setTextColor(0x5AEB);
  int contentY = 95;
  printCenteredWrapped(String(content), 15, contentY);

  Serial.print("Showing notification: +");
  Serial.print(amount);
  Serial.print(" - ");
  Serial.print(gateway);
  Serial.print(":");
  Serial.print(account);
  Serial.print(" - ");
  Serial.println(content);

  isShowingNotification = true;
  notificationStart = millis();
}

void recordActivity() {
  lastInputMs = millis();
}

void loop() {
  // Nút 1, 2, 3: Nếu màn hình tắt, nhấn nút bất kỳ chỉ để bật lên
  // --- XỬ LÝ NÚT NHẤN K1, K2, K3 ---
  // Nếu đang hiện QR động, nhấn nút bất kỳ sẽ quay về mode bình thường
  // Nếu đang hiện QR động hoặc thông báo, nhấn nút bất kỳ sẽ quay về mode bình thường
  if ((dynamicQR.active || isShowingNotification) && (digitalRead(BUTTON1) == LOW || digitalRead(BUTTON2) == LOW || digitalRead(BUTTON3) == LOW || digitalRead(BUTTON4) == LOW)) {
    dynamicQR.active = false;
    isShowingNotification = false;
    if (!isPowerOn) togglePowerOnOnly();
    displayMode(currentMode);
    delay(300);
    return;
  }

  // Nút K1 (Đổi Mode 1 / Đè 3s hiện IP)
  if (isPowerOn && digitalRead(BUTTON1) == LOW) {
    delay(50);
    if (digitalRead(BUTTON1) == LOW) {
      unsigned long startHold = millis();
      bool held = false;
      while(digitalRead(BUTTON1) == LOW) {
        if (millis() - startHold > 2000) {
          held = true;
          handleIPDisplay(); 
          break;
        }
        delay(10);
      }
      if (!held) {
        currentMode = 1; displayMode(1);
        recordActivity();
      }
    }
  }

  // Nút K2 (Đổi Mode 2 / Đè 3s Reboot)
  if (isPowerOn && digitalRead(BUTTON2) == LOW) {
    delay(50);
    if (digitalRead(BUTTON2) == LOW) {
      unsigned long startHold = millis();
      bool held = false;
      while(digitalRead(BUTTON2) == LOW) {
        if (millis() - startHold > 2000) {
          held = true;
          tft.fillScreen(ST77XX_BLACK);
          tft.setTextColor(ST77XX_WHITE);
          tft.setTextSize(1);
          tft.setCursor(10, 60);
          tft.println("Rebooting...");
          delay(1000);
          ESP.restart();
        }
        delay(10);
      }
      if (!held) {
        currentMode = 2; displayMode(2);
        recordActivity();
      }
    }
  }

  // Nút K3 (Đổi Mode 3 / Đè 3s Factory Reset)
  if (isPowerOn && digitalRead(BUTTON3) == LOW) {
    delay(50);
    if (digitalRead(BUTTON3) == LOW) {
      unsigned long startHold = millis();
      bool held = false;
      while(digitalRead(BUTTON3) == LOW) {
        if (millis() - startHold > 2000) {
          held = true;
          tft.fillScreen(ST77XX_BLACK);
          tft.setTextColor(ST77XX_RED);
          tft.setTextSize(1);
          tft.setCursor(10, 60);
          tft.println("Factory reset...");
          preferences.begin("bank_data", false);
          preferences.clear();
          preferences.end();
          delay(1000);
          ESP.restart();
        }
        delay(10);
      }
      if (!held) {
        currentMode = 3; displayMode(3);
        recordActivity();
      }
    }
  }

  // --- XỬ LÝ NÚT K4 (Nhấn đôi LED / Đè 3s Config) ---
  if (digitalRead(BUTTON4) == LOW) {
    delay(50);
    if (digitalRead(BUTTON4) == LOW) {
      if (!isPowerOn) {
        while(digitalRead(BUTTON4) == LOW) {
          delay(10);
        }

        unsigned long releaseTime = millis();
        bool doubleClicked = false;
        while (millis() - releaseTime < 400) {
          if (digitalRead(BUTTON4) == LOW) {
            delay(50);
            if (digitalRead(BUTTON4) == LOW) {
              doubleClicked = true;
              break;
            }
          }
          delay(10);
        }

        if (doubleClicked) {
          togglePower();
          recordActivity();
        }
      } else {
        unsigned long startHold = millis();
        bool held = false;
        while(digitalRead(BUTTON4) == LOW) {
          if (millis() - startHold > 2000) {
            held = true;
            handleOTA(); 
            break;
          }
          delay(10);
        }
        
        if (!held) {
          unsigned long releaseTime = millis();
          bool doubleClicked = false;
          while (millis() - releaseTime < 400) {
            if (digitalRead(BUTTON4) == LOW) {
              delay(50);
              if (digitalRead(BUTTON4) == LOW) {
                doubleClicked = true;
                break;
              }
            }
            delay(10);
          }
          
          if (doubleClicked) {
            togglePower();
          }
        }
        recordActivity();
      }
    }
  }

  if (isPowerOn && (millis() - lastInputMs >= INACTIVITY_TIMEOUT_MS)) {
    digitalWrite(TFT_BL, LOW);
    tft.sendCommand(0x28);
    isPowerOn = false;
    dynamicQR.active = false; // Tắt luôn QR động khi hết timeout
  }

  // Web Server handling - Chỉ chạy khi WiFi đã ok
  if (isServerStarted) {
    server.handleClient();
  }

  // Tự động thoát QR động sau 60 giây
  if (isPowerOn && dynamicQR.active && (millis() - dynamicQR.startTime >= 60000UL)) {
    dynamicQR.active = false;
    displayMode(currentMode);
    Serial.println("Dynamic QR timeout, reverting to static QR");
  }


  // MQTT & WiFi
  static bool lastWifiStatus = false;
  if (!connectionDisabled) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!lastWifiStatus) {
        Serial.print("WiFi connected! IP: ");
        Serial.println(WiFi.localIP());
        lastWifiStatus = true;
        wifiRetries = 0; 

        if (!isServerStarted) {
          initApiRoutes();
          server.begin();
          isServerStarted = true;
          Serial.println("API Server started on WiFi");
          
          if (MDNS.begin("qrstation")) {
            Serial.println("MDNS responder started (qrstation.local)");
          }
        }
      }
      if (!mqttClient.connected()) {
        reconnectMQTT();
      }
      mqttClient.loop();
    } else {
      lastWifiStatus = false;
      if (millis() - lastWifiRetry > 15000) { 
        lastWifiRetry = millis();
        Serial.println("WiFi disconnected, searching for saved networks...");
        wifiMulti.run();
      }
    }
  }

  // Tự động tắt màn hình thông báo sau 8 giây (đồng bộ với extension)
  if (isShowingNotification && (millis() - notificationStart > 8000)) {
    isShowingNotification = false;
    displayMode(currentMode);
    Serial.println("Notification closed, returning to QR");
  }

  // Cập nhật trạng thái định kỳ khi đang có kết nối
  static unsigned long lastStatusUpdate = 0;
  if (isPowerOn && !isShowingNotification && (millis() - lastStatusUpdate > 2000)) {
    lastStatusUpdate = millis();
    drawStatusDots();
  }

  yield(); // Giải phóng CPU cho các tác vụ hệ thống
  delay(10);
}

// Hàm tính toán CRC16 chuẩn EMVCo/QR
uint16_t crc16_ccitt(const char* data, int length) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

// Helper để đảm bảo độ dài luôn có 2 chữ số (ví dụ: 9 -> "09")
String fLen(int len) {
  if (len < 10) return "0" + String(len);
  return String(len);
}

// Hàm tạo chuỗi QR chuẩn VietQR (EMVCo)
String generateQRString(String bin, String account, String amount = "", String desc = "") {
  // 1. Thông tin định danh ngân hàng (Tag 38)
  // Tag 01 của Tag 38 chứa BIN (Tag 00) và STK (Tag 01)
  String sub01 = "0006" + bin + "01" + fLen(account.length()) + account;
  
  // Tag 38 gồm: GUID (A000000727), Data (Tag 01) và Service Code (QRIBFTTA)
  String tag38Content = String("0010A000000727") + "01" + fLen(sub01.length()) + sub01 + "0208QRIBFTTA";
  
  String qr = "000201";                             // Tag 00: Phiên bản (Cố định 01)
  
  // Tag 01: 11 (Static - QR cố định), 12 (Dynamic - QR kèm số tiền)
  if (amount != "" && amount != "0") qr += "010212";
  else qr += "010211";
  
  qr += "38" + fLen(tag38Content.length()) + tag38Content; // Tag 38
  qr += "5303704";                                         // Tag 53: Đơn vị tiền (704 = VND)
  
  if (amount != "" && amount != "0") {
    qr += "54" + fLen(amount.length()) + amount;           // Tag 54: Số tiền
  }
  
  qr += "5802VN";                                          // Tag 58: Quốc gia (VN)
  
  if (desc != "") {
    // Tag 62: Thông tin bổ sung, bên trong dùng Tag 08 để ghi nội dung chuyển khoản
    String tag62Content = "08" + fLen(desc.length()) + desc;
    qr += "62" + fLen(tag62Content.length()) + tag62Content;
  }
  
  qr += "6304"; // Tag cuối cùng (CRC)
  
  char crcStr[5];
  sprintf(crcStr, "%04X", crc16_ccitt(qr.c_str(), qr.length()));
  qr += String(crcStr);
  
  return qr;
}

// Hàm in văn bản căn giữa và tự động xuống dòng
void printCenteredWrapped(const String &text, int maxChars, int &currentY) {
  int screenWidth = 128; // Màn hình ST7735 dọc
  String name = text;
  name.trim();
  
  String currentLine = "";
  int i = 0;
  while (i < name.length()) {
    int nextSpace = name.indexOf(' ', i);
    int wordEnd = (nextSpace == -1) ? name.length() : nextSpace;
    String word = name.substring(i, wordEnd);
    
    if (word.length() == 0) {
      i = wordEnd + 1;
      continue;
    }

    String testLine = (currentLine == "") ? word : currentLine + " " + word;
    
    if (testLine.length() > maxChars) {
      if (currentLine != "") {
        int16_t tx, ty; uint16_t tw, th;
        tft.getTextBounds(currentLine, 0, 0, &tx, &ty, &tw, &th);
        tft.setCursor((screenWidth - tw) / 2, currentY);
        tft.print(currentLine);
        currentY += th + 3;
        currentLine = word;
      } else {
        int16_t tx, ty; uint16_t tw, th;
        tft.getTextBounds(word, 0, 0, &tx, &ty, &tw, &th);
        tft.setCursor((screenWidth - tw) / 2, currentY);
        tft.print(word);
        currentY += th + 3;
        currentLine = "";
      }
    } else {
      currentLine = testLine;
    }
    i = wordEnd + 1;
  }

  if (currentLine.length() > 0) {
    int16_t tx, ty; uint16_t tw, th;
    tft.getTextBounds(currentLine, 0, 0, &tx, &ty, &tw, &th);
    tft.setCursor((screenWidth - tw) / 2, currentY);
    tft.print(currentLine);
    currentY += th + 3;
  }
}

// Vẽ biểu tượng WiFi và MQTT
void drawStatusDots() {
  if (!isPowerOn || isShowingNotification) return;

  int idx = currentMode - 1;
  uint16_t bgColor = (strlen(accounts[idx].accNum) == 0) ? ST77XX_BLACK : ST77XX_WHITE;
  if (dynamicQR.active) bgColor = ST77XX_WHITE; // Dynamic QR always has white background

  // 1. Vẽ biểu tượng WiFi
  uint16_t wifiColor = (WiFi.status() == WL_CONNECTED) ? ST77XX_GREEN : ST77XX_RED;
  const int16_t W = 20;
  const int16_t H = 12;
  int16_t wx_start = 0, wy_start = 2;
 
  tft.fillRect(wx_start, wy_start, W, H, bgColor);

  const int16_t cx = wx_start + W / 2;
  const int16_t cy = wy_start + H - 2;

  int16_t radii[] = {9, 6, 3};
  for (int i = 0; i < 3; i++) {
    int16_t r = radii[i];
    for (int deg = 225; deg <= 315; deg += 3) {
      float rad = deg * 0.0174532925f;
      int16_t px = cx + (int16_t)(cos(rad) * r);
      int16_t py = cy + (int16_t)(sin(rad) * r);
      tft.drawPixel(px, py, wifiColor);
    }
  }
  tft.drawPixel(cx, cy, wifiColor);

  // 2. Vẽ biểu tượng MQTT
  uint16_t mqttColor = (mqttClient.connected()) ? ST77XX_GREEN : ST77XX_RED;
  int mx = 120, my = 8;
  tft.fillRect(mx - 3, my - 3, 7, 7, bgColor);
  tft.fillCircle(mx, my, 3, mqttColor);
}

void displayBankQR(const String &qrText,
                   const String &bankName,
                   const String &accountNumber,
                   const String &accountName) {
  
  // 1. Vẽ mã QR TRƯỚC
  int qrSize = 100;
  int qrTopMargin = 22;
  qrcode.setTopMargin(qrTopMargin); 
  qrcode.create(qrText);
  
  // 2. Chờ QR vẽ xong rồi mới in chữ đè lên
  tft.setTextColor(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  
  // 3. In tên ngân hàng ở vùng trống bên trên QR
  int currentY = 5; 
  printCenteredWrapped(bankName, 15, currentY);
  
  // 4. In thông tin tài khoản ở dưới QR
  currentY = qrTopMargin + qrSize + 1; 
  printCenteredWrapped(accountNumber, 15, currentY);
  currentY += 2;
  printCenteredWrapped(accountName, 15, currentY);
  
  tft.setTextWrap(true);
}

void displayEmptyMessage() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  
  // tft.setTextColor(ST77XX_CYAN);
  // tft.setCursor(10, 10);
  // tft.println("SYSTEM STATUS");

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(10, 30);
  tft.println("Empty QR");

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 110);
  tft.println("Hold K4 to setup");

  drawStatusDots();
}

void displayMode(int mode) {
  if (mode == -1 && dynamicQR.active) {
    tft.fillScreen(ST77XX_WHITE);
    displayBankQR(
      generateQRString(dynamicQR.bin, dynamicQR.acc, dynamicQR.amount, dynamicQR.desc),
      "",
      dynamicQR.amount + " VND",
      dynamicQR.name
    );
    drawStatusDots();
    Serial.println("Displaying Dynamic QR");
    return;
  }

  int idx = mode - 1;
  if (idx < 0 || idx > 2) idx = 0; // Fallback
  
  if (strlen(accounts[idx].accNum) == 0) {
    displayEmptyMessage();
    return;
  }

  tft.fillScreen(ST77XX_WHITE);
  displayBankQR(
    generateQRString(accounts[idx].bin, accounts[idx].accNum),
    accounts[idx].bankName,
    accounts[idx].accNum,
    accounts[idx].ownerName
  );
  drawStatusDots();
  Serial.print("Displaying Mode "); Serial.println(mode);
}

void handleIPDisplay() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  
  tft.setCursor(10, 10);
  tft.println("SYSTEM INFO");
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 30);
  tft.println("Device IP");

  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 42);
    tft.println(WiFi.localIP().toString());
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(10, 42);
    tft.println("Not connect WIFI");
  }
  
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(10, 110);
  tft.println("Hold K1 to exit");
  
  unsigned long exitHoldStart = 0;
  while(true) {
    // Vẫn xử lý API/MQTT để không bị treo khi đang xem IP
    if (isServerStarted) server.handleClient();
    if (mqttClient.connected()) mqttClient.loop();

    // Nếu có QR động tới, tự thoát để hiện QR
    if (dynamicQR.active || isShowingNotification) {
      return;
    }

    if (digitalRead(BUTTON1) == LOW) {
      if (exitHoldStart == 0) exitHoldStart = millis();
      if (millis() - exitHoldStart > 2000) {
        displayMode(currentMode);
        while(digitalRead(BUTTON1) == LOW);
        delay(200);
        return;
      }
    } else {
      exitHoldStart = 0;
    }
    delay(1);
  }
}

/*
 * Project: QR Station
 * Open Source: https://github.com/mhqb365/qr-station
 * Developer: mhqb365.com
 * Description: ESP32-C3 Based Bank QR Display
 */