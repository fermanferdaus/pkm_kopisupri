#include <WiFi.h>
#include <HTTPClient.h>
#include <max6675.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "ConfigPortal.h"

ConfigPortal configPortal;
Config config;

// ======================== Konstanta & Objek ========================
#define SDA 4
#define SCL 5

const int thermoSO = 47;
const int thermoCS = 21;
const int thermoSCK = 20;
const int mq135Pin = 2;
const int buttonPin = 42;
const int modeButtonPin = 15;
const int relayPin = 7;
const int buzzerPin = 35; 

MAX6675 thermocouple(thermoSCK, thermoCS, thermoSO);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ======================== Variabel ========================
bool relayState = false;
bool lastButtonState = HIGH;
bool stableButtonState = HIGH;
bool isOnlineMode = true;
bool lastModeButtonState = HIGH;
bool stableModeButtonState = HIGH;
unsigned long lastModeDebounceTime = 0;
const unsigned long modeDebounceDelay = 100;
unsigned long modeChangeDisplayTime = 0;
bool showModeChangeMessage = false;

bool lastRelayState = false;  
bool wifiConnecting = false;

int modeOnlineMelody[] = {1000, 1200, 1400};
int modeOfflineMelody[] = {1400, 1200, 1000};
int modeMelodyLength = 3;
int melody[] = {1000, 800, 1000, 800, 1000, 800};
int noteDurations[] = {200, 200, 200, 200, 200, 200};
int noteCount = sizeof(melody) / sizeof(melody[0]);

bool isModeMelodyPlaying = false;
int currentModeNote = 0;
unsigned long modeNoteStartTime = 0;

int currentNote = 0;
bool isPlayingMelody = false;
unsigned long noteStartTime = 0;

unsigned long lastWiFiReconnect = 0;
const unsigned long reconnectInterval = 1000;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 100;

unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 1000;

unsigned long lastSyncTime = 0;
const unsigned long syncInterval = 1000; // 1 detik

unsigned long lastManualChangeTime = 0;
const unsigned long manualPriorityDelay = 2000; // 2 detik, tunda sync server

unsigned long relayOnStartTime = 0;
unsigned long relayOnDuration = 0;
bool relayWasOn = false;

const char* urlPostData = "https://smartroaster.kopisupri.com/api/post_data.php";
const char* urlPostStatus = "https://smartroaster.kopisupri.com/api/update_status.php";
const char* urlGetButton = "https://smartroaster.kopisupri.com/api/get_aksi.php";

QueueHandle_t dataQueue;

typedef struct {
  float suhu;
  float asap;
  bool relayStatus;
  char durasi[21];
} SensorData;

// ======================== Nada Buzzer ========================
enum BuzzerMode {
  BUZZER_OFF,
  BUZZER_PERINGATAN,
  BUZZER_TITTIT
};

BuzzerMode currentBuzzerMode = BUZZER_OFF;

unsigned long lastTitTitTime = 0;
const unsigned long titTitInterval = 500; // 500 ms antara tit-tit
bool titTitState = false; // false: mati, true: nyala

#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_B5 988
#define NOTE_C6 1047

int esKrimMelody[] = {
  NOTE_C5, NOTE_E5, NOTE_G5, NOTE_C6, NOTE_E5, NOTE_G5,
  NOTE_C5, NOTE_E5, NOTE_G5, NOTE_C6
};

int esKrimDurations[] = {
  300, 300, 300, 600, 300, 300,
  300, 300, 300, 600
};

int esKrimLength = sizeof(esKrimMelody) / sizeof(int);

void putarLaguEsKrim() {
  int melody[] = {
    NOTE_C5, NOTE_E5, NOTE_G5, NOTE_C6,
    NOTE_G5, NOTE_E5, NOTE_C5, NOTE_G5
  };
  
  int durations[] = {
    250, 250, 250, 500,
    250, 250, 250, 500
  };
  
  int length = sizeof(melody) / sizeof(int);
  
  for (int i = 0; i < length; i++) {
    tone(buzzerPin, melody[i], durations[i]);
    delay(durations[i] + 50);  // jeda antar nada
  }

  noTone(buzzerPin);
}

void updateBuzzerAsap(unsigned long currentMillis) {
  if (currentBuzzerMode != BUZZER_TITTIT) return;

  if (currentMillis - lastTitTitTime >= titTitInterval) {
    lastTitTitTime = currentMillis;
    titTitState = !titTitState;

    if (titTitState) {
      tone(buzzerPin, 1000);  // bunyi tit
    } else {
      noTone(buzzerPin);      // diam
    }
  }
}

// ======================== Fungsi ========================
void connectToWiFi() {
  if (!isOnlineMode) {
    Serial.println("Lewati koneksi WiFi karena mode offline.");
    wifiConnecting = false;
    return;
  }

  lcd.clear();
  lcdPrintCenter(1, "Menghubungkan ke");
  lcdPrintCenter(2, String(config.ssid));

  WiFi.begin(config.ssid, config.password);
  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 10000; // 10 detik timeout

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    configPortal.handleClient();
    handleModeToggle(millis());

    // keluar dari loop jika user berpindah ke offline mode
    if (!isOnlineMode) {
      Serial.println("Dibatalkan: Berpindah ke mode offline saat koneksi.");
      WiFi.disconnect(true);
      lcd.clear();
      lcdPrintCenter(0, "Smart Roaster");
      wifiConnecting = false;
      return;
    }

    delay(100);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected");
    lcdPrintCenter(1, "Terhubung Ke");
    lcdPrintCenter(2, String(config.ssid));
    mulaiNadaMode(true);
    delay(2000);
    lcd.clear();
    lcdPrintCenter(0, "Smart Roaster");
  } else {
    // tampilkan pesan gagal hanya jika masih di mode online
    if (isOnlineMode) {
      Serial.println("WiFi gagal terhubung!");
      lcdPrintCenter(1, "WiFi gagal!");
      lcdPrintCenter(2, "Coba Lagi!");
      delay(2000);
      lcd.clear();
    }
  }

  wifiConnecting = false; // Reset flag agar bisa reconnect di loop jika perlu
}

void handleWiFiReconnect(unsigned long currentMillis) {
  if (WiFi.status() != WL_CONNECTED && currentMillis - lastWiFiReconnect >= reconnectInterval) {
    Serial.println("Mencoba koneksi ulang WiFi...");
    connectToWiFi();
    lastWiFiReconnect = currentMillis;
  }
}

void handleModeToggle(unsigned long currentMillis) {
  bool reading = digitalRead(modeButtonPin);

  if (reading != lastModeButtonState) {
    lastModeDebounceTime = currentMillis;
  }

  if ((currentMillis - lastModeDebounceTime) > modeDebounceDelay) {
    if (reading != stableModeButtonState) {
      stableModeButtonState = reading;

      if (stableModeButtonState == LOW) {
        isOnlineMode = !isOnlineMode;

        mulaiNadaMode(isOnlineMode);

        // Tampilkan mode
        lcd.clear();
        lcdPrintCenter(1, isOnlineMode ? "     MODE ONLINE    " : "    MODE OFFLINE   ");
        delay(2000);
        lcd.clear();
        lcdPrintCenter(0, "Smart Roaster");

        if (!isOnlineMode) {
          WiFi.disconnect(true);  // Matikan WiFi saat offline
          wifiConnecting = false;
        } else {
          wifiConnecting = false; // Biar loop nanti sambung lagi
        }
      }
    }
  }

  lastModeButtonState = reading;
}

void initLCD() {
  Wire.begin(SDA, SCL);
  lcd.begin();
  lcd.backlight();
}

void initIO() {
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(modeButtonPin, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(relayPin, HIGH);
}

void initSerial() {
  Serial.begin(115200);
  delay(1000);
}

void initConfigPortal() {
  configPortal.begin();
  configPortal.getConfig(config);
  WiFi.mode(WIFI_AP_STA);
  configPortal.startAccessPoint();
}

void initWiFiConnection() {
  connectToWiFi();
}

void enterConfigMode() {
  Serial.println("Masuk mode konfigurasi portal...");
  while (true) {
    configPortal.handleClient();
  }
}

void initQueueAndTasks() {
  dataQueue = xQueueCreate(5, sizeof(SensorData));
  xTaskCreatePinnedToCore(httpSendTask, "HTTP Sender", 4096, NULL, 1, NULL, 1);
}

float bacaSuhu() {
  return thermocouple.readCelsius();
}

float bacaAsapPersen() {
  int asapAnalog = analogRead(mq135Pin);
  return ((4095 - asapAnalog) / 4095.0) * 100.0;
}

void lcdPrintCenter(int row, String text) {
  lcd.setCursor(0, row);
  lcd.print("                    ");
  int padding = (20 - text.length()) / 2;
  if (padding < 0) padding = 0;
  lcd.setCursor(padding, row);
  lcd.print(text);
}

void tampilkanSuhu(float suhu) {
  String teks = "Suhu: " + String(suhu, 2) + " " + (char)223 + "C";
  Serial.print(teks + " | ");
  lcdPrintCenter(1, teks);
}

void tampilkanDurasiRelay(unsigned long durationMs) {
  unsigned long totalDetik = durationMs / 1000;
  unsigned int jam = totalDetik / 3600;
  unsigned int menit = (totalDetik % 3600) / 60;
  unsigned int detik = totalDetik % 60;

  char buffer[21];
  if (jam > 0) sprintf(buffer, "Durasi: %02u:%02u:%02u", jam, menit, detik);
  else sprintf(buffer, "Durasi: %02u:%02u", menit, detik);

  lcdPrintCenter(2, String(buffer));
}

void tampilkanAsapRelay(float asapPersen, bool relayStatus) {
  String asapText = "Asap:" + String(asapPersen, 0) + "%";
  String relayText = "Status:" + String(relayStatus ? "ON" : "OFF");
  String combined = asapText + "  " + relayText;
  if (combined.length() > 20) combined = combined.substring(0, 20);
  Serial.println(combined);
  lcd.setCursor(0, 3);
  lcd.print("                    ");
  lcd.setCursor(0, 3);
  lcd.print(combined);
}

void mulaiNadaPeringatan() {
  if (!isPlayingMelody) {
    isPlayingMelody = true;
    currentNote = 0;
    noteStartTime = millis();
    tone(buzzerPin, melody[currentNote], noteDurations[currentNote]);
  }
}

void updateBuzzer() {
  if (isPlayingMelody) {
    unsigned long currentMillis = millis();
    if (currentMillis - noteStartTime >= noteDurations[currentNote]) {
      currentNote++;
      if (currentNote < noteCount) {
        tone(buzzerPin, melody[currentNote], noteDurations[currentNote]);
        noteStartTime = currentMillis;
      } else {
        noTone(buzzerPin);
        isPlayingMelody = false;
      }
    }
  }
}

void mulaiNadaMode(bool online) {
  isModeMelodyPlaying = true;
  currentModeNote = 0;
  modeNoteStartTime = millis();

  int freq = online ? modeOnlineMelody[currentModeNote] : modeOfflineMelody[currentModeNote];
  tone(buzzerPin, freq, 150);
}

void updateModeBuzzer() {
  if (isModeMelodyPlaying) {
    unsigned long currentMillis = millis();
    if (currentMillis - modeNoteStartTime >= 200) { // jeda antar nada
      currentModeNote++;
      if (currentModeNote < modeMelodyLength) {
        int freq = isOnlineMode ? modeOnlineMelody[currentModeNote] : modeOfflineMelody[currentModeNote];
        tone(buzzerPin, freq, 150);
        modeNoteStartTime = currentMillis;
      } else {
        noTone(buzzerPin);
        isModeMelodyPlaying = false;
      }
    }
  }
}

void handleDataUpdateAndSend(unsigned long currentMillis) {
  if (currentMillis - lastUpdateTime >= updateInterval) {
    lastUpdateTime = currentMillis;

    float suhu = bacaSuhu();
    float asapPersen = bacaAsapPersen();
    tampilkanSuhu(suhu);

    unsigned long durasiMs = relayState ? (currentMillis - relayOnStartTime) : relayOnDuration;
    tampilkanDurasiRelay(durasiMs);
    tampilkanAsapRelay(asapPersen, relayState);
    
    bool suhuTinggi = suhu >= config.suhuThreshold;
    bool asapTinggi = asapPersen >= config.asapThreshold;

    if (asapTinggi) {
      if (currentBuzzerMode != BUZZER_TITTIT) {
        Serial.println("Asap tinggi! Buzzer aktif.");
        currentBuzzerMode = BUZZER_TITTIT;
        titTitState = false;
        lastTitTitTime = millis();
        noTone(buzzerPin); // pastikan mati sebelum mulai tit tit
      }
    } else if (suhuTinggi) {
      if (currentBuzzerMode != BUZZER_PERINGATAN) {
        Serial.println("Suhu tinggi! Buzzer aktif.");
        currentBuzzerMode = BUZZER_PERINGATAN;
        mulaiNadaPeringatan();
      }
    } else {
      if (currentBuzzerMode != BUZZER_OFF) {
        Serial.println("Suhu & Asap normal. Buzzer mati.");
        currentBuzzerMode = BUZZER_OFF;
        noTone(buzzerPin);
        isPlayingMelody = false;
      }
    }

    char durasiBuffer[21];
    formatDurasi(durasiMs, durasiBuffer);

    SensorData periodicData;
    periodicData.suhu = suhu;
    periodicData.asap = asapPersen;
    periodicData.relayStatus = relayState;
    strcpy(periodicData.durasi, durasiBuffer);
    xQueueSend(dataQueue, &periodicData, 0);
  }
}

void formatDurasi(unsigned long durasiMs, char* buffer) {
  unsigned long totalDetik = durasiMs / 1000;
  unsigned int jam = totalDetik / 3600;
  unsigned int menit = (totalDetik % 3600) / 60;
  unsigned int detik = totalDetik % 60;

  if (jam > 0)
    sprintf(buffer, "%02u:%02u:%02u", jam, menit, detik);
  else
    sprintf(buffer, "%02u:%02u", menit, detik);
}

void httpSendTask(void *parameter) {
  SensorData data;
  for (;;) {
    if (xQueueReceive(dataQueue, &data, portMAX_DELAY)) {
      if (!isOnlineMode || WiFi.status() != WL_CONNECTED) {
        Serial.println("MODE OFFLINE: Data tidak dikirim.");
        continue;
      }

      HTTPClient http;
      http.begin(urlPostData);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      String postData = "suhu=" + String(data.suhu, 2) +
                        "&durasi=" + String(data.durasi) +
                        "&asap=" + String(data.asap, 0);
      int httpResponseCode = http.POST(postData);
      if (httpResponseCode > 0) Serial.println("HTTP Response code: " + String(httpResponseCode));
      else Serial.println("Error sending POST: " + String(httpResponseCode));
      http.end();
    }
  }
}

void syncRelayFromServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  // TUNDA sync jika baru saja diubah manual
  if (millis() - lastManualChangeTime < manualPriorityDelay) {
    Serial.println("Tunda sync server, prioritas tombol fisik.");
    return;
  }

  HTTPClient http;
  http.begin(urlGetButton);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String response = http.getString();
    int serverButton = response.toInt();
    bool serverRelayState = (serverButton == 1);

    if (relayState != serverRelayState) {
      relayState = serverRelayState;
      digitalWrite(relayPin, relayState ? LOW : HIGH);

      if (relayState) {
        relayOnStartTime = millis();
      } else {
        relayOnDuration = millis() - relayOnStartTime;
        relayWasOn = true;
      }
    }
  }
  http.end();
}

void handleRelaySync(unsigned long currentMillis) {
  if (!isOnlineMode || WiFi.status() != WL_CONNECTED) return;

  if (currentMillis - lastSyncTime >= syncInterval) {
    lastSyncTime = currentMillis;
    syncRelayFromServer();
  }
}

void handleManualButton(unsigned long currentMillis) {
  bool reading = digitalRead(buttonPin);
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
  }

  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        relayState = !relayState;
        digitalWrite(relayPin, relayState ? LOW : HIGH);
        lastManualChangeTime = millis();

        float asapPersen = bacaAsapPersen();
        tampilkanAsapRelay(asapPersen, relayState);

        if (relayState) {
          relayOnStartTime = currentMillis;
        } else {
          relayOnDuration = currentMillis - relayOnStartTime;
          relayWasOn = true;
        }

        sendRelayStatusToServer();
      }
    }
  }
  lastButtonState = reading;
}

void sendRelayStatusToServer() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(urlPostStatus);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String statusData = "status=" + String(relayState ? 1 : 0);
    int response = http.POST(statusData);
    if (response > 0) {
      Serial.println("Status updated (manual), response: " + String(response));
    } else {
      Serial.println("Gagal update status (manual), response: " + String(response));
    }
    http.end();
  } else {
    Serial.println("Tidak bisa kirim status manual, WiFi tidak terhubung");
  }
}

void handleWiFi(unsigned long currentMillis) {
  if (isOnlineMode && WiFi.status() != WL_CONNECTED && !wifiConnecting) {
    wifiConnecting = true;
    connectToWiFi();
  }

  handleWiFiReconnect(currentMillis);
}

void handleAllSync(unsigned long currentMillis) {
  handleRelaySync(currentMillis);
  handleDataUpdateAndSend(currentMillis);
}

void updateAllBuzzers(unsigned long currentMillis) {
  if (currentBuzzerMode == BUZZER_PERINGATAN) {
    updateBuzzer(); // ini melodi peringatan
  } else if (currentBuzzerMode == BUZZER_TITTIT) {
    updateBuzzerAsap(currentMillis);
  }

  updateModeBuzzer(); // untuk nada saat ganti mode
}

void handleAllButtons(unsigned long currentMillis) {
  handleModeToggle(currentMillis);
  handleManualButton(currentMillis);
}

// ======================== Setup & Loop ========================
void setup() {
  initSerial();
  initConfigPortal();
  initLCD();
  initIO();
  putarLaguEsKrim();
  
  if (configPortal.shouldStartPortal()) {
    enterConfigMode(); // jika butuh konfigurasi
  }

  initQueueAndTasks();
}

void loop() {
  configPortal.handleClient();

  unsigned long currentMillis = millis();

  handleAllButtons(currentMillis);
  handleWiFi(currentMillis);
  handleAllSync(currentMillis);
  updateAllBuzzers(currentMillis);
}