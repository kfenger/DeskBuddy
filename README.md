# DeskBuddy --- ESP32-C6 Touch LCD 1.47

Et lite DeskBuddy-prosjekt bygget rundt **Waveshare
ESP32-C6-Touch-LCD-1.47**.

Prosjektet viser et enkelt, minimalistisk brukergrensesnitt med ansikt,
klokke, dato, vær, månefase, aksjekurs og GitHub-statistikk. Navigering
skjer med touch på skjermen --- det er **ingen automatisk
sideskifting**.

## Innhold

-   [Maskinvare](#maskinvare)
-   [Funksjoner](#funksjoner)
-   [Sider](#sider)
-   [Pinout](#pinout)
-   [Programvare](#programvare)
-   [Installering](#installering)
-   [Arduino IDE-innstillinger](#arduino-ide-innstillinger)
-   [Konfigurasjon](#konfigurasjon)
-   [Touch og navigering](#touch-og-navigering)
-   [Vær](#vær)
-   [IMU](#imu)
-   [USB-C](#usb-c)
-   [Feilsøking](#feilsøking)
-   [Gjøre FACE-siden mer clean](#gjøre-face-siden-mer-clean)
-   [Prosjektstruktur](#prosjektstruktur)
-   [Status](#status)

------------------------------------------------------------------------

## Maskinvare

**Hovedkort:**

Waveshare ESP32-C6-Touch-LCD-1.47

Skjermen er:

-   320 × 172 piksler i prosjektets landskapsmodus
-   JD9853-basert LCD
-   Kapasitiv AXS5106L-touch
-   QMI8658A IMU

Prosjektet bruker kun USB-C for strøm og programmering. Batteri er ikke
nødvendig.

------------------------------------------------------------------------

## Funksjoner

DeskBuddy-programmet inneholder:

-   🙂 Animert ansikt
-   👀 Øyebevegelse basert på IMU
-   😄 Flere ansiktsuttrykk ved trykk på FACE-siden
-   🕒 Klokke
-   📅 Dato
-   🌦️ Vær fra Open-Meteo
-   🌙 Månefase
-   📈 AAPL-aksjekurs
-   GitHub-statistikk
-   👆 Touch/swipe-navigering
-   🇳🇴 Grimstad-vær
-   Celsius
-   km/h
-   Europe/Oslo

**Viktig:** Sider byttes ikke automatisk. Navigering skjer med touch.

------------------------------------------------------------------------

## Sider

  Side   Innhold    Navigering
  ------ ---------- -----------------------------
  0      FACE       Trykk endrer ansiktsuttrykk
  1      TIME       Touch/swipe til neste side
  2      DATE       Touch/swipe til neste side
  3      GRIMSTAD   Værdata
  4      MOON       Månefase
  5      AAPL       Aksjekurs
  6      GITHUB     GitHub-statistikk

Horisontal swipe:

-   Swipe mot venstre → neste side
-   Swipe mot høyre → forrige side

På FACE-siden endrer et kort trykk ansiktsuttrykket i stedet for å bytte
side.

------------------------------------------------------------------------

## Pinout

### LCD

  Funksjon     GPIO
  ---------- ------
  LCD SCLK        1
  LCD MOSI        2
  LCD CS         14
  LCD DC         15
  LCD RST        22
  LCD BL         23

### Touch / I2C

  Funksjon                GPIO
  ------------------- --------
  Touch SDA                 18
  Touch SCL                 19
  Touch RST                 20
  Touch INT                 21
  Touch I2C-adresse     `0x63`

### IMU

  Funksjon                                    Verdi
  ---------------------- --------------------------
  QMI8658A I2C-adresse                       `0x6B`
  Buss                     Samme I2C-buss som touch

Touch og IMU deler altså I2C-bussen.

------------------------------------------------------------------------

## Programvare

Koden er laget for:

-   Arduino IDE 1.8.19
-   ESP32 Arduino core 3.3.8
-   Board: `ESP32C6 Dev Module`

Bibliotekene som brukes av sketchen er:

``` cpp
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
```

### Biblioteker

Installer nødvendige biblioteker gjennom Arduino IDEs Library Manager,
spesielt:

-   Arduino_GFX_Library
-   ArduinoJson

ESP32-støtten installeres via Boards Manager.

------------------------------------------------------------------------

## Installering

1.  Installer Arduino IDE 1.8.19.
2.  Åpne **Boards Manager**.
3.  Installer: **esp32 by Espressif Systems**
4.  Prosjektet er testet med ESP32 core **3.3.8**.
5.  Installer nødvendige biblioteker.
6.  Åpne `.ino`-filen.
7.  Velg riktig ESP32-C6-board.
8.  Velg riktig USB-port.
9.  Trykk **Verify**.
10. Hvis kompileringen lykkes, trykk **Upload**.

------------------------------------------------------------------------

## Arduino IDE-innstillinger

Bruk følgende innstillinger:

``` text
Board:             ESP32C6 Dev Module
Upload Speed:      115200
USB CDC On Boot:   Enabled
CPU Frequency:     160MHz (WiFi)
Flash Frequency:   80MHz
Flash Mode:        QIO
Flash Size:        4MB (32Mb)
Partition Scheme:  Default 4MB with spiffs
Core Debug Level:  None
Erase Flash:       Disabled
JTAG Adapter:      Disabled
Zigbee Mode:       Disabled
```

Eksempel på port:

``` text
/dev/ttyACM0
```

Portnummeret kan være annerledes på andre Linux-systemer.

------------------------------------------------------------------------

## Konfigurasjon

Wi-Fi og GitHub-bruker ligger øverst i `.ino`-filen:

``` cpp
#define WIFI_SSID     "Orange"
#define WIFI_PASSWORD "VelkommenTilSOC1."
#define GITHUB_USER   "kfenger"
```

Bytt disse verdiene hvis prosjektet skal brukes på et annet nettverk
eller med en annen GitHub-konto.

### Sikkerhet

Ikke legg ekte Wi-Fi-passord i et offentlig GitHub-repository.

For et offentlig repository bør Wi-Fi-opplysningene flyttes til en lokal
konfigurasjonsfil som ikke committes.

------------------------------------------------------------------------

## Touch og navigering

Touchkontrolleren er AXS5106L.

Prosjektet bruker:

``` cpp
#define AXS5106L_ADDR 0x63
#define AXS5106L_TOUCH_DATA_REG 0x01
```

Touchpakken leses som:

``` text
gesture
touch count
X high
X low
Y high
Y low
```

Koden bruker den første aktive touch-punktet.

### Ingen automatisk sideskifting

Det finnes ingen timer som automatisk endrer `currentApp`.

Hovedløkken leser touch:

``` cpp
readTouch();
```

og nettverkssider oppdateres separat:

``` cpp
updateNetworkPages();
```

Dette betyr at vær, aksje- og GitHub-data kan oppdateres uten at selve
siden automatisk skifter.

------------------------------------------------------------------------

## Vær

Værsiden bruker Open-Meteo.

Den er konfigurert for Grimstad-området med koordinatene som ligger i
sketchen:

``` text
latitude=58.3405
longitude=8.5934
```

API-innstillingene er:

``` text
temperature_unit=celsius
wind_speed_unit=kmh
timezone=Europe/Oslo
```

Værsiden viser:

-   temperatur i °C
-   luftfuktighet
-   vind i KM/H
-   værtype
-   dag/natt-status

Værdata oppdateres med intervaller på omtrent 15 minutter når vær-siden
brukes.

------------------------------------------------------------------------

## IMU

QMI8658A brukes til å registrere hvordan enheten holdes og beveges.

På FACE-siden brukes dette til å flytte øynene.

Ved rask rotasjon kan ansiktsuttrykket også endres.

Hvis IMU ikke starter, bruker programmet en animert fallback-bevegelse
slik at ansiktet fortsatt fungerer.

Ved oppstart utføres også en kort nøytral-kalibrering.

------------------------------------------------------------------------

## USB-C

Prosjektet trenger ikke batteri.

USB-C brukes til:

-   strøm
-   programmering
-   seriell kommunikasjon

Ingen firmware-innstilling er nødvendig for USB-C-only bruk.

------------------------------------------------------------------------

## Feilsøking

### 1. `boot_app0.bin': -c: line 1: unexpected EOF`

Hvis du får:

``` text
boot_app0.bin': -c: line 1: unexpected EOF while looking for matching `''
exit status 2
```

skal du først kontrollere:

-   Arduino IDE-versjon
-   ESP32 board package-versjon
-   valgt board
-   Linux-verktøykjeden

Dette prosjektet ble satt opp med Arduino IDE 1.8.19 og ESP32 core
3.3.8.

Warnings som:

``` text
WARNING: Category 'Sound' in library ESP_SR is not valid.
WARNING: Category 'Security' in library Hash is not valid.
WARNING: Category '' in library ESP Insights is not valid.
```

er bibliotekmetadata-advarsler. Hvis kompileringen avsluttes med
`Sketch uses ...`, er dette ikke kompileringsfeil.

------------------------------------------------------------------------

### 2. `ESP_ERR_INVALID_STATE` fra I2C

Eksempel:

``` text
i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
```

Dette gjelder I2C-kommunikasjonen.

Sjekk først:

-   SDA = GPIO18
-   SCL = GPIO19
-   Touch-adresse = `0x63`
-   IMU-adresse = `0x6B`
-   at kortet får stabil strøm via USB-C

Touch og IMU deler samme I2C-buss.

------------------------------------------------------------------------

### 3. Touch virker ikke

Kontroller:

``` cpp
#define TOUCH_SDA 18
#define TOUCH_SCL 19
#define TOUCH_RST 20
#define TOUCH_INT 21

#define AXS5106L_ADDR 0x63
#define AXS5106L_TOUCH_DATA_REG 0x01
```

Åpne Serial Monitor på:

``` text
115200 baud
```

Ved oppstart skal du blant annet se:

``` text
ESP32-C6 DeskBuddy starting
Touch controller initialised
```

------------------------------------------------------------------------

### 4. Skjermen er delt, rotert eller feiljustert

Kontroller:

``` cpp
static const uint8_t ROTATION = 1;
```

Displayet opprettes som:

``` cpp
Arduino_ST7789(
  bus,
  LCD_RST,
  0,
  false,
  172,
  320,
  34,
  0,
  34,
  0
);
```

og deretter:

``` cpp
display->setRotation(ROTATION);
```

LCD-initsekvensen i `lcdRegInit()` er viktig for dette panelet og bør
ikke fjernes.

------------------------------------------------------------------------

### 5. Været viser ikke noe

Kontroller:

-   Wi-Fi SSID
-   Wi-Fi-passord
-   at ESP32-C6 er koblet til internett
-   at `GRIMSTAD`-siden er åpnet

På siden kan det først stå:

``` text
CONNECTING
```

eller:

``` text
UPDATING
```

før data kommer inn.

------------------------------------------------------------------------

## Gjøre FACE-siden mer clean

FACE-siden kan gjøres mer minimalistisk ved å fjerne de to stiplete
linjene rundt ansiktet.

Finn denne blokken i `drawFace()`:

``` cpp
for (int x=12; x<SCREEN_W-12; x+=18) {
  gfx->drawLine(x,   31, x+8,  31, FG);
  gfx->drawLine(x+4,144, x+12,144, FG);
}
```

Slett blokken.

Resten av FACE-siden kan beholdes.

Dette fjerner de stiplete linjene, men beholder:

-   øynene
-   øyebevegelse
-   blinking
-   munn
-   ansiktsuttrykk
-   sideindikatorene nederst

------------------------------------------------------------------------

## Prosjektstruktur

En enkel prosjektmappe kan se slik ut:

``` text
DeskBuddy/
├── DeskBuddy_Grimstad_TOUCH_I2C_FIXED.ino
└── README.md
```

Hvis prosjektet legges på GitHub, anbefales det å holde hemmelig
konfigurasjon utenfor repositoryet.

Eksempel:

``` text
DeskBuddy/
├── DeskBuddy_Grimstad_TOUCH_I2C_FIXED.ino
├── README.md
└── secrets.example.h
```

------------------------------------------------------------------------

## Seriell diagnostikk

Serial Monitor bruker:

``` text
115200 baud
```

Programmet skriver blant annet:

``` text
ESP32-C6 DeskBuddy starting
Touch controller initialised
IMU initialised
```

og periodisk:

``` text
app=0 mood=0
```

`app` viser gjeldende side:

``` text
0 = FACE
1 = TIME
2 = DATE
3 = GRIMSTAD
4 = MOON
5 = AAPL
6 = GITHUB
```

`mood` viser gjeldende ansiktsuttrykk.

------------------------------------------------------------------------

## Status

### Fungerer

-   [x] ESP32-C6
-   [x] 320×172 LCD
-   [x] Landskapsmodus
-   [x] Touch
-   [x] Swipe-navigering
-   [x] Touch-only sideskifting
-   [x] Ingen automatisk sideskifting
-   [x] FACE-animasjon
-   [x] IMU
-   [x] Klokke
-   [x] Dato
-   [x] Grimstad-vær
-   [x] Celsius
-   [x] km/h
-   [x] Europe/Oslo
-   [x] Månefase
-   [x] AAPL
-   [x] GitHub-statistikk
-   [x] USB-C-only

### Mulige fremtidige forbedringer

-   [ ] Egen `secrets.h` som ikke committes
-   [ ] Mer minimalistisk FACE-side
-   [ ] Flere ansiktsuttrykk
-   [ ] Bedre offline-visning
-   [ ] Flere værdata
-   [ ] Konfigurerbar side-rekkefølge
-   [ ] Lavere flash-bruk

------------------------------------------------------------------------

## Lisens og videre bruk

Dette README-et beskriver prosjektoppsettet og den tilpassede
DeskBuddy-sketchens funksjoner.

Hvis prosjektet publiseres offentlig, bør du også kontrollere lisensene
til Arduino-bibliotekene og eventuell original prosjektkode som brukes
som utgangspunkt.