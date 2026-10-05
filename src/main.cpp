// Firmware perso pour GeekMagic SmallTV-Ultra (ESP8266).
//
// Regle d'or : /update doit TOUJOURS rester accessible, en Wi-Fi maison
// comme en point d'acces de secours. C'est la seule voie de mise a jour
// (et de retour au firmware d'origine) sans cable serie.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

// --- Materiel -------------------------------------------------------------
// Pas de bouton ni de zone tactile sur le SmallTV-Ultra.
static const uint8_t PIN_BACKLIGHT = 5;

// --- Constantes -----------------------------------------------------------
static const char *AP_SSID = "SmallTV-Custom";       // point d'acces de secours, ouvert
static const uint32_t WIFI_TIMEOUT_MS = 20000;
// En point d'acces sans personne connecte, on redemarre pour retenter le Wi-Fi
// (ex. coupure de courant : la box redemarre plus lentement que l'ecran).
static const uint32_t AP_RETRY_MS = 5UL * 60 * 1000;
static const uint32_t STOCK_FW_SIZE = 505200;         // firmware d'origine 9.0.50 et 9.0.51
static const char *SETTINGS_FILE = "/custom.json";    // n'ecrase aucun fichier d'origine

TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

bool fsMounted = false;
bool apMode = false;
uint32_t apStartedAt = 0;
uint8_t brightness = 70;   // 0..100 %
bool blInverted = true;    // polarite du retroeclairage, modifiable depuis le web

// --- Reglages persistants -------------------------------------------------

void saveSettings() {
  if (!fsMounted) return;
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return;
  f.printf("{\"brt\":%u,\"blinv\":%u}", brightness, blInverted ? 1 : 0);
  f.close();
}

void loadSettings() {
  if (!fsMounted || !LittleFS.exists(SETTINGS_FILE)) return;
  File f = LittleFS.open(SETTINGS_FILE, "r");
  String s = f.readString();
  f.close();
  int i = s.indexOf("\"brt\":");
  if (i >= 0) brightness = constrain(s.substring(i + 6).toInt(), 0, 100);
  i = s.indexOf("\"blinv\":");
  if (i >= 0) blInverted = s.substring(i + 8).toInt() != 0;
}

// Lit les identifiants Wi-Fi du firmware d'origine : {"a":"ssid","p":"mdp"}
bool readStockWifi(String &ssid, String &pass) {
  if (!fsMounted || !LittleFS.exists("/config.json")) return false;
  File f = LittleFS.open("/config.json", "r");
  String s = f.readString();
  f.close();
  auto field = [&](const char *key) -> String {
    String k = String("\"") + key + "\"";
    int i = s.indexOf(k);
    if (i < 0) return "";
    i = s.indexOf('"', s.indexOf(':', i + k.length()) + 1);
    int j = s.indexOf('"', i + 1);
    return (i < 0 || j < 0) ? "" : s.substring(i + 1, j);
  };
  ssid = field("a");
  pass = field("p");
  return ssid.length() > 0;
}

// --- Ecran ----------------------------------------------------------------

void applyBacklight() {
  uint32_t duty = map(brightness, 0, 100, 0, 1023);
  analogWrite(PIN_BACKLIGHT, blInverted ? 1023 - duty : duty);
}

// Grande police si le texte tient dans la largeur, sinon petite police.
void drawFit(const String &text, int y, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(text, 120, y, tft.textWidth(text, 4) <= 232 ? 4 : 2);
}

void drawStatus(const String &line1, const String &line2, const String &line3 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("SmallTV Custom", 120, 12, 4);
  tft.drawString(FW_VERSION, 120, 42, 2);

  // Mire de couleurs (validee sur l'appareil : Rouge / Vert / Bleu dans l'ordre).
  tft.fillRect(0, 70, 80, 50, TFT_RED);
  tft.fillRect(80, 70, 80, 50, TFT_GREEN);
  tft.fillRect(160, 70, 80, 50, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("R", 40, 87, 2);
  tft.drawString("V", 120, 87, 2);
  tft.drawString("B", 200, 87, 2);

  drawFit(line1, 128, TFT_YELLOW);
  drawFit(line2, 156, TFT_WHITE);
  drawFit(line3, 184, TFT_WHITE);
}

// --- Wi-Fi ----------------------------------------------------------------

bool waitForWifi() {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("smalltv-custom");

  // 1) identifiants memorises par le SDK (ceux du firmware d'origine en general)
  WiFi.begin();
  if (waitForWifi()) return;

  // 2) identifiants du fichier /config.json d'origine
  String ssid, pass;
  if (readStockWifi(ssid, pass)) {
    WiFi.begin(ssid.c_str(), pass.c_str());
    if (waitForWifi()) return;
  }

  // 3) point d'acces de secours : /update et /wifi y restent disponibles
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  apStartedAt = millis();
}

// --- Pages web ------------------------------------------------------------

String htmlPage() {
  uint32_t freeOta = ESP.getFreeSketchSpace();
  bool rollbackOk = freeOta >= STOCK_FW_SIZE;
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  String h;
  h.reserve(2600);
  h += F("<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>SmallTV Custom</title><style>"
         "body{font-family:sans-serif;max-width:560px;margin:auto;padding:16px;background:#111;color:#eee}"
         "a,button{color:#fff;background:#2a6;border:0;padding:10px 14px;border-radius:6px;text-decoration:none;display:inline-block;margin:4px 0}"
         ".warn{background:#a62}td{padding:4px 8px}.ok{color:#6d6}.bad{color:#f66}"
         "</style></head><body><h2>SmallTV Custom</h2><table>");
  h += "<tr><td>Version</td><td>" FW_VERSION "</td></tr>";
  h += "<tr><td>IP</td><td>" + ip + (apMode ? " (point d'acces)" : "") + "</td></tr>";
  h += "<tr><td>Heap libre</td><td>" + String(ESP.getFreeHeap()) + " o</td></tr>";
  h += "<tr><td>Taille firmware</td><td>" + String(ESP.getSketchSize()) + " o</td></tr>";
  h += "<tr><td>Place pour /update</td><td>" + String(freeOta) + " o</td></tr>";
  h += String("<tr><td>Retour firmware d'origine</td><td class='") + (rollbackOk ? "ok'>possible" : "bad'>TROP GROS") + "</td></tr>";
  h += String("<tr><td>Fichiers d'origine</td><td>") + (fsMounted ? "intacts (LittleFS monte)" : "non montes") + "</td></tr></table>";

  h += "<h3>Luminosite : " + String(brightness) + " %</h3>";
  h += F("<input type='range' min='0' max='100' id='b' value='");
  h += String(brightness);
  h += F("' onchange=\"location='/set?brt='+this.value\" style='width:100%'>"
         "<p><a href='/set?blinv=toggle'>Inverser le retroeclairage</a> "
         "(si l'ecran reste noir ou si le reglage marche a l'envers)</p>"
         "<h3>Wi-Fi</h3><p><a href='/wifi'>Changer de reseau Wi-Fi</a></p>"
         "<h3>Mise a jour</h3><p><a class='warn' href='/update'>Envoyer un firmware (.bin / .bin.gz)</a></p>"
         "<p>Retour a l'origine : envoyer le firmware officiel GeekMagic (.bin) sur la meme page.</p>"
         "<p><a href='/reboot'>Redemarrer</a></p></body></html>");
  return h;
}

void setupWeb() {
  // Page /update sans le formulaire "FileSystem" (qui effacerait toute la zone
  // de fichiers d'origine). Declaree AVANT updater.setup() pour remplacer sa
  // page GET ; le traitement POST reste celui, eprouve, de la bibliotheque.
  server.on("/update", HTTP_GET, [] {
    server.send(200, "text/html", F(
        "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Mise a jour</title><style>body{font-family:sans-serif;max-width:560px;"
        "margin:auto;padding:16px;background:#111;color:#eee}input{margin:8px 0}</style>"
        "</head><body><h2>Mise a jour du firmware</h2>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' accept='.bin,.bin.gz' name='firmware'><br>"
        "<input type='submit' value='Mettre a jour'></form>"
        "<p>Ne pas couper le courant pendant la mise a jour.</p>"
        "<p><a href='/' style='color:#8cf'>Retour</a></p></body></html>"));
  });
  updater.setup(&server, "/update");

  server.on("/", [] { server.send(200, "text/html", htmlPage()); });

  server.on("/set", [] {
    if (server.hasArg("brt")) {
      brightness = constrain(server.arg("brt").toInt(), 0, 100);
    }
    if (server.hasArg("blinv")) {
      blInverted = !blInverted;
    }
    applyBacklight();
    saveSettings();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/wifi", HTTP_GET, [] {
    String h = F("<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>"
                 "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                 "<title>Wi-Fi</title><style>body{font-family:sans-serif;max-width:560px;margin:auto;"
                 "padding:16px;background:#111;color:#eee}input,select,button{width:100%;padding:10px;"
                 "margin:6px 0;box-sizing:border-box}</style></head><body><h2>Reseau Wi-Fi</h2>"
                 "<form method='POST' action='/wifi'><select onchange=\"s.value=this.value\">"
                 "<option value=''>-- reseaux detectes --</option>");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      ssid.replace("'", "&#39;");
      ssid.replace("<", "&lt;");
      h += "<option value='" + ssid + "'>" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
    WiFi.scanDelete();
    h += F("</select><input id='s' name='ssid' placeholder='Nom du reseau (SSID)' required>"
           "<input name='pass' type='password' placeholder='Mot de passe'>"
           "<button>Enregistrer et redemarrer</button></form>"
           "<p><a href='/' style='color:#8cf'>Retour</a> - <a href='/update' style='color:#8cf'>Mise a jour firmware</a></p>"
           "</body></html>");
    server.send(200, "text/html", h);
  });

  server.on("/wifi", HTTP_POST, [] {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.isEmpty()) {
      server.send(400, "text/plain", "SSID manquant");
      return;
    }
    server.send(200, "text/html",
                "<meta charset='utf-8'>Enregistre. Redemarrage... Si la connexion echoue, "
                "le reseau SmallTV-Custom reapparaitra.");
    delay(300);
    // Memorise dans la zone Wi-Fi du SDK, puis redemarre
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    delay(500);
    ESP.restart();
  });

  server.on("/reboot", [] {
    server.send(200, "text/plain", "Redemarrage...");
    delay(300);
    ESP.restart();
  });

  // Compatibilite minimale avec l'interface d'origine
  server.on("/v.json", [] {
    server.send(200, "application/json", "{\"m\":\"SmallTV-Ultra\",\"v\":\"" FW_VERSION "\"}");
  });

  server.onNotFound([] {
    if (apMode) {  // portail captif simple : tout renvoie vers l'accueil
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302);
    } else {
      server.send(404, "text/plain", "Introuvable");
    }
  });

  server.begin();
}

// --- Programme principal --------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Monter LittleFS SANS jamais formater : on garde les fichiers d'origine.
  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  fsMounted = LittleFS.begin();
  loadSettings();

  analogWriteRange(1023);
  analogWriteFreq(1000);
  applyBacklight();

  tft.init();
  tft.setRotation(0);
  drawStatus("Connexion Wi-Fi...", "(jusqu'a 40 s)");

  startWifi();
  setupWeb();

  if (apMode) {
    drawStatus("Pas de Wi-Fi", String("Reseau : ") + AP_SSID, "http://192.168.4.1");
  } else {
    drawStatus("Connecte", "http://" + WiFi.localIP().toString());
  }
}

void loop() {
  server.handleClient();

  if (apMode && WiFi.softAPgetStationNum() == 0 && millis() - apStartedAt > AP_RETRY_MS) {
    ESP.restart();
  }
}
