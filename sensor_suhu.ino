#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <esp_wifi.h> 

// ======================
// KONFIGURASI TARGET AP GATEWAY
// ======================
const char* GATEWAY_AP_SSID = "GATEWAY_CONFIG_AP";

// ======================
// GATEWAY MAC ADDRESS
// ======================
uint8_t gatewayMAC[] = { 0xB4, 0xBF, 0xE9, 0x05, 0x0C, 0xA0 };

// ======================
// DHT CONFIG
// ======================
#define DHTPIN 4       // Pin sensor suhu
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ======================
// RELAY CONFIG
// ======================
#define RELAY_PIN 5    // Sambungkan pin IN/Signal Relay ke Pin 5 ESP32

// ======================
// LCD CONFIG
// ======================  
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Variabel Global Manajemen Jaringan
String myMacAddress;
int currentChannel = 1;
unsigned long waktuKirimTerakhir = 0;

// ======================
// FUNGSI SCANNING CHANNEL GATEWAY (BLOCKING SETUP)
// ======================
int dapatkanChannelGateway(const char* target_ssid) {
  Serial.println("[SCAN] Mencari channel Gateway via SSID...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scanning Gateway");
  
  // Looping terus-menerus sampai SSID Gateway ditemukan (wajib ketemu saat booting awal)
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
// CALLBACK SEND
// ======================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Status Kirim ke GA : ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("SUKSES COY!");
  } else {
    Serial.println("GAGAL (Interferensi Jarak/Kendala Fisik)");
  }
}

// ======================
// SETUP
// ======================
void setup() {
  Serial.begin(115200);

  // Inisialisasi Pin Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Pastikan kipas mati saat awal

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Menyiapkan Alat");

  // Inisialisasi DHT
  dht.begin();

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
  Serial.println("\n===== TEMPERATURE NODE =====");
  Serial.print("MAC Address Alat Ini: "); Serial.println(myMacAddress);
  Serial.printf("Beroperasi pada Channel Locked: %d\n", currentChannel);

  // Inisialisasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT GAGAL");
    lcd.setCursor(0, 1);
    lcd.print("ESP-NOW Gagal! ");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  // Daftarkan Gateway sebagai Peer menggunakan Channel dinamis yang ditemukan
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = currentChannel;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("GAGAL TAMBAH PEER GATEWAY");
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Alat Siap!");
  delay(1000);
}

// ======================
// LOOP
// ======================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Baca Sensor Suhu dan Kelembapan
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  // Cek jika sensor gagal membaca data
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Gagal membaca dari sensor DHT!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error!");
    delay(2000);
    return;
  }

  // Logika Kipas
  if (temperature > 35.0) {
    digitalWrite(RELAY_PIN, HIGH); // Nyalakan kipas
    Serial.println(">> SUHU PANAS! Kipas Menyala <<");
  } else {
    digitalWrite(RELAY_PIN, LOW);  // Matikan kipas
  }

  // Tampilkan Suhu dan Kelembapan ke LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: "); lcd.print(temperature, 1); lcd.print(" C");
  lcd.setCursor(0, 1);
  lcd.print("Hum : "); lcd.print(humidity, 1); lcd.print(" %");

  // 2. Pengiriman Data Menggunakan Timer Non-Blocking Millis (Setiap 5 Detik)
  if (currentMillis - waktuKirimTerakhir >= 5000) {
    waktuKirimTerakhir = currentMillis;

    // Susun Data Menjadi Format JSON
    String jsonPayload = "{";
    jsonPayload += "\"mac\":\"" + myMacAddress + "\",";
    jsonPayload += "\"deviceName\":\"DHT\",";
    jsonPayload += "\"suhu\":" + String(temperature, 2);
    jsonPayload += "}";

    // Kirim via ESP-NOW ke Gateway
    esp_now_send(gatewayMAC, (uint8_t *)jsonPayload.c_str(), jsonPayload.length() + 1);

    // Cetak ke Serial Monitor untuk Debugging
    Serial.println("\n===== DATA DIKIRIM KE GA OVER ESP-NOW =====");
    Serial.print("Channel Aktif: "); Serial.println(currentChannel);
    Serial.print("Payload JSON : "); Serial.println(jsonPayload);
    Serial.println("===========================================");
  }

  delay(200); 
}