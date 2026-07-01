# ESP32-S3 Lüftersteuerung – 4× Arctic P14 Pro PST

Temperaturgesteuerte PWM-Lüfterregelung mit OLED-Display und Webinterface, gebaut auf **ESP-IDF v6** (kein Arduino-Framework).  
Vier Arctic-P14-Pro-PST-Lüfter werden über einen IRLZ44N Low-Side-MOSFET mit 25 kHz PWM gesteuert.  
Ein 0,96"-SSD1306-OLED zeigt Temperatur, Luftfeuchte, Lüfterleistung und Betriebsmodus direkt am Gerät an.  
Der ESP arbeitet als **WLAN-Access-Point** – kein Router nötig, einfach verbinden und im Browser öffnen.

---

<img width="1124" height="1244" alt="image" src="https://github.com/user-attachments/assets/cc8a1a88-fa48-41fc-b9c4-59e8a7535ea9" />


## Überblick

| Merkmal | Wert |
|---|---|
| MCU | Seeed Studio XIAO ESP32-S3 |
| Lüfter | 4× Arctic P14 Pro PST (Zuluft, parallel) |
| PWM-Frequenz | 25 kHz |
| Regelung | Linear zwischen zwei konfigurierbaren Temperaturgrenzen |
| Sensor | BME280 (Temperatur + Luftfeuchte) via I²C |
| Display | 0,96" OLED SSD1306 128×64 (I²C, 0x3C) – Temperatur, Feuchte, Lüfter, Modus |
| Nachtabsenkung | Zeitgesteuert, Uhr wird beim Öffnen der Webseite synchronisiert |
| Webinterface | Dunkles Responsive-Design, Live-Updates alle 2 s |
| WLAN-Modus | Access Point `espFanControl` / `P4ssw0rt!` |

---

## Hardware

### Stückliste

| Bauteil | Beschreibung |
|---|---|
| Seeed Studio XIAO ESP32-S3 | Kompakter ESP32-S3 (USB-C, 11 I/O-Pins) |
| 4× Arctic P14 Pro PST | 140-mm-Lüfter (PST = gemeinsame PWM-Leitung) |
| IRLZ44N (TO-220) | Logic-Level MOSFET, Low-Side-Schalter |
| BME280 | Temperatur- + Luftfeuchtesensor, I²C (Adresse 0x76) |
| SSD1306 OLED 0,96" | 128×64-Pixel-Display, I²C (Adresse 0x3C) – z. B. APKLVSR von Amazon |
| 100 µF / 25 V | Elektrolytkondensator (Entstörung, nah am MOSFET) |
| 100 nF | Keramikkondensator (Entstörung, parallel zum Elko) |
| 100 Ω | Gate-Widerstand (Schutz ESP32-Ausgang) |
| 10 kΩ | Pull-Down am Gate (MOSFET sicher AUS wenn GPIO unkonfiguriert) |

### Pinbelegung (XIAO ESP32-S3)

| Board-Label | GPIO | Funktion | Verbindung |
|---|---|---|---|
| D2 | GPIO 3 | PWM-Ausgang (25 kHz) | 100 Ω → Gate IRLZ44N |
| D4 / SDA | GPIO 5 | I²C SDA (gemeinsam) | BME280 SDA **und** OLED SDA |
| D5 / SCL | GPIO 6 | I²C SCL (gemeinsam) | BME280 SCL **und** OLED SCL |
| 3V3 | – | Sensorversorgung | BME280 VIN + OLED VCC |
| GND | – | Gemeinsame Masse | Alle GND |

> **I²C-Bus:** BME280 (0x76) und SSD1306 OLED (0x3C) teilen sich GPIO 5/6 bei **400 kHz**. Der Bus wird einmal in `i2c_bus.c` initialisiert; beide Treiber erhalten den gleichen Handle via `i2c_bus_get_handle()`. Externe Pull-ups (4,7 kΩ) sind optional – die internen Pull-ups des ESP32 reichen bei kurzen Leitungen.

> **Wichtig für XIAO-Nutzer:** GPIO 21 ist auf diesem Board die `USER_LED` und darf **nicht** als I²C-SDA verwendet werden. GPIO 22 existiert auf dem XIAO gar nicht.

### Schaltplan

#### 12-V-Kreis – Lüfter & MOSFET

```
  +12V (Netzteil)
       │
       ├─────────────────────────────────────────────── +12V (rot) ──────────────────┐
       │                                                                              │
     ──┤├── C1: 100 µF / 25 V  ──┐                                         ┌──────────┴──────────┐
     ──┤├── C2: 100 nF          ─┤ GND                               ┌─────┤   Lüfter L1         │
       │   (nah am MOSFET,       │                                   ├─────┤   Lüfter L2         │
       │    parallel zu 12V/GND) │                                   ├─────┤   Lüfter L3  (alle  │
       │                         │                                   └─────┤   Lüfter L4  4-pin) │
      GND                       GND                                        │                     │
                                                                           │   GND (schwarz) ─────┼──┐
                                                                           │   +12V (rot)    ─────┘  │
                                                                           │   Tacho (grün)  ─ n.c.  │
                                                                           │   PWM  (blau)   ─ n.c.  │
                                                                           └─────────────────────────┘
                                                                                                   │
                                                                                                   │ (alle 4 GND zusammen)
                                                                                                   │
                                                                                               ┌───┴───┐
                                                          GPIO3 (D2) ──── R1 (100 Ω) ──────────┤  G    │
                                                             GND    ──── R2 (10 kΩ)  ──────────┤  G    │  IRLZ44N
                                                                                               ├─ D    │  (TO-220)
                                                                                               │       │
                                                                                               └───┬───┘
                                                                                                   │ S (Source)
                                                                                                  GND
```

> R_DS(on) ≈ 35 mΩ bei V_GS = 3,3 V, I_D = 1,4 A → P_V ≈ 70 mW. Kein Kühlkörper nötig.

---

#### 3,3-V-Kreis – XIAO ESP32-S3 & I²C-Sensoren

```
  +3V3 (XIAO-Pin)
       │
       ├───────────────────────────────── VCC ─── BME280  (I²C-Adresse 0x76)
       │                                           ├── SDA ─────────────────────────-─┐
       │                                           ├── SCL ────────────────────────-┐ │
       │                                           └── GND ───── GND                │ │
       │                                                                            │ │
       └───────────────────────────────── VCC ─── SSD1306 OLED (I²C-Adresse 0x3C)   │ │
                                                   ├── SDA ────────────────────────-┤ │
                                                   ├── SCL ────────────────────────-┤ │
                                                   └── GND ───── GND                │ │
                                                                                    │ │
                                              ┌─────────────────────────────────────┘ │
                                              │   I²C-Bus (400 kHz)                   │
                                              │                             ┌─────────┘
                                              ▼                             ▼
                                  GPIO5 / D4 (SDA)            GPIO6 / D5 (SCL)
                                  [XIAO-Label: SDA]           [XIAO-Label: SCL]


  GPIO3 / D2 (PWM 25 kHz) ──── R1 ──── MOSFET Gate           (siehe 12-V-Kreis oben)
  GND (XIAO) ──────────────────────────────────────────────── GND (gemeinsam mit Netzteil)
```

> Externe Pull-up-Widerstände (4,7 kΩ) auf SDA/SCL sind **optional** – bei kurzen Leitungen (< 10 cm) reichen die internen Pull-ups des ESP32.

---

## Features

- **25 kHz PWM** via LEDC-Peripheral (10-Bit, 1024 Stufen)
- **Sanfte Ramp-Funktion** – 0 % → 100 % in ~4 Sekunden, verhindert Stromspitzen
- **Kickstart-Puls** – beim Anlaufen aus dem Stillstand kurz auf 60 % für 400 ms, damit der Motor zuverlässig anläuft
- **Automatische Temperaturregelung** – lineare Interpolation zwischen konfigurierbaren Temperaturgrenzen
- **Nachtabsenkung** – konfigurierbare Start-/Endzeit, maximale Lüftergeschwindigkeit nachts begrenzt
- **OLED-Display (SSD1306 128×64)** – zeigt Temperatur (groß), Luftfeuchte, Lüfterprozent, Modus und Uhrzeit; Refresh alle 2,5 s
- **Captive Portal** – Webseite öffnet sich automatisch nach WLAN-Verbindung (iOS, Android, Windows)
- **Zeitsync vom Browser** – beim ersten Laden der Webseite wird die ESP-Systemzeit automatisch gesetzt (kein NTP nötig)
- **Modularer Code** – separate C-Dateien pro Funktion, keine Arduino-Abhängigkeiten
- **Vollständiger BME280-Treiber** – direkt über IDF-I²C-API, keine Drittbibliothek

### OLED-Display (SSD1306 128×64)

Das Display wird über den gleichen I²C-Bus wie der BME280 betrieben und zeigt alle 2,5 Sekunden aktualisierte Werte:

```
┌────────────────────────────────┐
│ AUTO            20:15          │  ← Modus + Uhrzeit (klein)
│────────────────────────────────│
│                                │
│      23.4°C                    │  ← Temperatur (2× vergrößert)
│                                │
│ Feuchte:  52%                  │  ← Luftfeuchtigkeit
│ Luefter:  64%                  │  ← Lüfterleistung
│ [############      ]           │  ← Lüfter-Fortschrittsbalken
└────────────────────────────────┘
```

- Obere Zeile: Betriebsmodus (`AUTO` / `MANU`) und aktuelle Uhrzeit (wird vom Browser synchronisiert)
- Temperatur in doppelter Schriftgröße (gut lesbar aus Distanz)
- `°`-Symbol im eigenen Glyph (`\x7F`) des 5×7-Zeichensatzes
- Anzeige `---°C` wenn der Sensor noch keine Daten geliefert hat
- Kein externer Bibliotheks-Overhead: vollständig selbst implementierter Framebuffer-Treiber

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
    ├── i2c_bus.c/.h        ← Geteilter I²C-Master-Bus (BME280 + OLED)
    ├── wifi_ap.c/.h        ← SoftAP + DHCP-Server
    ├── pwm_control.c/.h    ← LEDC 25 kHz + Ramp-Task
    ├── bme280.c/.h         ← I²C-Treiber (IDF v6 API) + Sensor-Task
    ├── temp_control.c/.h   ← Automatische Temperaturregelung
    ├── night_mode.c/.h     ← Nachtmodus-Task + Zeitsync
    ├── captive_dns.c/.h    ← UDP-DNS-Server (Port 53), Captive Portal
    ├── webserver.c/.h      ← HTTP-Server, alle API-Handler + Portal-Redirects
    ├── oled.c/.h           ← SSD1306-Treiber + Display-Task (Framebuffer, 5×7-Font)
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
3. **Das Gerät zeigt automatisch eine Anmeldeanfrage** (Captive Portal) – antippen genügt
4. Alternativ Browser öffnen → `http://192.168.4.1`
5. Beim ersten Laden synchronisiert die Seite automatisch die Uhrzeit vom Browser

> **Captive Portal:** Ein eingebetteter DNS-Server leitet alle Anfragen auf den ESP um. iOS zeigt sofort ein „Anmelden"-Popup, Android eine Benachrichtigung, Windows öffnet automatisch den Browser.

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
| `KICKSTART_PCT` | 60 % | Anlaufleistung beim Kaltstart |
| `KICKSTART_MS` | 400 ms | Dauer des Kickstart-Pulses |
| `RAMP_INTERVAL_MS` | 20 ms | Wake-Intervall des Ramp-Tasks |
| `RAMP_STEP_PCT` | 0,5 % | Schrittweite pro Intervall (0→100 % in ~4 s) |
| `SENSOR_INTERVAL_MS` | 2000 ms | BME280-Leseintervall |
| `OLED_I2C_ADDR` | 0x3C | I²C-Adresse des SSD1306 (0x3D wenn SA0=High) |
| `OLED_REFRESH_MS` | 2500 ms | Display-Aktualisierungsintervall |
| `I2C_FREQ_HZ` | 400 000 Hz | I²C-Bus-Taktfrequenz |

Alle Laufzeit-Werte (`temp_low`, `temp_high`, `fan_min`, `fan_max`, Nachtmodus) sind über das Webinterface änderbar.  
Die übrigen Parameter werden zur Kompilierzeit in `app_config.h` festgelegt.

---

## Lizenz

MIT License – freie Verwendung, Änderung und Weitergabe.
