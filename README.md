# ESP32-S3 Lüftersteuerung – 4× Arctic P14 Pro PST

Temperaturgesteuerte PWM-Lüfterregelung mit OLED-Display und Webinterface, gebaut auf **ESP-IDF v6** (kein Arduino-Framework).  
Vier Arctic-P14-Pro-PST-Lüfter (4-Pin) werden über das **native 25-kHz-PWM-Signal direkt an Pin 4** gesteuert – kein MOSFET im 12-V-Pfad, kein Leistungsverlust.  
Ein 0,96"-SSD1306-OLED zeigt Temperatur, Luftfeuchte, Lüfterleistung und Betriebsmodus direkt am Gerät an.  
Der ESP arbeitet als **WLAN-Access-Point** – kein Router nötig, einfach verbinden und im Browser öffnen.

---

<img width="1124" height="1244" alt="image" src="https://github.com/user-attachments/assets/cc8a1a88-fa48-41fc-b9c4-59e8a7535ea9" />


## Überblick

| Merkmal | Wert |
|---|---|
| MCU | Seeed Studio XIAO ESP32-S3 |
| Lüfter | 4× Arctic P14 Pro PST (Zuluft, parallel) |
| PWM-Frequenz | 25 kHz, direkt auf Fan-Pin 4 (Intel 4-Wire-Spec) |
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
| 4× Arctic P14 Pro PST | 140-mm-Lüfter, 4-Pin PWM (PST = PWM-Leitungen aller 4 Lüfter parallel) |
| BME280 | Temperatur- + Luftfeuchtesensor, I²C (Adresse 0x76) |
| SSD1306 OLED 0,96" | 128×64-Pixel-Display, I²C (Adresse 0x3C) – z. B. APKLVSR von Amazon |
| 100 µF / 25 V | Elektrolytkondensator (Entstörung auf 12-V-Rail) |
| 100 nF | Keramikkondensator (Entstörung, parallel zum Elko) |
| 100 Ω | Serienwiderstand auf PWM-Leitung (Schutz GPIO, dämpft HF-Ringing) |

> Kein MOSFET mehr nötig: Die 4-Pin-Lüfter haben ihren eigenen internen Motorcontroller und akzeptieren das 25-kHz-PWM-Signal direkt. Die Arctic-P14-PST-Lüfter akzeptieren 3,3-V-Logikpegel (kein Level-Shifter nötig).

### Pinbelegung (XIAO ESP32-S3)

| Board-Label | GPIO | Funktion | Verbindung |
|---|---|---|---|
| D2 | GPIO 3 | PWM-Ausgang 25 kHz | 100 Ω → **Lüfter Pin 4** (PWM, alle 4 parallel via PST) |
| D4 / SDA | GPIO 5 | I²C SDA | BME280 SDA **und** OLED SDA |
| D5 / SCL | GPIO 6 | I²C SCL | BME280 SCL **und** OLED SCL |
| 3V3 | – | Sensorversorgung | BME280 VIN + OLED VCC |
| GND | – | Masse | Alle GND (gemeinsam mit 12-V-Netzteil-Minus) |

**4-Pin-Lüfter – Pinbelegung (pro Lüfter):**

| Lüfter-Pin | Farbe (Arctic) | Verbindung |
|---|---|---|
| 1 – GND | Schwarz | GND (Netzteil) |
| 2 – +12V | Gelb/Rot | +12V (Netzteil) |
| 3 – Tacho | Grün | nicht angeschlossen *(optional: GPIO2 + 4,7 kΩ pull-up → Drehzahlmessung)* |
| 4 – PWM | Blau | 100 Ω → GPIO3 (alle 4 Lüfter parallel, PST-Daisy-Chain) |

> **I²C-Bus:** GPIO 5/6, 400 kHz. BME280 (0x76) und OLED (0x3C) teilen sich den Bus – initialisiert einmalig in `i2c_bus.c`.  
> **XIAO-Hinweis:** GPIO 21 = USER_LED, GPIO 22 existiert nicht → niemals als I²C verwenden.

### Schaltplan

#### 12-V-Kreis – Lüfter (4-Pin, direkte PWM-Steuerung)

```
  +12V (Netzteil)
       │
       ├───────────────────────────────── +12V ────────────────────────────────────────┐
       │                                                                               │
     ──┤├── C1: 100 µF / 25 V ──┐                                          ┌──────────┴──────────┐
     ──┤├── C2: 100 nF         ─┤ GND                                ┌─────┤  L1: Arctic P14 PST │
       │   (nah an den Lüftern, │                                    ├─────┤  L2: Arctic P14 PST │
       │    parallel zu 12V/GND)│                                    ├─────┤  L3: Arctic P14 PST │
       │                        │                                    └─────┤  L4: Arctic P14 PST │
      GND                      GND                                         │                     │
                                                             Pin 2 (+12V)  │ ─────────────────── ┘ (oben, rot)
                                                             Pin 1 (GND)   │ ─── GND (Netzteil)
                                                             Pin 3 (Tacho) │ ─── n.c.  (opt.: 4,7kΩ → 3V3 → GPIO2)
                                                             Pin 4 (PWM)   │ ─── alle 4 zusammen ──┐
                                                                           └─────────────────────  │
                                                                                                   │
                                              GPIO3 (D2) ──── R1 (100 Ω) ─────────────────────────┘
                                              (XIAO ESP32-S3, 3,3-V-Logik, 25 kHz PWM)
```

> Die Lüfter haben einen **internen Motorcontroller**: Pin 4 steuert die Drehzahl direkt, +12V und GND sind dauerhaft verbunden. Kein MOSFET im Leistungspfad nötig.  
> **PST** (Parallel Speed Technology): die vier PWM-Pins (Pin 4) aller Lüfter sind über die PST-Daisy-Chain intern verbunden – ein einziges PWM-Signal steuert alle vier.

---

#### 3,3-V-Kreis – XIAO ESP32-S3 & I²C-Sensoren

```
  +3V3 (XIAO)
       │
       ├───── VCC ──── BME280  (0x76) ───── GND
       │               ├── SDA ──────────────────────────────── GPIO5 / D4 (XIAO SDA)
       │               └── SCL ──────────────────────────────── GPIO6 / D5 (XIAO SCL)
       │
       └───── VCC ──── SSD1306 (0x3C) ───── GND
                        ├── SDA ──────────────────────────────── GPIO5 / D4
                        └── SCL ──────────────────────────────── GPIO6 / D5

  GPIO3 / D2 ──── R1 (100 Ω) ──── Lüfter Pin 4 (PWM, alle 4 via PST)
  GND (XIAO) ──────────────────── GND (gemeinsam mit 12-V-Netzteil-Minus)
```

> Externe Pull-up-Widerstände (4,7 kΩ) auf SDA/SCL sind **optional** – bei kurzen Leitungen (< 10 cm) reichen die internen Pull-ups des ESP32.

---

## Features

- **25 kHz PWM** direkt auf Fan-Pin 4 (4-Pin-Standard, Intel PWM-Spec), via LEDC-Peripheral (10-Bit, 1024 Stufen)
- **Sanfte Ramp-Funktion** – 0 % → 100 % in ~4 Sekunden, verhindert Anlaufstrom-Spitzen
- **Kickstart-Puls** – beim Anlaufen aus dem Stillstand kurz auf 60 % für 400 ms, damit der Motor auch bei niedrigem PWM-Tastverhältnis sicher anläuft
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
