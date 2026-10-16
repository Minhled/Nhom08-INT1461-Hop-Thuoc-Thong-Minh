/*
  De tai: Hop thuoc thong minh nhac uong thuoc - Nhom 8 INT1461
  Nen tang: ESP32 DevKit V1 + DS1307 + LCD1602 I2C + 3 LED + buzzer + 2 nut nhan
  Luu y: lich su chi luu trong RAM, se mat khi reset/mat nguon.
*/
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

constexpr uint8_t PIN_LED_A = 18;
constexpr uint8_t PIN_LED_B = 19;
constexpr uint8_t PIN_LED_C = 23;
constexpr uint8_t PIN_BUZZER = 32;
constexpr uint8_t PIN_BTN_OK = 27;
constexpr uint8_t PIN_BTN_HISTORY = 26;

constexpr uint32_t DEBOUNCE_MS = 35;
constexpr uint32_t BLINK_MS = 350;
constexpr uint32_t ALERT_TIMEOUT_MS = 60000; // 60 giay, co the doi theo yeu cau GV
constexpr uint8_t HISTORY_CAPACITY = 20;

RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

struct Dose {
  const char* name;
  uint8_t hour;
  uint8_t minute;
  uint8_t ledPin;
};

Dose schedule[] = {
  {"Ngan A", 8, 0, PIN_LED_A},
  {"Ngan B", 13, 0, PIN_LED_B},
  {"Ngan C", 20, 0, PIN_LED_C}
};
constexpr size_t DOSE_COUNT = sizeof(schedule) / sizeof(schedule[0]);

struct HistoryRecord {
  char scheduled[20];
  char confirmed[20];
  char compartment[12];
  char status[16];
};
HistoryRecord historyRecords[HISTORY_CAPACITY];
uint8_t historyCount = 0;
uint8_t historyNext = 0;

enum State { IDLE, ALERTING, SHOW_HISTORY };
State state = IDLE;
int activeDose = -1;
int32_t lastTriggeredDate[DOSE_COUNT] = {-1, -1, -1};
uint32_t alertStartedAt = 0;
uint32_t lastBlinkAt = 0;
bool blinkOn = false;
uint32_t lastLcdAt = 0;
uint32_t lastSerialAt = 0;
uint32_t historyViewStartedAt = 0;

struct DebouncedButton {
  uint8_t pin;
  bool stableState;
  bool lastReading;
  uint32_t changedAt;
};
DebouncedButton okButton{PIN_BTN_OK, HIGH, HIGH, 0};
DebouncedButton historyButton{PIN_BTN_HISTORY, HIGH, HIGH, 0};

String twoDigits(uint8_t value) {
  return value < 10 ? "0" + String(value) : String(value);
}
String dateTimeString(const DateTime& t) {
  return String(t.year()) + "-" + twoDigits(t.month()) + "-" + twoDigits(t.day()) +
         " " + twoDigits(t.hour()) + ":" + twoDigits(t.minute()) + ":" + twoDigits(t.second());
}
int32_t dateKey(const DateTime& t) {
  return (int32_t)t.year() * 10000L + (int32_t)t.month() * 100L + t.day();
}
String scheduledString(const Dose& d) {
  return twoDigits(d.hour) + ":" + twoDigits(d.minute);
}
void allAlertsOff() {
  digitalWrite(PIN_LED_A, LOW);
  digitalWrite(PIN_LED_B, LOW);
  digitalWrite(PIN_LED_C, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}
void setDoseLed(int doseIndex, bool on) {
  allAlertsOff();
  if (doseIndex >= 0 && doseIndex < (int)DOSE_COUNT) {
    digitalWrite(schedule[doseIndex].ledPin, on ? HIGH : LOW);
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  }
}
bool pressedEvent(DebouncedButton& b) {
  bool reading = digitalRead(b.pin);
  uint32_t now = millis();
  if (reading != b.lastReading) {
    b.lastReading = reading;
    b.changedAt = now;
  }
  if ((now - b.changedAt) >= DEBOUNCE_MS && reading != b.stableState) {
    b.stableState = reading;
    if (b.stableState == LOW) return true;
  }
  return false;
}
void addHistory(int doseIndex, const DateTime& now, bool taken) {
  if (doseIndex < 0 || doseIndex >= (int)DOSE_COUNT) return;
  HistoryRecord& rec = historyRecords[historyNext];
  String planned = scheduledString(schedule[doseIndex]);
  String confirmed = dateTimeString(now);
  planned.toCharArray(rec.scheduled, sizeof(rec.scheduled));
  confirmed.toCharArray(rec.confirmed, sizeof(rec.confirmed));
  String compartment = schedule[doseIndex].name;
  compartment.toCharArray(rec.compartment, sizeof(rec.compartment));
  String status = taken ? "Da uong" : "Bo lo";
  status.toCharArray(rec.status, sizeof(rec.status));
  historyNext = (historyNext + 1) % HISTORY_CAPACITY;
  if (historyCount < HISTORY_CAPACITY) historyCount++;
  Serial.printf("EVENT,%s,%s,%s,%s\n", rec.compartment, rec.scheduled, rec.confirmed, rec.status);
}
void confirmDose(const DateTime& now) {
  if (state != ALERTING || activeDose < 0) return;
  addHistory(activeDose, now, true);
  Serial.printf("CONFIRMED,%s\n", schedule[activeDose].name);
  activeDose = -1;
  state = IDLE;
  allAlertsOff();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Da xac nhan!");
  lcd.setCursor(0, 1); lcd.print("Cam on ban");
  lastLcdAt = millis();
}
void startAlert(size_t index, const DateTime& now) {
  activeDose = (int)index;
  alertStartedAt = millis();
  lastBlinkAt = 0;
  blinkOn = false;
  state = ALERTING;
  lastTriggeredDate[index] = dateKey(now);
  Serial.printf("ALARM,%s,%s\n", schedule[index].name, scheduledString(schedule[index]).c_str());
}
void checkSchedule(const DateTime& now) {
  if (state != IDLE) return;
  for (size_t i = 0; i < DOSE_COUNT; i++) {
    if (now.hour() == schedule[i].hour &&
        now.minute() == schedule[i].minute &&
        lastTriggeredDate[i] != dateKey(now)) {
      startAlert(i, now);
      return;
    }
  }
}
void updateAlert(const DateTime& now) {
  if (state != ALERTING || activeDose < 0) return;
  uint32_t ms = millis();
  if (ms - lastBlinkAt >= BLINK_MS) {
    lastBlinkAt = ms;
    blinkOn = !blinkOn;
    setDoseLed(activeDose, blinkOn);
  }
  if (ms - alertStartedAt >= ALERT_TIMEOUT_MS) {
    addHistory(activeDose, now, false);
    Serial.printf("MISSED,%s\n", schedule[activeDose].name);
    allAlertsOff();
    activeDose = -1;
    state = IDLE;
  }
}
void updateLcd(const DateTime& now) {
  if (millis() - lastLcdAt < 250) return;
  lastLcdAt = millis();

  if (state == SHOW_HISTORY) return;
  if (state == ALERTING && activeDose >= 0) {
    lcd.setCursor(0, 0);
    lcd.print("NHAC: "); lcd.print(schedule[activeDose].name); lcd.print("       ");
    lcd.setCursor(0, 1);
    lcd.print("Gio "); lcd.print(scheduledString(schedule[activeDose]));
    lcd.print(" - Bam OK    ");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print(twoDigits(now.hour())); lcd.print(":");
  lcd.print(twoDigits(now.minute())); lcd.print(":");
  lcd.print(twoDigits(now.second())); lcd.print("  Hop thuoc   ");
  lcd.setCursor(0, 1);
  lcd.print("Ke tiep ");
  int next = -1;
  for (size_t i = 0; i < DOSE_COUNT; i++) {
    if (schedule[i].hour * 60 + schedule[i].minute > now.hour() * 60 + now.minute()) {
      next = (int)i; break;
    }
  }
  if (next < 0) next = 0;
  lcd.print(schedule[next].name); lcd.print(" ");
  lcd.print(scheduledString(schedule[next])); lcd.print(" ");
}
String htmlEscape(const String& s) {
  String out = s;
  out.replace("&", "&amp;"); out.replace("<", "&lt;"); out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  return out;
}
String pageHtml() {
  DateTime now = rtc.now();
  String html = "<!doctype html><html lang='vi'><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='5'><title>Hop thuoc thong minh</title>";
  html += "<style>body{font-family:Arial;background:#f3f6fb;margin:0;color:#172033}";
  html += ".wrap{max-width:900px;margin:24px auto;padding:16px}.card{background:white;border-radius:14px;padding:18px;margin:12px 0;box-shadow:0 2px 10px #0001}";
  html += "h1{color:#1456a0}table{border-collapse:collapse;width:100%}td,th{padding:9px;border-bottom:1px solid #ddd;text-align:left}small{color:#566}</style></head><body><div class='wrap'>";
  html += "<h1>Hop thuoc thong minh</h1><div class='card'><b>Thoi gian RTC:</b> " + dateTimeString(now);
  html += "<br><b>Trang thai:</b> " + String(state == ALERTING ? "DANG NHAC - " + String(schedule[activeDose].name) : "San sang");
  html += "<p><small>Trang web tu cap nhat moi 5 giay. Lich su chi ton tai trong phien mo phong.</small></p></div>";
  html += "<div class='card'><h2>Lich uong thuoc</h2><table><tr><th>Ngan</th><th>Gio</th></tr>";
  for (size_t i = 0; i < DOSE_COUNT; i++) {
    html += "<tr><td>" + String(schedule[i].name) + "</td><td>" + scheduledString(schedule[i]) + "</td></tr>";
  }
  html += "</table></div><div class='card'><h2>Lich su gan nhat</h2><table><tr><th>Ngan</th><th>Gio theo lich</th><th>Thoi diem ghi nhan</th><th>Trang thai</th></tr>";
  for (int i = 0; i < historyCount; i++) {
    int idx = (historyNext - 1 - i + HISTORY_CAPACITY) % HISTORY_CAPACITY;
    HistoryRecord& rec = historyRecords[idx];
    html += "<tr><td>" + htmlEscape(rec.compartment) + "</td><td>" + htmlEscape(rec.scheduled);
    html += "</td><td>" + htmlEscape(rec.confirmed) + "</td><td>" + htmlEscape(rec.status) + "</td></tr>";
  }
  if (historyCount == 0) html += "<tr><td colspan='4'>Chua co su kien nao.</td></tr>";
  html += "</table></div></div></body></html>";
  return html;
}
void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send(200, "text/html; charset=utf-8", pageHtml()); });
  server.on("/api/status", HTTP_GET, []() {
    DateTime now = rtc.now();
    String body = "{\"time\":\"" + dateTimeString(now) + "\",\"state\":\"";
    body += (state == ALERTING ? "ALERTING" : "IDLE");
    body += "\",\"history_count\":" + String(historyCount) + "}";
    server.send(200, "application/json", body);
  });
  server.begin();
  Serial.println("HTTP server da khoi dong tren cong 80");
}
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Dang ket noi WiFi Wokwi-GUEST");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI_OK, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WIFI_NOT_CONNECTED; phan nhac cuc bo van tiep tuc.");
  }
}
void showHistoryOnLcd(const DateTime& now) {
  state = SHOW_HISTORY;
  allAlertsOff();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Lich su: ");
  lcd.print(historyCount); lcd.print(" su kien");
  if (historyCount > 0) {
    int idx = (historyNext - 1 + HISTORY_CAPACITY) % HISTORY_CAPACITY;
    lcd.setCursor(0, 1);
    lcd.print(historyRecords[idx].compartment);
    lcd.print(" "); lcd.print(historyRecords[idx].status);
  } else {
    lcd.setCursor(0, 1); lcd.print("Chua co du lieu");
  }
  historyViewStartedAt = millis();
}
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_A, OUTPUT); pinMode(PIN_LED_B, OUTPUT); pinMode(PIN_LED_C, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  pinMode(PIN_BTN_HISTORY, INPUT_PULLUP);
  allAlertsOff();

  Wire.begin(21, 22);
  Serial.println("I2C_SCAN_START");
  uint8_t foundI2C = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C_DEVICE,0x%02X\n", address);
      foundI2C++;
    }
  }
  Serial.printf("I2C_SCAN_DONE,count=%u\n", foundI2C);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Hop thuoc Nhom 8");
  lcd.setCursor(0, 1); lcd.print("Dang khoi dong");

  if (!rtc.begin()) {
    Serial.println("ERROR: Khong tim thay DS1307. Kiem tra I2C SDA/SCL.");
    lcd.clear(); lcd.print("Loi RTC DS1307");
    while (true) { allAlertsOff(); delay(500); }
  }
  if (!rtc.isrunning()) {
    Serial.println("RTC chua chay; dat theo thoi gian bien dich.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.println("RTC_OK");
  Serial.println("FORMAT: EVENT,ngan,gio_lich,thoi_diem_ghi_nhan,trang_thai");
  connectWiFi();
  setupWebServer();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("He thong san sang");
  lcd.setCursor(0, 1); lcd.print("Dang doc RTC...");
  Serial.println("SYSTEM_READY");
  delay(700);
}
void loop() {
  DateTime now = rtc.now();

  if (pressedEvent(okButton)) {
    if (state == ALERTING) confirmDose(now);
  }
  if (pressedEvent(historyButton)) {
    if (state != ALERTING) showHistoryOnLcd(now);
  }
  if (state == SHOW_HISTORY && millis() - historyViewStartedAt >= 2500) {
    state = IDLE;
    lcd.clear();
  }

  checkSchedule(now);
  updateAlert(now);
  updateLcd(now);
  server.handleClient();

  if (millis() - lastSerialAt >= 1000) {
    lastSerialAt = millis();
    Serial.printf("TIME,%s,STATE,%s,HISTORY,%u\n",
      dateTimeString(now).c_str(), state == ALERTING ? "ALERTING" : "IDLE", historyCount);
  }
}
