# Camera Button

Physischer Drei-Tasten-Controller (M5Stack Basic Core, ESP32) für Home Assistant:
Kino-Leinwand rauf/runter und alle Überwachungskameras (Tapo + Eufy) ein/aus –
per Knopfdruck, ohne Handy oder Sprachassistent.

## Funktionen

- **Taste A – Kino ein**: löst die HA-Automation "Cinema On" aus. Muss ca. 1,5s
  gehalten werden (Schutz gegen versehentliches Auslösen), mit Fortschrittsbalken
  auf dem Display.
- **Taste B – Kino aus**: löst "Cinema Off" aus, normaler kurzer Druck.
- **Taste C – Kameras aus / ein**: schaltet Tapo-Steckdose + 2× Eufy-Kamera
  (`camera enabled`) aus bzw. wieder ein. Toggle-Verhalten: die Karte zeigt
  immer die nächste mögliche Aktion, ein Druck reicht.
- Grosses, farbiges Karten-UI mit einfachen Vektor-Icons statt kleiner Textzeilen.
- Kurze Sperre (2,5s) nach jedem Tastendruck, um Doppel-Auslösungen (z.B. Race
  Conditions bei schneller Bedienung) zu verhindern.
- Grüne/rote Einfärbung der Karte für 2s als Erfolg-/Fehler-Feedback.
- Kommunikation ausschliesslich über Home-Assistant-**Webhooks** – kein API-Token
  auf dem Gerät nötig, dadurch minimale Rechte falls das Gerät kompromittiert wird.

## Hardware

- [M5Stack Basic Core (ESP32 IoT Development Kit v2.7)](https://www.bastelgarage.ch/m5stack-basic-core-esp32-iot-development-kit-v2-7)
- Per USB an einen Rechner zum Flashen, danach im WLAN autark.

## Repository-Struktur

```
src/main.cpp                          Firmware (PlatformIO / Arduino, M5Unified)
include/secrets.h.example             Vorlage für WLAN- & HA-Zugangsdaten
platformio.ini                        Board-/Library-Konfiguration
homeassistant/cinema_on_off_webhooks.yaml   Trigger-Ergänzung für bestehende
                                             "Cinema On"/"Cinema Off"-Automationen
homeassistant/kameras_aus.yaml        Script "Kameras aus" + Webhook-Automation
homeassistant/kameras_ein.yaml        Script "Kameras ein" + Webhook-Automation
homeassistant/eufy-security-ws/       Docker-Compose für den eufy-security-ws-
                                       Server (Voraussetzung für die Eufy-
                                       Integration bei batteriebetriebenen Kameras)
```

## Setup

### 1. Firmware

Voraussetzung: [PlatformIO](https://platformio.org/) (`brew install platformio`).

```sh
cp include/secrets.h.example include/secrets.h
```

`include/secrets.h` mit echten Werten füllen (WLAN-SSID/Passwort, HA-Base-URL,
Webhook-IDs). Diese Datei ist in `.gitignore` und wird nie committed.

Danach bauen und flashen (Port anpassen, siehe `ls /dev/cu.*`):

```sh
pio run --target upload --upload-port /dev/cu.usbserial-XXXXXXXXXX
```

### 2. Home Assistant

Du brauchst pro Aktion eine Automation mit `trigger: webhook`, die deine
eigentliche Logik auslöst (bestehende Automation oder ein neues Script):

- **Cinema On / Cinema Off**: Trigger aus `homeassistant/cinema_on_off_webhooks.yaml`
  zu deinen bestehenden Automationen hinzufügen.
- **Kameras aus / ein**: Script + Wrapper-Automation aus
  `homeassistant/kameras_aus.yaml` bzw. `kameras_ein.yaml` anlegen. Entity-IDs
  der Kamera-Switches müssen auf deine Umgebung angepasst werden.

Alle Webhook-Trigger nutzen `local_only: true` – sie akzeptieren nur Anfragen
aus dem lokalen Netzwerk, kein Zugriff von aussen nötig oder möglich.

Für Benachrichtigungen im HA-Notifications-Center bei jeder Aktion wird
`persistent_notification.create` als letzter Schritt der jeweiligen Automation
aufgerufen (siehe Beispiele in den YAML-Dateien).

### 3. Eufy-Kameras (batteriebetrieben, z.B. Cam C2)

Diese benötigen die `eufy_security`-HACS-Integration, welche wiederum einen
separaten `eufy-security-ws`-Server voraussetzt (spricht per P2P mit der
Eufy-Homebase). Docker-Compose-Vorlage: `homeassistant/eufy-security-ws/docker-compose.yaml`.

```sh
mkdir -p /opt/eufy-security-ws/data
# docker-compose.yaml dorthin kopieren, USERNAME/PASSWORD/COUNTRY eintragen
cd /opt/eufy-security-ws && docker compose up -d
```

Danach in HA die `eufy_security`-Integration mit der Server-IP:Port (Standard
`3000`) des Docker-Hosts einrichten.

### 4. Tapo-Kamera

Wird über eine normale Smart-Steckdose (Kasa/Tapo) geschaltet, keine separate
Kamera-Integration nötig – deutlich robuster als die Cloud-Auth-basierten
Tapo-Kamera-Integrationen.

## Bekannte Einschränkungen

- Der Ein/Aus-Zustand der Kameras-Taste wird **lokal auf dem Gerät** gemerkt
  (kein Auslesen des echten HA-Zustands, um kein API-Token auf dem Gerät zu
  benötigen). Wird der Zustand extern geändert (App, HA-Dashboard), zeigt die
  Karte bis zum nächsten Tastendruck ggf. die falsche Beschriftung – der
  ausgelöste Webhook selbst ist davon nicht betroffen.
- Nach einem Neustart des Geräts wird "Kameras aus" als Startzustand angenommen.
