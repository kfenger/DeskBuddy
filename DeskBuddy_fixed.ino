// ============================================================
// DeskBuddy — Waveshare ESP32-C6-Touch-LCD-1.47
// Features: animated face, clock, date, weather, moon,
//           stock ticker, GitHub stats.  Touch swipe to
//           change pages; tilt via QMI8658 IMU animates eyes.
// ============================================================
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

// ── Local configuration ─────────────────────────────────────
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#define GITHUB_USER   ""



// Wi-Fi and the optional public GitHub name live in include/secrets.h.
// Leaving WIFI_SSID or GITHUB_USER blank leaves those online pages in setup mode.
// These values are intentionally local to this workspace and are not shown on-screen.
// The LiPo uses the board's dedicated battery connector, not a GPIO, so it has no firmware pin definition.

// USB-C power only is fully supported. The LiPo battery is optional;
// it is connected through the board's dedicated battery connector.
// No firmware setting is required for USB-C-only operation.

// ── Pin definitions ────────────────────────────────────────
#define LCD_BL    23
#define LCD_DC    15
#define LCD_CS    14
#define LCD_SCK    1
#define LCD_MOSI   2
#define LCD_RST   22

#define TOUCH_SDA 18
#define TOUCH_SCL 19
#define TOUCH_RST 20
#define TOUCH_INT 21


struct AccelData {
  float accelX;
  float accelY;
  float accelZ;
  uint32_t timestamp;
};

struct GyroData {
  float gyroX;
  float gyroY;
  float gyroZ;
  uint32_t timestamp;
};

struct calData {
  bool valid;
  float accelBias[3];
  float gyroBias[3];
};

struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

struct touch_data_t {
  uint8_t    count;
  TouchPoint coords[1];  // first active touch point used by this UI
};


// Forward declarations
void resetSharedI2CBus();
void bsp_touch_init(TwoWire *wire, uint8_t rstPin, uint8_t intPin, uint8_t rotation, uint16_t dispW, uint16_t dispH);
void bsp_touch_read();
uint16_t clampTouchCoord(int32_t value, uint16_t maxValue);
uint16_t scaleTouchAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, uint16_t outMax);
bool bsp_touch_get_coordinates(uint16_t *outX, uint16_t *outY);
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
float clampFloat(float v, float lo, float hi);
void lcdRegInit();
uint32_t compileTimeSeconds();
uint8_t compileMonthNumber();
int32_t daysFromCivil(int32_t y, uint8_t mo, uint8_t d);
void civilFromDays(int32_t z, int32_t *year, uint8_t *month, uint8_t *day);
int32_t compileDateDays();
String csvField(const String &row, uint8_t index);
void centeredText(const char *text, int y, uint8_t size);
void drawPageDots();
void drawHeader(const char *title);
void switchApp(int8_t delta);
bool wifiConfigured();
bool githubConfigured();
bool ensureWifi();
void drawWeatherIcon(int cx, int cy, int code, bool isDay);
bool fetchWeather();
bool fetchStock();
bool fetchGithub();
void drawEye(int cx, int cy, int w, int h, bool closed, int px, int py);
void drawMouth(int cx, int cy);
void drawFace(float tx, float ty);
void drawDigitSegment(int x, int y, int w, int h, int t, uint8_t seg);
void drawDigit(int x, int y, uint8_t digit);
void drawClock();
void drawDatePage();
void drawWeather();
void drawMoonDisc(int cx, int cy, int radius, float phase);
void drawMoon();
void drawStock();
void drawGithub();
void triggerFaceTap();
void readSensors();
void readTouch();
void updateFaceTimers();
void updateNetworkPages();
void calibrateNeutral();

uint32_t lastI2CResetMs = 0;

void resetSharedI2CBus() {
  // Touch and IMU share one I2C controller.  Do not call Wire.end()/begin()
  // from a failed transaction: on ESP32-C6's I2C driver that can leave a
  // transaction in an invalid state and creates the error we are recovering from.
  // Both readers simply skip this sample and retry on the already-configured bus.
  uint32_t now = millis();
  if (now - lastI2CResetMs < 250) return;
  lastI2CResetMs = now;
  Wire.setClock(100000);
}

#define IMU_ADDRESS 0x6B








class QMI8658Mini {
 public:
  int init(calData cal, uint8_t address = IMU_ADDRESS) {
    imuAddress = address;
    calibration = cal;
    if (read8(0x00) != 0x05) return -1;  // WHO_AM_I
    write8(0x60, 0xFF);                  // soft reset
    delay(100);
    write8(0x02, 0x40);                  // CTRL1: auto-increment
    setAccelRange(4);
    setGyroRange(512);
    write8(0x06, 0x03);                  // CTRL5: accel/gyro low-pass defaults
    write8(0x08, 0x03);                  // CTRL7: enable accel + gyro
    delay(100);
    return 0;
  }

  int setAccelRange(int range) {
    uint8_t config = 0x10;
    if (range == 2) { accelScale = 2.0f / 32768.0f; config = 0x00; }
    else if (range == 4) { accelScale = 4.0f / 32768.0f; config = 0x10; }
    else if (range == 8) { accelScale = 8.0f / 32768.0f; config = 0x20; }
    else if (range == 16) { accelScale = 16.0f / 32768.0f; config = 0x30; }
    else return -1;
    write8(0x08, 0x00);
    rmw8(0x03, 0x70, config);            // CTRL2 accel range bits
    write8(0x08, 0x03);
    return 0;
  }

  int setGyroRange(int range) {
    uint8_t config = 0x50;
    if (range == 128 || range == 125) { gyroScale = 128.0f / 32768.0f; config = 0x30; }
    else if (range == 256 || range == 250) { gyroScale = 256.0f / 32768.0f; config = 0x40; }
    else if (range == 512 || range == 500) { gyroScale = 512.0f / 32768.0f; config = 0x50; }
    else if (range == 1024 || range == 1000) { gyroScale = 1024.0f / 32768.0f; config = 0x60; }
    else if (range == 2048 || range == 2000) { gyroScale = 2048.0f / 32768.0f; config = 0x70; }
    else return -1;
    write8(0x08, 0x00);
    rmw8(0x04, 0x70, config);            // CTRL3 gyro range bits
    write8(0x08, 0x03);
    return 0;
  }

  void update() {
    uint8_t status = read8(0x2E);        // STATUS0: accel/gyro ready bits
    if ((status & 0x03) == 0) return;
    uint8_t raw[12] = {0};
    if (!readBytes(0x35, raw, sizeof(raw))) return;

    int16_t ax = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ay = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t az = (int16_t)((raw[5] << 8) | raw[4]);
    int16_t gx = (int16_t)((raw[7] << 8) | raw[6]);
    int16_t gy = (int16_t)((raw[9] << 8) | raw[8]);
    int16_t gz = (int16_t)((raw[11] << 8) | raw[10]);
    uint32_t now = micros();

    accel.accelX = ax * accelScale - calibration.accelBias[0];
    accel.accelY = ay * accelScale - calibration.accelBias[1];
    accel.accelZ = az * accelScale - calibration.accelBias[2];
    accel.timestamp = now;
    gyro.gyroX = gx * gyroScale - calibration.gyroBias[0];
    gyro.gyroY = gy * gyroScale - calibration.gyroBias[1];
    gyro.gyroZ = gz * gyroScale - calibration.gyroBias[2];
    gyro.timestamp = now;
  }

  void getAccel(AccelData *out) { *out = accel; }
  void getGyro(GyroData *out) { *out = gyro; }

 private:
  uint8_t imuAddress = IMU_ADDRESS;
  float accelScale = 4.0f / 32768.0f;
  float gyroScale = 512.0f / 32768.0f;
  calData calibration = {0};
  AccelData accel = {0};
  GyroData gyro = {0};

  uint8_t read8(uint8_t reg) {
    uint8_t value = 0;
    readBytes(reg, &value, 1);
    return value;
  }

  bool readBytes(uint8_t reg, uint8_t *buffer, uint8_t len) {
    Wire.beginTransmission(imuAddress);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) {
      Serial.println("IMU register select failed");
      resetSharedI2CBus();
      return false;
    }
    delayMicroseconds(300);
    if (Wire.requestFrom((uint8_t)imuAddress, len, (uint8_t)true) != len) {
      Serial.println("IMU sample read failed");
      resetSharedI2CBus();
      return false;
    }
    for (uint8_t i = 0; i < len; i++) buffer[i] = Wire.read();
    return true;
  }

  void write8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(imuAddress);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  void rmw8(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t current = read8(reg);
    write8(reg, (current & ~mask) | (value & mask));
  }
};

// ── AXS5106L inline touch reader ──────────────────────────
// The AXS5106L is the capacitive touch controller on the
// Waveshare ESP32-C6-Touch-LCD-1.47. ESP-IDF components exist,
// but this Arduino starter inlines a small polling reader so it
// does not need the ESP-IDF/LVGL touch stack.
// Protocol: I2C @ 100 kHz, 7-bit device address 0x63.
// Touch data begin at register 0x01: touch count followed by the first
// six-byte point record. This sketch uses the first active point for tap/swipe navigation.

#define AXS5106L_ADDR 0x63
#define AXS5106L_TOUCH_DATA_REG 0x01





static TwoWire *_touchWire = nullptr;
static uint8_t  _touchRst  = 255;
static uint8_t  _touchInt  = 255;
static uint16_t _touchW    = 320;
static uint16_t _touchH    = 172;
static uint8_t  _touchRot  = 0;

void bsp_touch_init(TwoWire *wire, uint8_t rstPin, uint8_t intPin,
                    uint8_t rotation, uint16_t dispW, uint16_t dispH) {
  _touchWire = wire;
  _touchRst  = rstPin;
  _touchInt  = intPin;
  _touchRot  = rotation;
  _touchW    = dispW;
  _touchH    = dispH;

  if (_touchRst != 255) {
    pinMode(_touchRst, OUTPUT);
    digitalWrite(_touchRst, LOW);
    delay(20);
    digitalWrite(_touchRst, HIGH);
    delay(50);
  }
  if (_touchInt != 255) {
    pinMode(_touchInt, INPUT_PULLUP);
  }
}

// bsp_touch_read — no-op for polling mode; INT pin can be
// checked externally if needed.
void bsp_touch_read() {}

uint16_t clampTouchCoord(int32_t value, uint16_t maxValue) {
  if (value < 0) return 0;
  if (value >= maxValue) return maxValue - 1;
  return (uint16_t)value;
}

uint16_t scaleTouchAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, uint16_t outMax) {
  if (rawMax <= rawMin || outMax == 0) return 0;
  if (raw <= rawMin) return 0;
  if (raw >= rawMax) return outMax - 1;
  return (uint32_t)(raw - rawMin) * (outMax - 1) / (rawMax - rawMin);
}

// Returns true if at least one touch point is active.
bool bsp_touch_get_coordinates(uint16_t *outX, uint16_t *outY) {
  if (!_touchWire || !outX || !outY) return false;

  // AXS5106L packet: gesture, touch count, X high/low, Y high/low.
  _touchWire->beginTransmission(AXS5106L_ADDR);
  _touchWire->write(AXS5106L_TOUCH_DATA_REG);
  if (_touchWire->endTransmission(true) != 0) {
    Serial.println("Touch register select failed");
    resetSharedI2CBus();
    return false;
  }

  delayMicroseconds(300);

  uint8_t len = _touchWire->requestFrom((uint8_t)AXS5106L_ADDR, (uint8_t)6, (uint8_t)true);
  if (len < 6) {
    Serial.println("Touch sample read failed");
    resetSharedI2CBus();
    return false;
  }

  uint8_t buf[6];
  for (uint8_t i = 0; i < 6; i++) buf[i] = _touchWire->read();

  uint8_t nPoints = buf[1];
  if (nPoints == 0 || nPoints > 2) return false;

  uint16_t rawX = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
  uint16_t rawY = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];
  if ((rawX == 0x0FFF && rawY == 0x0FFF) || rawX > 4090 || rawY > 4090) return false;

  // Small edge dead-zone compensation. The controller reports raw axes with a
  // few pixels of slack at the extremes; scaling them to the active screen area
  // makes edge swipes less sticky while preserving the current orientation.
  // Landscape rotation 1: touch axes are swapped to match the 320x172 display.
  const uint16_t rawXMax = 319;
  const uint16_t rawYMax = 171;
  uint16_t mappedX = rawX;
  uint16_t mappedY = rawY;

  switch (_touchRot) {
    case 1:
      mappedX = scaleTouchAxis(rawY, 0, rawYMax, _touchW);
      mappedY = scaleTouchAxis(rawX, 0, rawXMax, _touchH);
      break;
    case 2:
      mappedX = _touchW - 1 - scaleTouchAxis(rawX, 0, rawXMax, _touchW);
      mappedY = _touchH - 1 - scaleTouchAxis(rawY, 0, rawYMax, _touchH);
      break;
    case 3:
      mappedX = _touchW - 1 - scaleTouchAxis(rawY, 0, rawYMax, _touchW);
      mappedY = scaleTouchAxis(rawX, 0, rawXMax, _touchH);
      break;
    default:
      mappedX = scaleTouchAxis(rawX, 0, rawXMax, _touchW);
      mappedY = scaleTouchAxis(rawY, 0, rawYMax, _touchH);
      break;
  }

  *outX = clampTouchCoord(mappedX, _touchW);
  *outY = clampTouchCoord(mappedY, _touchH);
  return true;
}
// ── End AXS5106L driver ────────────────────────────────────

static const int SCREEN_W         = 320;
static const int SCREEN_H         = 172;
static const uint8_t APP_COUNT    = 7;
static const uint8_t FACE_MOOD_COUNT = 5;
static const uint16_t FG = RGB565_WHITE;
static const uint16_t BG = RGB565_BLACK;
static const uint8_t ROTATION = 1;

Arduino_DataBus *bus     = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX    *display  = new Arduino_ST7789(bus, LCD_RST, 0, false, 172, 320, 34, 0, 34, 0);
Arduino_Canvas *gfx      = new Arduino_Canvas(SCREEN_W, SCREEN_H, display);

QMI8658Mini imu;
calData calib = {0};
AccelData accel;
GyroData gyro;

bool     imuReady        = false;
bool     touchReady      = false;
bool     touchWasDown    = false;
bool     wifiAttempted   = false;
bool     weatherValid    = false;
bool     stockValid      = false;
bool     githubValid     = false;
uint8_t  currentApp      = 0;
uint8_t  faceMood        = 0;
uint16_t touchStartX     = 0;
uint16_t touchStartY     = 0;
uint16_t touchLastX      = 0;
uint16_t touchLastY      = 0;
uint32_t touchStartMs    = 0;
uint8_t  touchMissFrames  = 0;
bool     touchMoved       = false;
uint32_t nextBlink       = 1400;
uint32_t blinkUntil      = 0;
uint32_t nextGlance      = 900;
uint32_t lastSerialMs    = 0;
uint32_t clockStartMillis   = 0;
uint32_t clockStartSeconds  = 0;
uint32_t weatherUpdatedAt   = 0;
uint32_t stockUpdatedAt     = 0;
uint32_t githubUpdatedAt    = 0;
float restAx    = 0.0f;
float restAy    = 0.0f;
float filteredAx = 0.0f;
float filteredAy = 0.0f;
float filteredGz = 0.0f;
float faceGlanceX  = 0.0f;
float faceGlanceY  = 0.0f;
float faceTargetX  = 0.0f;
float faceTargetY  = 0.0f;
float pressPulse   = 0.0f;
int   weatherTempC    = 0;
int   weatherHumidity = 0;
int   weatherWindKmh  = 0;
int   weatherCode     = -1;
bool  weatherIsDay    = true;
String weatherLabel   = "WAITING";
float  stockPrice = 0.0f;
float  stockOpen  = 0.0f;
float  stockHigh  = 0.0f;
float  stockLow   = 0.0f;
String stockTime  = "";
int githubFollowers = 0;
int githubRepos     = 0;

// ── Helpers ────────────────────────────────────────────────

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

float clampFloat(float v, float lo, float hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

// ── LCD init sequence for the AXS15231B panel ─────────────

void lcdRegInit() {
  static const uint8_t ops[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x11,
      END_WRITE,
      DELAY, 120,
      BEGIN_WRITE,
      WRITE_C8_D16, 0xDF, 0x98, 0x53,
      WRITE_C8_D8,  0xB2, 0x23,
      WRITE_COMMAND_8, 0xB7,
      WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
      WRITE_COMMAND_8, 0xBB,
      WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
      WRITE_C8_D16, 0xC0, 0x44, 0xA4,
      WRITE_C8_D8,  0xC1, 0x16,
      WRITE_COMMAND_8, 0xC3,
      WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
      WRITE_COMMAND_8, 0xC4,
      WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
      WRITE_COMMAND_8, 0xC8,
      WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      WRITE_COMMAND_8, 0xD0,
      WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
      WRITE_C8_D16, 0xD7, 0x00, 0x30,
      WRITE_C8_D8,  0xE6, 0x14,
      WRITE_C8_D8,  0xDE, 0x01,
      WRITE_COMMAND_8, 0xB7,
      WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
      WRITE_COMMAND_8, 0xC1,
      WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
      WRITE_C8_D16, 0xC2, 0x06, 0x3A,
      WRITE_C8_D16, 0xC4, 0x72, 0x12,
      WRITE_C8_D8,  0xBE, 0x00,
      WRITE_C8_D8,  0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3, 0x01, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x35, 0x00,
      WRITE_C8_D8, 0x3A, 0x05,
      WRITE_COMMAND_8, 0x2A,
      WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
      WRITE_COMMAND_8, 0x2B,
      WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
      WRITE_C8_D8, 0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x36, 0x00,
      WRITE_COMMAND_8, 0x21,
      END_WRITE,
      DELAY, 10,
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x29,
      END_WRITE};
  bus->batchOperation(ops, sizeof(ops));
}

// ── Compile-time clock seed ────────────────────────────────

uint32_t compileTimeSeconds() {
  const char *t = __TIME__;
  uint8_t hh = (t[0]-'0')*10 + (t[1]-'0');
  uint8_t mm = (t[3]-'0')*10 + (t[4]-'0');
  uint8_t ss = (t[6]-'0')*10 + (t[7]-'0');
  return (uint32_t)hh*3600UL + (uint32_t)mm*60UL + ss;
}

uint8_t compileMonthNumber() {
  const char *m = __DATE__;
  static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (uint8_t i = 0; i < 12; i++)
    if (strncmp(m, names+i*3, 3) == 0) return i+1;
  return 1;
}

int32_t daysFromCivil(int32_t y, uint8_t mo, uint8_t d) {
  y -= mo <= 2;
  const int32_t era = (y >= 0 ? y : y-399)/400;
  const uint32_t yoe = (uint32_t)(y - era*400);
  const uint32_t doy = (153*(mo+(mo>2?-3:9))+2)/5 + d - 1;
  const uint32_t doe = yoe*365 + yoe/4 - yoe/100 + doy;
  return era*146097 + (int32_t)doe - 719468;
}

void civilFromDays(int32_t z, int32_t *year, uint8_t *month, uint8_t *day) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z-146096)/146097;
  const uint32_t doe = (uint32_t)(z - era*146097);
  const uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096)/365;
  int32_t y = (int32_t)yoe + era*400;
  const uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);
  const uint32_t mp  = (5*doy+2)/153;
  const uint32_t d   = doy - (153*mp+2)/5 + 1;
  const uint32_t mo  = mp + (mp < 10 ? 3 : -9);
  y += mo <= 2;
  *year  = y;
  *month = (uint8_t)mo;
  *day   = (uint8_t)d;
}

int32_t compileDateDays() {
  const char *d = __DATE__;
  uint8_t day = (d[4]==' ' ? 0 : d[4]-'0')*10 + (d[5]-'0');
  int32_t y = (int32_t)(d[7]-'0')*1000 + (int32_t)(d[8]-'0')*100 +
              (int32_t)(d[9]-'0')*10   + (d[10]-'0');
  return daysFromCivil(y, compileMonthNumber(), day);
}

// ── CSV helper ────────────────────────────────────────────

String csvField(const String &row, uint8_t index) {
  int start = 0;
  for (uint8_t i = 0; i < index; i++) {
    start = row.indexOf(',', start);
    if (start < 0) return "";
    start++;
  }
  int end = row.indexOf(',', start);
  if (end < 0) end = row.length();
  String v = row.substring(start, end);
  v.trim();
  return v;
}

// ── Drawing primitives ────────────────────────────────────

void centeredText(const char *text, int y, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(FG);
  int width = (int)strlen(text)*6*size;
  gfx->setCursor((SCREEN_W-width)/2, y);
  gfx->print(text);
}

void drawPageDots() {
  int startX = SCREEN_W/2 - ((APP_COUNT-1)*16)/2;
  for (uint8_t i = 0; i < APP_COUNT; i++) {
    if (i == currentApp)
      gfx->fillCircle(startX+i*16, SCREEN_H-12, 3, FG);
    else
      gfx->drawCircle(startX+i*16, SCREEN_H-12, 2, rgb(90,90,90));
  }
}

void drawHeader(const char *title) {
  gfx->fillScreen(BG);
  gfx->drawLine(0, 20, SCREEN_W, 20, FG);
  gfx->setTextSize(1);
  gfx->setTextColor(FG);
  gfx->setCursor(8, 7);
  gfx->print(title);
}

// ── App navigation ────────────────────────────────────────

void switchApp(int8_t delta) {
  currentApp = (currentApp + APP_COUNT + delta) % APP_COUNT;
  pressPulse = 1.0f;
  Serial.print("Page changed to ");
  Serial.println(currentApp);
}

// ── Wi-Fi ─────────────────────────────────────────────────

bool wifiConfigured() { return strlen(WIFI_SSID) > 0; }
bool githubConfigured() { return strlen(GITHUB_USER) > 0; }

bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!wifiConfigured() || wifiAttempted) return false;
  wifiAttempted = true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-start < 3500UL) delay(120);
  return WiFi.status() == WL_CONNECTED;
}

// ── Weather fetch (Open-Meteo, Grimstad) ───────────────

const char *weatherCodeText(int code) {
  if (code == 0)                             return "CLEAR";
  if (code == 1 || code == 2)               return "PARTLY CLOUDY";
  if (code == 3)                             return "CLOUDY";
  if (code == 45 || code == 48)             return "FOG";
  if ((code>=51&&code<=67)||(code>=80&&code<=82)) return "RAIN";
  if (code >= 71 && code <= 77)             return "SNOW";
  if (code >= 95)                            return "STORM";
  return "WEATHER";
}

void drawWeatherIcon(int cx, int cy, int code, bool isDay) {
  if (code == 0) {
    gfx->drawCircle(cx, cy, 22, FG);
    for (uint8_t i = 0; i < 8; i++) {
      float a = i*0.7854f;
      gfx->drawLine(cx+(int)(cos(a)*30), cy+(int)(sin(a)*30),
                    cx+(int)(cos(a)*40), cy+(int)(sin(a)*40), FG);
    }
    if (!isDay) gfx->fillCircle(cx+11, cy-8, 18, BG);
    return;
  }
  gfx->fillCircle(cx-19, cy+5, 19, FG);
  gfx->fillCircle(cx+2,  cy-6, 25, FG);
  gfx->fillCircle(cx+27, cy+8, 17, FG);
  gfx->fillRoundRect(cx-42, cy+8, 87, 25, 12, FG);
  if ((code>=51&&code<=67)||(code>=80&&code<=82)) {
    for (int x=-25; x<=25; x+=17) {
      gfx->drawLine(cx+x, cy+45, cx+x-8, cy+62, FG);
      gfx->drawLine(cx+x+1, cy+45, cx+x-7, cy+62, FG);
    }
  } else if (code>=71 && code<=77) {
    for (int x=-24; x<=24; x+=24) {
      gfx->drawLine(cx+x-6, cy+53, cx+x+6, cy+53, FG);
      gfx->drawLine(cx+x, cy+47, cx+x, cy+59, FG);
      gfx->drawLine(cx+x-5, cy+48, cx+x+5, cy+58, FG);
      gfx->drawLine(cx+x+5, cy+48, cx+x-5, cy+58, FG);
    }
  }
}

bool fetchWeather() {
  if (!ensureWifi()) return false;
  HTTPClient http;
  http.setTimeout(6000);
  if (!http.begin(
        "http://api.open-meteo.com/v1/forecast?"
        "latitude=58.3405&longitude=8.5934"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
        "&temperature_unit=celsius&wind_speed_unit=kmh&timezone=Europe%2FOslo"))
    return false;
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  weatherTempC     = doc["current"]["temperature_2m"].as<float>();
  weatherHumidity  = doc["current"]["relative_humidity_2m"].as<int>();
  weatherWindKmh   = (int)round(doc["current"]["wind_speed_10m"].as<float>());
  weatherCode      = doc["current"]["weather_code"].as<int>();
  weatherIsDay     = doc["current"]["is_day"].as<int>() != 0;
  weatherLabel     = weatherCodeText(weatherCode);
  weatherUpdatedAt = millis();
  weatherValid     = true;
  return true;
}

// ── Stock fetch (stooq CSV, AAPL) ────────────────────────

bool fetchStock() {
  if (!ensureWifi()) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(7000);
  if (!http.begin(client, "https://stooq.com/q/l/?s=aapl.us&f=sd2t2ohlcv&h&e=csv"))
    return false;
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  String csv = http.getString();
  http.end();
  int rowStart = csv.indexOf('\n');
  if (rowStart < 0) return false;
  String row = csv.substring(rowStart+1);
  row.trim();
  String closeText = csvField(row, 6);
  if (closeText.length()==0 || closeText=="N/D") return false;
  stockTime  = csvField(row, 2);
  stockOpen  = csvField(row, 3).toFloat();
  stockHigh  = csvField(row, 4).toFloat();
  stockLow   = csvField(row, 5).toFloat();
  stockPrice = closeText.toFloat();
  stockUpdatedAt = millis();
  stockValid = true;
  return true;
}

// ── GitHub fetch ──────────────────────────────────────────

bool fetchGithub() {
  if (!githubConfigured()) return false;
  if (!ensureWifi()) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(7000);
  String url = String("https://api.github.com/users/") + GITHUB_USER;
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "ESP32-C6-Touch-LCD");
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  githubFollowers  = doc["followers"].as<int>();
  githubRepos      = doc["public_repos"].as<int>();
  githubUpdatedAt  = millis();
  githubValid      = true;
  return true;
}

// ── Face rendering ────────────────────────────────────────

void drawEye(int cx, int cy, int w, int h, bool closed, int px, int py) {
  if (closed) {
    gfx->fillRoundRect(cx-w/2, cy-3, w, 6, 3, FG);
    return;
  }
  gfx->fillRoundRect(cx-w/2, cy-h/2, w, h, h/2, FG);
  gfx->fillRoundRect(cx-7+px, cy-9+py, 14, 18, 7, BG);
}

void drawMouth(int cx, int cy) {
  if (faceMood == 2) {
    gfx->fillEllipse(cx, cy+2, 15, 21, FG);
    gfx->fillEllipse(cx, cy+2, 7, 11, BG);
  } else if (faceMood == 3) {
    gfx->fillRoundRect(cx-38, cy, 76, 6, 3, FG);
  } else if (faceMood == 4) {
    gfx->drawLine(cx-28, cy+8, cx+28, cy-8, FG);
    gfx->drawLine(cx-28, cy+9, cx+28, cy-7, FG);
  } else {
    int radius = (faceMood == 1) ? 48 : 40;
    gfx->fillArc(cx, cy-16, radius, radius-5, 34.0f, 146.0f, FG);
  }
}

void drawFace(float tx, float ty) {
  uint32_t now = millis();
  drawHeader("FACE");

  float breathe = sin(now*0.0021f)*0.03f + pressPulse*0.08f;

  int dx = (int)(tx*14.0f + faceGlanceX);
  int dy = (int)(ty*8.0f  + faceGlanceY);

  int eyeW = 44 + (int)(breathe*28.0f);
  int eyeH = (faceMood==2) ? 45 : (faceMood==3) ? 16 : (62 + (int)(breathe*18.0f));

  bool blink = now < blinkUntil;

  drawEye(114+dx, 75+dy, eyeW, eyeH, blink||faceMood==3,
          dx/4, dy/5);

  drawEye(206+dx, 75+dy, eyeW, eyeH, blink||faceMood==3||faceMood==4,
          dx/4, dy/5);

  drawMouth(160+dx/4, 116+dy/4);

  drawPageDots();
}

// ── 7-segment clock ───────────────────────────────────────

void drawDigitSegment(int x, int y, int w, int h, int t, uint8_t seg) {
  int half = h/2, r = t/2;
  switch (seg) {
    case 0: gfx->fillRoundRect(x+t, y, w-2*t, t, r, FG); break;
    case 1: gfx->fillRoundRect(x+w-t, y+t, t, half-t, r, FG); break;
    case 2: gfx->fillRoundRect(x+w-t, y+half, t, half-t, r, FG); break;
    case 3: gfx->fillRoundRect(x+t, y+h-t, w-2*t, t, r, FG); break;
    case 4: gfx->fillRoundRect(x, y+half, t, half-t, r, FG); break;
    case 5: gfx->fillRoundRect(x, y+t, t, half-t, r, FG); break;
    case 6: gfx->fillRoundRect(x+t, y+half-t/2, w-2*t, t, r, FG); break;
  }
}

void drawDigit(int x, int y, uint8_t digit) {
  static const uint8_t masks[10] = {
    0b00111111,0b00000110,0b01011011,0b01001111,0b01100110,
    0b01101101,0b01111101,0b00000111,0b01111111,0b01101111};
  for (uint8_t seg = 0; seg < 7; seg++)
    if (masks[digit%10] & (1<<seg)) drawDigitSegment(x, y, 42, 76, 8, seg);
}

void drawClock() {
  uint32_t elapsed = (millis()-clockStartMillis)/1000UL;
  uint32_t sod     = (clockStartSeconds+elapsed)%86400UL;
  uint8_t hh = sod/3600UL, mm = (sod/60UL)%60UL, ss = sod%60UL;
  drawHeader("TIME");
  drawDigit(43,  46, hh/10);
  drawDigit(93,  46, hh%10);
  if ((ss%2)==0) {
    gfx->fillRoundRect(141, 68, 8, 8, 4, FG);
    gfx->fillRoundRect(141, 96, 8, 8, 4, FG);
  }
  drawDigit(159, 46, mm/10);
  drawDigit(209, 46, mm%10);
  gfx->setTextSize(2); gfx->setTextColor(FG);
  gfx->setCursor(268, 101);
  if (ss < 10) gfx->print("0");
  gfx->print(ss);
  drawPageDots();
}

// ── Date page ─────────────────────────────────────────────

void drawDatePage() {
  static const char *wd[]  = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"};
  static const char *mon[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  uint32_t elapsedSec = (millis()-clockStartMillis)/1000UL;
  int32_t days = compileDateDays() + (int32_t)((clockStartSeconds+elapsedSec)/86400UL);
  int32_t year; uint8_t month, day;
  civilFromDays(days, &year, &month, &day);
  uint8_t weekday = (uint8_t)((days+4)%7);
  drawHeader("DATE");
  centeredText(wd[weekday], 35, 3);
  char line[24];
  snprintf(line, sizeof(line), "%s %02u", mon[month-1], day);
  centeredText(line, 82, 5);
  snprintf(line, sizeof(line), "%ld", (long)year);
  centeredText(line, 130, 2);
  drawPageDots();
}

// ── Weather page ──────────────────────────────────────────

void drawWeather() {
  drawHeader("GRIMSTAD");
  if (!wifiConfigured()) {
    centeredText("NO WIFI CONFIG", 70, 2);
    centeredText("EDIT CONFIG", 102, 1);
    drawPageDots(); return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    centeredText("CONNECTING", 75, 2);
    drawWeatherIcon(250, 82, 3, true);
    drawPageDots(); return;
  }
  if (!weatherValid) { centeredText("UPDATING", 76, 2); drawPageDots(); return; }
  drawWeatherIcon(241, 70, weatherCode, weatherIsDay);
  gfx->setTextSize(7); gfx->setTextColor(FG);
  gfx->setCursor(20, 60); gfx->print(weatherTempC);
  gfx->setTextSize(3); gfx->print("C");
  gfx->setTextSize(1);
  gfx->setCursor(24,  136); gfx->print(weatherLabel);
  gfx->setCursor(146, 136); gfx->print("H "); gfx->print(weatherHumidity); gfx->print("%");
  gfx->setCursor(214, 136); gfx->print("W "); gfx->print(weatherWindKmh); gfx->print("KM/H");
  drawPageDots();
}

// ── Moon phase page ───────────────────────────────────────

const char *moonPhaseLabel(float phase) {
  if (phase<0.03f||phase>0.97f) return "NEW MOON";
  if (phase<0.22f)              return "WAXING CRESCENT";
  if (phase<0.28f)              return "FIRST QUARTER";
  if (phase<0.47f)              return "WAXING GIBBOUS";
  if (phase<0.53f)              return "FULL MOON";
  if (phase<0.72f)              return "WANING GIBBOUS";
  if (phase<0.78f)              return "LAST QUARTER";
  return "WANING CRESCENT";
}

void drawMoonDisc(int cx, int cy, int radius, float phase) {
  phase = phase - floor(phase);
  gfx->drawCircle(cx, cy, radius+3, rgb(72,72,72));
  gfx->fillCircle(cx, cy, radius, FG);
  if (phase<0.03f||phase>0.97f) {
    gfx->fillCircle(cx, cy, radius-2, BG);
    gfx->drawCircle(cx, cy, radius, FG);
    return;
  }
  if (phase>0.47f && phase<0.53f) return;
  int shadowX = (phase < 0.5f)
    ? cx - (int)(4.0f*radius*phase)
    : cx + (int)(2.0f*radius - 4.0f*radius*(phase-0.5f));
  gfx->fillCircle(shadowX, cy, radius, BG);
  gfx->drawCircle(cx, cy, radius, FG);
}

void drawMoon() {
  const float syn = 29.53058867f;
  uint32_t elapsed = (millis()-clockStartMillis)/1000UL;
  float days = (float)compileDateDays() + ((float)compileTimeSeconds()+(float)elapsed)/86400.0f;
  float age  = fmod(days-10962.7597f, syn);
  if (age < 0.0f) age += syn;
  float phase       = age/syn;
  int   illumination = (int)round((1.0f-cos(phase*6.2831853f))*50.0f);
  drawHeader("MOON");
  drawMoonDisc(232, 82, 45, phase);
  gfx->setTextSize(2); gfx->setTextColor(FG);
  gfx->setCursor(24, 58);  gfx->print(moonPhaseLabel(phase));
  gfx->setTextSize(1);
  gfx->setCursor(26, 98);  gfx->print("AGE "); gfx->print(age, 1); gfx->print(" DAYS");
  gfx->setCursor(26, 118); gfx->print("LIGHT "); gfx->print(illumination); gfx->print("%");
  drawPageDots();
}

// ── Stock page ────────────────────────────────────────────

void drawStock() {
  drawHeader("AAPL");
  if (!wifiConfigured()) {
    centeredText("NO WIFI CONFIG", 70, 2);
    centeredText("EDIT CONFIG", 102, 1);
    drawPageDots(); return;
  }
  if (WiFi.status() != WL_CONNECTED) { centeredText("CONNECTING",75,2); drawPageDots(); return; }
  if (!stockValid)                    { centeredText("UPDATING",  76,2); drawPageDots(); return; }
  gfx->setTextSize(6); gfx->setTextColor(FG);
  gfx->setCursor(18, 58); gfx->print("$"); gfx->print(stockPrice, 2);
  gfx->setTextSize(1);
  gfx->setCursor(24,  132); gfx->print("O "); gfx->print(stockOpen, 2);
  gfx->setCursor(105, 132); gfx->print("H "); gfx->print(stockHigh, 2);
  gfx->setCursor(186, 132); gfx->print("L "); gfx->print(stockLow, 2);
  gfx->setCursor(256,  18); gfx->print(stockTime);
  drawPageDots();
}

// ── GitHub page ───────────────────────────────────────────

void drawGithub() {
  drawHeader("GITHUB");
  if (!wifiConfigured() || !githubConfigured()) {
    centeredText("SETUP REQUIRED", 68, 2);
    centeredText("EDIT CONFIG", 102, 1);
    drawPageDots(); return;
  }
  if (WiFi.status() != WL_CONNECTED) { centeredText("CONNECTING",75,2); drawPageDots(); return; }
  if (!githubValid)                   { centeredText("UPDATING",  76,2); drawPageDots(); return; }
  gfx->setTextSize(6); gfx->setTextColor(FG);
  gfx->setCursor(22, 56);  gfx->print(githubFollowers);
  gfx->setTextSize(2);
  gfx->setCursor(24, 118); gfx->print("FOLLOWERS");
  gfx->setTextSize(1);
  gfx->setCursor(218, 20);  gfx->print("@"); gfx->print(GITHUB_USER);
  gfx->setCursor(222, 132); gfx->print("REPOS "); gfx->print(githubRepos);
  drawPageDots();
}

// ── Interaction handlers ──────────────────────────────────

void triggerFaceTap() {
  if (currentApp == 0) {
    faceMood = (faceMood+1) % FACE_MOOD_COUNT;
    pressPulse = 1.0f;
    Serial.print("Face mood changed to ");
    Serial.println(faceMood);
  } else {
    switchApp(1);
  }
}

void readSensors() {
  if (!imuReady) {
    // Animate eyes sinusoidally when IMU is absent
    filteredAx = sin(millis()*0.0012f)*0.12f;
    filteredAy = cos(millis()*0.0010f)*0.12f;
    return;
  }
  imu.update();
  imu.getAccel(&accel);
  imu.getGyro(&gyro);
  filteredAx = filteredAx*0.88f + accel.accelX*0.12f;
  filteredAy = filteredAy*0.88f + accel.accelY*0.12f;
  filteredGz = filteredGz*0.82f + gyro.gyroZ*0.18f;
  // Fast spin → surprised face
  if (fabs(filteredGz) > 130.0f) { faceMood = 2; pressPulse = 1.0f; }
}

void readTouch() {
  if (!touchReady) return;
  uint16_t x = 0, y = 0;
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&x, &y)) {
    uint32_t now = millis();
    touchLastX = x; touchLastY = y;
    touchMissFrames = 0;
    if (!touchWasDown) {
      touchStartX = x; touchStartY = y; touchStartMs = now;
      touchMoved = false;
      touchWasDown = true;
      return;
    }
    int16_t dx = (int16_t)x-(int16_t)touchStartX;
    int16_t dy = (int16_t)y-(int16_t)touchStartY;
    if (abs(dx) > 12 || abs(dy) > 12) touchMoved = true;
    if (abs(dx) > 55 && abs(dx) > abs(dy)+18) {
      switchApp(dx < 0 ? 1 : -1);
      touchWasDown = false;
      touchMissFrames = 0;
      touchMoved = false;
    }
  } else if (touchWasDown) {
    // The AXS5106L INT/read path can miss the odd frame. Require a few
    // consecutive misses before treating it as release, otherwise taps/swipes
    // get chopped up and feel flaky.
    if (++touchMissFrames < 3) return;
    uint32_t pressMs = millis() - touchStartMs;
    int16_t dx = (int16_t)touchLastX-(int16_t)touchStartX;
    int16_t dy = (int16_t)touchLastY-(int16_t)touchStartY;
    if (pressMs >= 35 && pressMs <= 650 && !touchMoved && abs(dx) < 35 && abs(dy) < 35) {
      triggerFaceTap();
    }
    touchWasDown = false;
    touchMissFrames = 0;
    touchMoved = false;
  }
}

void updateFaceTimers() {
  uint32_t now = millis();
  if (now > nextBlink) {
    blinkUntil = now + (random(0,6)==0 ? 220 : 105);
    nextBlink  = now + 1000 + random(0, 2600);
  }
  if (now > nextGlance) {
    faceTargetX = (float)random(-8, 9);
    faceTargetY = (float)random(-4, 5);
    nextGlance  = now + 650 + random(0, 1500);
  }
  faceGlanceX = faceGlanceX*0.84f + faceTargetX*0.16f;
  faceGlanceY = faceGlanceY*0.84f + faceTargetY*0.16f;
  pressPulse *= 0.86f;
}


void updateNetworkPages() {
  if      (currentApp==3 && (!weatherValid || millis()-weatherUpdatedAt > 15UL*60UL*1000UL)) fetchWeather();
  else if (currentApp==5 && (!stockValid   || millis()-stockUpdatedAt   > 10UL*60UL*1000UL)) fetchStock();
  else if (currentApp==6 && (!githubValid  || millis()-githubUpdatedAt  > 30UL*60UL*1000UL)) fetchGithub();
}

void calibrateNeutral() {
  gfx->fillScreen(BG);
  centeredText("HOLD STILL", 76, 2);
  gfx->flush();
  delay(900);
  for (uint8_t i = 0; i < 100; i++) { imu.update(); delay(5); }
  float sumX=0.0f, sumY=0.0f;
  for (uint8_t i = 0; i < 140; i++) {
    imu.update(); imu.getAccel(&accel);
    sumX += accel.accelX; sumY += accel.accelY;
    delay(5);
  }
  restAx = sumX/140.0f; restAy = sumY/140.0f;
  filteredAx = restAx;  filteredAy = restAy;
}

// ── Arduino entry points ──────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("ESP32-C6 DeskBuddy starting");

  if (!gfx->begin(40000000)) Serial.println("Display init failed — check wiring");
  lcdRegInit();
  display->setRotation(ROTATION);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(BG);
  gfx->flush();

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000);
  bsp_touch_init(&Wire, TOUCH_RST, TOUCH_INT, ROTATION, gfx->width(), gfx->height());
  touchReady = true;
  Serial.println("Touch controller initialised");

  int err = imu.init(calib, IMU_ADDRESS);
  if (err == 0) {
    imuReady = (imu.setAccelRange(4)==0 && imu.setGyroRange(512)==0);
    if (imuReady) {
      Serial.println("IMU initialised");
      calibrateNeutral();
    }
  }
  if (!imuReady) Serial.println("IMU unavailable — using animated fallback motion");

  randomSeed(micros());
  clockStartMillis  = millis();
  clockStartSeconds = compileTimeSeconds();
  nextBlink         = millis() + 1200;
  nextGlance        = millis() + 600;
}

void loop() {
  readSensors();
  readTouch();
  updateFaceTimers();
  updateNetworkPages();

  float tx=0.0f, ty=0.0f;
  if (imuReady) {
    tx = clampFloat(-(filteredAy-restAy)*2.2f, -1.0f, 1.0f);
    ty = clampFloat( (filteredAx-restAx)*2.2f, -1.0f, 1.0f);
  } else {
    tx = sin(millis()*0.0014f)*0.25f;
    ty = cos(millis()*0.0011f)*0.16f;
  }

  switch (currentApp) {
    case 0: drawFace(tx, ty); break;
    case 1: drawClock();      break;
    case 2: drawDatePage();   break;
    case 3: drawWeather();    break;
    case 4: drawMoon();       break;
    case 5: drawStock();      break;
    default: drawGithub();    break;
  }
  gfx->flush();

  if (millis()-lastSerialMs > 1200) {
    lastSerialMs = millis();
    Serial.print("app="); Serial.print(currentApp);
    Serial.print(" mood="); Serial.println(faceMood);
  }
  delay(24);
}