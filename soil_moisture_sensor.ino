#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_wifi.h> 

// ======================
// KONFIGURASI TARGET AP GATEWAY
// ======================
const char* GATEWAY_AP_SSID = "GATEWAY_CONFIG_AP";

// ======================
// LCD
// ======================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================
// GATEWAY MAC
// ======================
uint8_t gatewayMAC[] = {
  0xB4, 0xBF, 0xE9, 0x05, 0x0C, 0xA0
};

// ======================
// PIN SETUP & KALIBRASI
// ======================
#define SOIL_PIN 34
#define RELAY_PIN 27  // Pastikan kabel jumper IN relay dicolok ke pin 27

// KALIBRASI FINAL (HASIL TES TANAH & ADAPTOR 12V)
#define DRY_VALUE 2511 // Batas 0% (Tanah Kering)
#define WET_VALUE 681  // Batas 100% (Tanah Lumpur)

// ======================
// NON-BLOCKING TIMERS
// ======================
const unsigned long WATERING_DURATION = 120000UL;   // 2 menit
const unsigned long COOLDOWN_DURATION = 14400000UL; // 4 jam
const unsigned long TASK_INTERVAL     = 2000UL;     // Interval baca & kirim data (2 detik)

unsigned long stateStartTime = 0;
unsigned long lastTaskTime = 0;

// ======================
// STATE MACHINE
// ======================
enum SystemState {
  STATE_IDLE,
  STATE_WATERING,
  STATE_COOLDOWN
};
SystemState currentState = STATE_IDLE;

int currentSoil = 100; // Menyimpan nilai soil terakhir
String myMacAddress;
int currentChannel = 1;

// ======================
// FUNGSI SCANNING CHANNEL GATEWAY (BLOCKING SETUP)
// ======================
int dapatkanChannelGateway(const char* target_ssid) {
  Serial.println("[SCAN] Mencari channel Gateway via SSID...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scanning Gateway");
  
  // Looping terus-menerus sampai SSID Gateway ditemukan (wajib ketemu saat booting)
  while (true) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i) == String(target_ssid)) {
        int ch = WiFi.channel(i);
        WiFi.scanDelete();
        Serial.printf("[SCAN] Sukses! Gateway ditemukan pada Channel: %d\n", ch);
        return ch;
      }
    }
    WiFi.scanDelete();
    Serial.println("[SCAN] Gateway belum terdeteksi. Mengulang pemindaian udara...");
    lcd.setCursor(0, 1);
    lcd.print("Retrying Scan...");
    delay(1000);
  }
}

// ======================
// CALLBACK ESP-NOW
// ======================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Status Kirim ESP-NOW: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("SUKSES");
  } else {
    Serial.println("GAGAL (Interferensi Jarak/Kendala Fisik)");
  }
}

// ======================
// LOGIKA RELAY (FLOATING PIN TRICK)
// ======================
void handleRelay() {
  unsigned long currentMillis = millis();

  switch (currentState) {
    case STATE_IDLE:
      if (currentSoil < 40) {
        // RELAY ON (VALVE BUKA)
        pinMode(RELAY_PIN, OUTPUT);
        digitalWrite(RELAY_PIN, LOW);
        
        currentState = STATE_WATERING;
        stateStartTime = currentMillis;
        Serial.println(">>> RELAY ON: SOIL KERING (WATERING MULAI) <<<");
      } else {
        // RELAY OFF (VALVE TUTUP)
        pinMode(RELAY_PIN, INPUT);
      }
      break;

    case STATE_WATERING:
      if (currentMillis - stateStartTime >= WATERING_DURATION) {
        // RELAY OFF (VALVE TUTUP)
        pinMode(RELAY_PIN, INPUT);
        
        currentState = STATE_COOLDOWN;
        stateStartTime = currentMillis;
        Serial.println(">>> WATERING SELESAI: MASUK COOLDOWN 4 Jam <<<");
      }
      break;

    case STATE_COOLDOWN:
      // Selama cooldown pastikan relay mati (OFF)
      pinMode(RELAY_PIN, INPUT);
      
      if (currentMillis - stateStartTime >= COOLDOWN_DURATION) {
        currentState = STATE_IDLE;
        Serial.println(">>> COOLDOWN SELESAI: STANDBY <<<");
      }
      break;
  }
}

// ======================
// LCD UPDATE
// ======================
void updateLCD(float raw, float soil) {
  lcd.clear();

  // Baris 1: Informasi Soil dan Channel Aktif
  lcd.setCursor(0, 0);
  lcd.print("SOIL: ");
  lcd.print((int)soil);
  lcd.print("% CH:");
  lcd.print(currentChannel);

  // Baris 2: Informasi Status Valve / Cooldown
  lcd.setCursor(0, 1);
  if (currentState == STATE_WATERING) {
    lcd.print("VALVE: OPEN");
  } else if (currentState == STATE_COOLDOWN) {
    unsigned long elapsed = millis() - stateStartTime;
    unsigned long timeLeftMs = COOLDOWN_DURATION - elapsed;
    unsigned long timeLeftMin = (timeLeftMs / 1000) / 60; 

    lcd.print("WAIT: ");
    lcd.print(timeLeftMin);
    lcd.print("m"); 
  } else { 
    lcd.print("VALVE: CLOSE");
  }
}

// ======================
// SETUP
// ======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SOIL NODE START");

  // Default saat awal nyala: Relay OFF
  pinMode(RELAY_PIN, INPUT);

  // Setup WiFi Mode (Wajib WIFI_STA)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Mengambil channel aktif Gateway secara dinamis via pemindaian udara (Blocking)
  currentChannel = dapatkanChannelGateway(GATEWAY_AP_SSID);

  // Paksa internal radio ESP32 Node berpindah ke channel Gateway hasil scan
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  delay(100);

  // Ambil MAC Address perangkat secara dinamis
  myMacAddress = WiFi.macAddress();
  Serial.println("\n===== SOIL NODE =====");
  Serial.print("MAC : ");  Serial.println(myMacAddress);
  Serial.printf("Beroperasi pada Channel Locked: %d\n", currentChannel);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Inisialisasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW FAIL");
    lcd.clear();
    lcd.print("ESP FAIL");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  
  // Daftarkan Gateway sebagai Peer menggunakan Channel dinamis hasil scan
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = currentChannel; 
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("PEER FAIL");
    lcd.clear();
    lcd.print("PEER FAIL");
    return;
  }

  delay(1500);
  lcd.clear();
  lcd.print("NODE READY");
  Serial.println("NODE READY");
}

// ======================
// LOOP
// ======================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Eksekusi relay terus-menerus (sangat responsif, tanpa delay)
  handleRelay();

  // 2. Baca sensor, update LCD, dan kirim JSON setiap 2 detik
  if (currentMillis - lastTaskTime >= TASK_INTERVAL) {
    lastTaskTime = currentMillis;

    // --- BACA SENSOR ---
    int raw = analogRead(SOIL_PIN);
    int soil = map(raw, DRY_VALUE, WET_VALUE, 0, 100);
    currentSoil = constrain(soil, 0, 100);

    // --- UPDATE LCD ---
    updateLCD(raw, currentSoil);

    // --- FORMAT STRING JSON ---
    String jsonPayload = "{";
    jsonPayload += "\"mac\":\"" + myMacAddress + "\",";
    jsonPayload += "\"deviceName\":\"SOIL\",";
    jsonPayload += "\"kelembapan\":" + String(currentSoil);
    jsonPayload += "}";

    // --- SEND DATA JSON ---
    esp_now_send(gatewayMAC, (uint8_t *)jsonPayload.c_str(), jsonPayload.length() + 1);

    // --- SERIAL DEBUG ---
    Serial.println("\n===== DATA SOIL OVER ESP-NOW =====");
    Serial.print("Channel Aktif    : "); Serial.println(currentChannel);
    Serial.print("Payload Terkirim : "); Serial.println(jsonPayload);
    
    // Status visual di Serial Monitor
    if (currentState == STATE_WATERING) {
      Serial.println("STATUS           : WATERING (VALVE OPEN)");
    } else if (currentState == STATE_COOLDOWN) {
      unsigned long elapsed = currentMillis - stateStartTime;
      unsigned long timeLeftMin = ((COOLDOWN_DURATION - elapsed) / 1000) / 60;
      Serial.print("STATUS           : COOLDOWN (WAIT "); Serial.print(timeLeftMin); Serial.println("m)");
    } else {
      Serial.println("STATUS           : STANDBY (VALVE CLOSE)");
    }
    Serial.println("===========================================");
  }
}