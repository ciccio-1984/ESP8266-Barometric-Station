#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Wire.h>
#include <BMP180.h>
#include <Adafruit_SSD1306.h>

// -------------------- DISPLAY --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// -------------------- BMP180 --------------------
BMP180 bmp180;

// -------------------- WIFI + PROVISIONING --------------------
ESP8266WebServer server(80);

String ssidSaved;
String passSaved;

// -------------------- STORICO PRESSIONE --------------------
const int HISTORY_SIZE = 18; // 3 ore (lettura ogni 10 min)
float history[HISTORY_SIZE];
int histIndex = 0;
bool histFull = false;

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 10UL * 60UL * 1000UL; // 10 minuti

// -------------------- SALVATAGGIO WIFI --------------------
void saveWiFiConfig(String ssid, String pass) {
  File f = SPIFFS.open("/wifi.txt", "w");
  if (!f) return;
  f.println(ssid);
  f.println(pass);
  f.close();
}

bool loadWiFiConfig(String &ssid, String &pass) {
  if (!SPIFFS.exists("/wifi.txt")) return false;

  File f = SPIFFS.open("/wifi.txt", "r");
  if (!f) return false;

  ssid = f.readStringUntil('\n'); ssid.trim();
  pass = f.readStringUntil('\n'); pass.trim();

  f.close();
  return true;
}

// -------------------- PAGINA WEB --------------------
void handleRoot() {
  int n = WiFi.scanNetworks();

  String html = "<html><body>"
                "<h2>Configurazione WiFi</h2>"
                "<form action='/save'>"
                "Rete WiFi:<br>"
                "<select name='ssid'>";

  for (int i = 0; i < n; i++) {
    html += "<option value='" + WiFi.SSID(i) + "'>";
    html += WiFi.SSID(i);
    html += " (" + String(WiFi.RSSI(i)) + " dBm)";
    html += "</option>";
  }

  html += "</select><br><br>"
          "Password:<br>"
          "<input name='pass' type='password'><br><br>"
          "<input type='submit' value='Connetti'>"
          "</form></body></html>";

  server.send(200, "text/html", html);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  saveWiFiConfig(ssid, pass);

  server.send(200, "text/html", "<h2>Salvato! Riavvio...</h2>");
  delay(1000);
  ESP.restart();
}

void startAPMode() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("MeteoSetup", "12345678");

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();
}

bool tryConnectWiFi() {
  if (!loadWiFiConfig(ssidSaved, passSaved)) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssidSaved.c_str(), passSaved.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
  }

  return WiFi.status() == WL_CONNECTED;
}

// -------------------- STORICO + TREND --------------------
void loadHistory() {
  if (!SPIFFS.exists("/history.bin")) return;

  File f = SPIFFS.open("/history.bin", "r");
  f.read((uint8_t*)history, sizeof(history));
  f.close();

  histFull = true;
}

void saveHistory() {
  File f = SPIFFS.open("/history.bin", "w");
  f.write((uint8_t*)history, sizeof(history));
  f.close();
}

float getTrend3h() {
  if (!histFull) return 0;

  int oldIndex = histIndex;
  float oldP = history[oldIndex];
  float nowP = history[(histIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE];

  return nowP - oldP;
}

// -------------------- GRAFICO --------------------
void drawGraph() {
  float minP = 2000, maxP = 0;

  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (!histFull && i >= histIndex) break;
    minP = min(minP, history[i]);
    maxP = max(maxP, history[i]);
  }

  float range = maxP - minP;
  if (range < 0.1) range = 0.1;

  for (int x = 0; x < HISTORY_SIZE; x++) {
    int idx = (histIndex + x) % HISTORY_SIZE;
    float p = history[idx];
    int h = map(p, minP, maxP, 0, 30);
    display.drawLine(x, 63, x, 63 - h, SSD1306_WHITE);
  }
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();

  SPIFFS.begin();

  if (!bmp180.begin()) {
    Serial.println("Errore BMP180!");
    while (1);
  }

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  loadHistory();

  if (!tryConnectWiFi()) {
    startAPMode();
  }
}

// -------------------- LOOP --------------------
void loop() {
  if (WiFi.getMode() == WIFI_AP_STA) {
    server.handleClient();
  }

  unsigned long now = millis();

  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;

    float p = bmp180.readPressure() / 100.0;

    history[histIndex] = p;
    histIndex = (histIndex + 1) % HISTORY_SIZE;
    if (histIndex == 0) histFull = true;

    saveHistory();

    float trend = getTrend3h();

    display.clearDisplay();
    display.setCursor(0,0);

    display.print("P: ");
    display.print(p, 1);
    display.println(" hPa");

    display.print("Trend 3h: ");
    display.print(trend, 1);
    display.println(" hPa");

    drawGraph();
    display.display();
  }
}
