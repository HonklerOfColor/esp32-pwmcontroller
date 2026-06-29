# ESP32-S3 Lüftersteuerung – 4× Arctic P14 Pro PST

Temperaturgesteuerte PWM-Lüfterregelung mit Webinterface, gebaut auf **ESP-IDF v6** (kein Arduino-Framework).  
Vier Arctic-P14-Pro-PST-Lüfter werden über einen IRLZ44N Low-Side-MOSFET mit 25 kHz PWM gesteuert.  
Der ESP arbeitet als **WLAN-Access-Point** – kein Router nötig, einfach verbinden und im Browser öffnen.

---

## Überblick

| Merkmal | Wert |
|---|---|
| MCU | ESP32-S3 DevKit |
| Lüfter | 4× Arctic P14 Pro PST (Zuluft, parallel) |
| PWM-Frequenz | 25 kHz |
| Regelung | Linear zwischen zwei konfigurierbaren Temperaturgrenzen |
| Sensor | BME280 (Temperatur + Luftfeuchte) via I²C |
| Nachtabsenkung | Zeitgesteuert, Uhr wird beim Öffnen der Webseite synchronisiert |
| Webinterface | Dunkles Responsive-Design, Live-Updates alle 2 s |
| WLAN-Modus | Access Point `espFanControl` / `P4ssw0rt!` |

---

## Hardware

### Stückliste

| Bauteil | Beschreibung |
|---|---|
| ESP32-S3 DevKit | Mikrocontroller mit WLAN |
| 4× Arctic P14 Pro PST | 140-mm-Lüfter (PST = gemeinsame PWM-Leitung) |
| IRLZ44N (TO-220) | Logic-Level MOSFET, Low-Side-Schalter |
| BME280 | Temperatur- + Luftfeuchtesensor, I²C |
| 100 µF / 25 V | Elektrolytkondensator (Entstörung, nah am MOSFET) |
| 100 nF | Keramikkondensator (Entstörung, parallel zum Elko) |
| 100 Ω | Gate-Widerstand (Schutz ESP32-Ausgang) |
| 10 kΩ | Pull-Down am Gate (MOSFET sicher AUS wenn GPIO unkonfiguriert) |

### Pinbelegung

| ESP32-S3-Pin | Funktion | Verbindung |
|---|---|---|
| GPIO 16 | PWM-Ausgang (25 kHz) | 100 Ω → Gate IRLZ44N |
| GPIO 21 | I²C SDA | BME280 SDA |
| GPIO 22 | I²C SCL | BME280 SCL |
| 3,3 V | Sensorversorgung | BME280 VIN |
| GND | Gemeinsame Masse | Alle GND |

### Schaltung (vereinfacht)

```
12V ──────────────────────┬──── Lüfter +12V (alle 4 parallel)
                          │
                    [100µF + 100nF]
                          │
GND ──────────────────────┤
                          │
Lüfter GND ──────── Drain (IRLZ44N)
                    Gate ──── 100Ω ──── GPIO16
                    Source ── GND
                    Gate ──── 10kΩ ─── GND (Pull-Down)

ESP32-S3 GPIO21 ──── BME280 SDA
ESP32-S3 GPIO22 ──── BME280 SCL
ESP32-S3 3.3V   ──── BME280 VIN
```

> **Hinweis:** Der IRLZ44N arbeitet mit 3,3 V Gate-Spannung ausreichend gut (R_DS(on) ≈ 35 mΩ bei 1,4 A Gesamtstrom = ca. 50 mW Verlustleistung).

---

## Features

- **25 kHz PWM** via LEDC-Peripheral (10-Bit, 1024 Stufen)
- **Sanfte Ramp-Funktion** – 0 % → 100 % in ~4 Sekunden, verhindert Stromspitzen
- **Automatische Temperaturregelung** – lineare Interpolation zwischen konfigurierbaren Temperaturgrenzen
- **Nachtabsenkung** – konfigurierbare Start-/Endzeit, maximale Lüftergeschwindigkeit nachts begrenzt
- **Zeitsync vom Browser** – beim ersten Laden der Webseite wird die ESP-Systemzeit automatisch gesetzt (kein NTP nötig)
- **Modularer Code** – separate C-Dateien pro Funktion, keine Arduino-Abhängigkeiten
- **Vollständiger BME280-Treiber** – direkt über IDF-I²C-API, keine Drittbibliothek

### Webinterface

| Seite | Adresse |
|---|---|
| Dashboard | `http://192.168.4.1` |

**Verfügbare API-Endpunkte:**

| Methode | Pfad | Beschreibung |
|---|---|---|
| `GET` | `/api/status` | JSON-Snapshot aller Werte |
| `POST` | `/api/control` | Modus (auto/manuell) und Lüfterprozent setzen |
| `POST` | `/api/config` | Temperaturgrenzwerte und Nachtmodus konfigurieren |
| `POST` | `/api/time` | Systemzeit vom Browser synchronisieren |

---

## Projektstruktur

```
esp32_pwmcontroller/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── app_config.h        ← Alle Konstanten, GPIO-Nummern, Shared State
    ├── main.c              ← app_main(), Initialisierungsreihenfolge
    ├── wifi_ap.c/.h        ← SoftAP + DHCP-Server
    ├── pwm_control.c/.h    ← LEDC 25 kHz + Ramp-Task
    ├── bme280.c/.h         ← I²C-Treiber (IDF v6 API) + Sensor-Task
    ├── temp_control.c/.h   ← Automatische Temperaturregelung
    ├── night_mode.c/.h     ← Nachtmodus-Task + Zeitsync
    ├── webserver.c/.h      ← HTTP-Server, alle API-Handler
    └── web/
        └── index.html      ← Eingebettetes Webinterface (EMBED_FILES)
```

---

## Bauen & Flashen

### Voraussetzungen

- [ESP-IDF v6.0+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- ESP32-S3 DevKit per USB verbunden

### Befehle

```bash
# IDF-Umgebung aktivieren (Pfad ggf. anpassen)
source ~/.espressif/v6.0.1/esp-idf/export.sh

# In das Projektverzeichnis wechseln
cd esp32_pwmcontroller

# Zielplatform setzen (nur beim ersten Mal)
idf.py set-target esp32s3

# Kompilieren
idf.py build

# Flashen (Port ggf. anpassen)
idf.py -p /dev/cu.usbmodem12501 flash

# Serieller Monitor
idf.py -p /dev/cu.usbmodem12501 monitor
```

### Alles in einem Schritt

```bash
idf.py -p /dev/cu.usbmodem12501 flash monitor
```

---

## Verbindung & Bedienung

1. Nach dem Booten erscheint das WLAN `espFanControl`
2. Mit Passwort `P4ssw0rt!` verbinden
3. Browser öffnen → `http://192.168.4.1`
4. Beim ersten Laden synchronisiert die Seite automatisch die Uhrzeit vom Browser

### Webinterface-Elemente

| Element | Funktion |
|---|---|
| Temperaturkarte | Aktuelle Raumtemperatur (BME280) |
| Feuchtigkeitskarte | Relative Luftfeuchtigkeit |
| Lüfter-Gauge | Aktueller Duty-Cycle als Halbkreis-Anzeige |
| Modus-Toggle | Wechsel zwischen Automatik und Manuell |
| Geschwindigkeitsregler | Manueller Lüfterprozentsatz (0–100 %) |
| Konfiguration | Temperaturgrenzen, Nachtabsenkung, Min-/Max-Speeds |
| Zeit sync | Setzt ESP-Systemzeit auf Browserzeit |

---

## Konfiguration (Standardwerte)

| Parameter | Standard | Beschreibung |
|---|---|---|
| `temp_low` | 22 °C | Lüfter läuft auf Minimum unterhalb dieser Temperatur |
| `temp_high` | 30 °C | Lüfter läuft auf Maximum oberhalb dieser Temperatur |
| `fan_min` | 20 % | Minimale Lüftergeschwindigkeit (im Betrieb) |
| `fan_max` | 100 % | Maximale Lüftergeschwindigkeit |
| `night_start` | 22 Uhr | Beginn Nachtabsenkung |
| `night_end` | 7 Uhr | Ende Nachtabsenkung |
| `night_max` | 50 % | Maximale Geschwindigkeit nachts |

Alle Werte sind zur Laufzeit über das Webinterface änderbar.

---

## Lizenz

MIT License – freie Verwendung, Änderung und Weitergabe.
