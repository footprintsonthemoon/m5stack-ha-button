#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"

struct ButtonAction {
  const char *webhookId;
  const char *lineOne;
  const char *lineTwo;
  uint16_t color;
};

static const ButtonAction ACTIONS[4] = {
  {WEBHOOK_CINEMA_ON,   "Kino",     "ein", TFT_CYAN},
  {WEBHOOK_CINEMA_OFF,  "Kino",     "aus", TFT_ORANGE},
  {WEBHOOK_CAMERAS_OFF, "Kameras",  "aus", TFT_RED},
  {WEBHOOK_CAMERAS_ON,  "Kameras",  "ein", TFT_GREEN},
};

// Welche Aktion (Index in ACTIONS) gerade auf welchem Kartenplatz (0=A,1=B,2=C)
// angezeigt wird. Slot 2 wechselt zur Laufzeit zwischen "Kameras aus" (2) und
// "Kameras ein" (3), je nachdem was zuletzt erfolgreich ausgeloest wurde.
static int slotAction[3] = {0, 1, 2};

static const int TOPBAR_H = 30;
static const int CARD_MARGIN = 6;
static const int CARD_GAP = 6;
static const int CARD_Y = TOPBAR_H + 4;
static const int CARD_H = 240 - CARD_Y - 4;
static const int CARD_W = (320 - 2 * CARD_MARGIN - 2 * CARD_GAP) / 3;

static uint32_t lastTriggerMs = 0;
static const uint32_t TRIGGER_COOLDOWN_MS = 2500;
static const uint32_t HOLD_THRESHOLD_MS = 1500;

static int cardX(int i) { return CARD_MARGIN + i * (CARD_W + CARD_GAP); }

static void drawIcon(int index, int cx, int cy, uint16_t color) {
  switch (index) {
    case 0: // Kino ein -> Pfeil runter (Leinwand faehrt runter)
      M5.Display.fillRect(cx - 6, cy - 26, 12, 24, color);
      M5.Display.fillTriangle(cx - 20, cy - 2, cx + 20, cy - 2, cx, cy + 24, color);
      break;
    case 1: // Kino aus -> Pfeil hoch (Leinwand faehrt hoch)
      M5.Display.fillRect(cx - 6, cy + 2, 12, 24, color);
      M5.Display.fillTriangle(cx - 20, cy + 2, cx + 20, cy + 2, cx, cy - 24, color);
      break;
    case 2: // Kameras aus -> Kamera-Silhouette mit Schraegstrich
      M5.Display.fillRoundRect(cx - 26, cy - 12, 52, 30, 5, color);
      M5.Display.fillRect(cx - 10, cy - 22, 20, 10, color);
      M5.Display.fillCircle(cx, cy + 3, 12, TFT_BLACK);
      M5.Display.fillCircle(cx, cy + 3, 8, color);
      M5.Display.drawLine(cx - 32, cy - 26, cx + 32, cy + 26, TFT_WHITE);
      M5.Display.drawLine(cx - 32, cy - 25, cx + 32, cy + 27, TFT_WHITE);
      M5.Display.drawLine(cx - 32, cy - 27, cx + 32, cy + 25, TFT_WHITE);
      break;
    case 3: // Kameras ein -> gleiche Kamera-Silhouette, ohne Schraegstrich
      M5.Display.fillRoundRect(cx - 26, cy - 12, 52, 30, 5, color);
      M5.Display.fillRect(cx - 10, cy - 22, 20, 10, color);
      M5.Display.fillCircle(cx, cy + 3, 12, TFT_BLACK);
      M5.Display.fillCircle(cx, cy + 3, 8, color);
      break;
  }
}

static void drawCardState(int slot, int actionIndex, uint16_t bg, uint16_t fg, uint16_t textColor) {
  int x = cardX(slot);
  int y = CARD_Y;
  const ButtonAction &a = ACTIONS[actionIndex];

  M5.Display.fillRoundRect(x, y, CARD_W, CARD_H, 8, bg);
  M5.Display.drawRoundRect(x, y, CARD_W, CARD_H, 8, a.color);

  drawIcon(actionIndex, x + CARD_W / 2, y + CARD_H / 3, fg);

  M5.Display.setTextDatum(top_center);
  M5.Display.setTextColor(textColor);
  M5.Display.setTextSize(2);
  M5.Display.drawString(a.lineOne, x + CARD_W / 2, y + CARD_H - 56);
  M5.Display.drawString(a.lineTwo, x + CARD_W / 2, y + CARD_H - 32);
  M5.Display.setTextSize(1);
}

static void drawCardNormal(int slot) {
  int actionIndex = slotAction[slot];
  drawCardState(slot, actionIndex, M5.Display.color565(20, 20, 28), ACTIONS[actionIndex].color, TFT_WHITE);
}

static void drawCardSending(int slot, int actionIndex) {
  drawCardState(slot, actionIndex, ACTIONS[actionIndex].color, TFT_BLACK, TFT_BLACK);
}

static void drawCardResult(int slot, int actionIndex, bool success) {
  uint16_t bg = success ? TFT_GREEN : TFT_RED;
  drawCardState(slot, actionIndex, bg, TFT_BLACK, TFT_BLACK);
}

static void drawHoldProgress(int slot, float progress, uint16_t color) {
  int x = cardX(slot);
  int y = CARD_Y + CARD_H - 14;
  int trackW = CARD_W - 8;
  int w = (int)(trackW * progress);
  if (w > trackW) w = trackW;
  M5.Display.fillRect(x + 4, y, trackW, 8, M5.Display.color565(20, 20, 28));
  M5.Display.fillRect(x + 4, y, w, 8, color);
}

static void drawTopBar(const char *msg, uint16_t color) {
  M5.Display.fillRect(0, 0, 320, TOPBAR_H, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color);
  M5.Display.setTextSize(1);
  M5.Display.drawString(msg, 160, TOPBAR_H / 2);
}

static void drawHomeScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  for (int i = 0; i < 3; i++) drawCardNormal(i);
}

static void connectWiFi() {
  drawTopBar("Scanne WLAN...", TFT_YELLOW);
  WiFi.mode(WIFI_STA);

  int n = WiFi.scanNetworks();
  Serial.printf("Scan fertig, %d Netzwerke gefunden:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  [%2d] SSID=\"%s\" RSSI=%d ch=%d enc=%d\n",
                  i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  (int)WiFi.encryptionType(i));
  }

  drawTopBar("Verbinde WLAN...", TFT_YELLOW);
  Serial.printf("Verbinde mit SSID \"%s\" ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  wl_status_t status;
  while ((status = WiFi.status()) != WL_CONNECTED && millis() - start < 15000) {
    Serial.printf("  WiFi.status() = %d\n", status);
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Verbunden! IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    drawTopBar("WLAN verbunden", TFT_GREEN);
  } else {
    Serial.printf("Fehlgeschlagen, letzter Status = %d\n", WiFi.status());
    String msg = "WLAN Fehler " + String((int)WiFi.status());
    drawTopBar(msg.c_str(), TFT_RED);
    delay(4000);
  }
}

static void triggerWebhook(int slot, int actionIndex) {
  const ButtonAction &action = ACTIONS[actionIndex];

  uint32_t now = millis();
  if (now - lastTriggerMs < TRIGGER_COOLDOWN_MS) {
    drawTopBar("Bitte kurz warten...", TFT_ORANGE);
    return;
  }
  lastTriggerMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    drawTopBar("Kein WLAN!", TFT_RED);
    return;
  }

  String label = String(action.lineOne) + " " + action.lineTwo;
  drawCardSending(slot, actionIndex);
  drawTopBar(label.c_str(), TFT_YELLOW);

  HTTPClient http;
  String url = String(HA_BASE_URL) + "/api/webhook/" + action.webhookId;
  http.begin(url);
  int code = http.POST("");
  http.end();

  bool success = code >= 200 && code < 300;
  drawCardResult(slot, actionIndex, success);
  if (success) {
    String ok = label + " OK";
    drawTopBar(ok.c_str(), TFT_GREEN);
    // Slot 2 (Kameras) wechselt nach Erfolg zur jeweils anderen Aktion, damit
    // die Karte immer die naechste moegliche Aktion anzeigt.
    if (actionIndex == 2) slotAction[2] = 3;
    if (actionIndex == 3) slotAction[2] = 2;
  } else {
    String err = label + " Fehler " + String(code);
    drawTopBar(err.c_str(), TFT_RED);
  }

  delay(2000);
  drawCardNormal(slot);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(1);

  M5.BtnA.setHoldThresh(HOLD_THRESHOLD_MS);

  connectWiFi();
  drawHomeScreen();
}

void loop() {
  M5.update();

  // Kino ein (Taste A) braucht einen langen Druck als Schutz gegen
  // versehentliches Ausloesen - kurzes Antippen macht nichts.
  if (M5.BtnA.isPressed() && !M5.BtnA.isHolding()) {
    uint32_t elapsed = millis() - M5.BtnA.lastChange();
    drawHoldProgress(0, (float)elapsed / HOLD_THRESHOLD_MS, ACTIONS[0].color);
  }
  if (M5.BtnA.wasHold()) {
    triggerWebhook(0, 0);
  }
  if (M5.BtnA.wasClicked()) {
    drawTopBar("Bitte laenger halten", TFT_ORANGE);
    drawCardNormal(0);
  }

  if (M5.BtnB.wasPressed()) triggerWebhook(1, 1);

  // Taste C: einfacher kurzer Druck toggelt zwischen "Kameras aus" und
  // "Kameras ein", je nachdem was die Karte gerade anzeigt.
  if (M5.BtnC.wasPressed()) triggerWebhook(2, slotAction[2]);

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
}
