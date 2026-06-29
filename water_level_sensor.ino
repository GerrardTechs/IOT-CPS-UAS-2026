#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 

// ======================
// KONFIGURASI TARGET AP GATEWAY
// ======================
const char* GATEWAY_AP_SSID = "GATEWAY_CONFIG_AP";

// ================= PIN =================
#define FLOAT_PIN   19
#define RELAY_PIN   4

// ================= TIMER & STATE =================
unsigned long waktuPenuh = 0;
bool menungguTutup = false;
unsigned long waktuKirimTerakhir = 0;
int relayStateTerakhir = -1; 

// Variabel Global Manajemen Jaringan
String myMacAddress;
int currentChannel = 1;

// ================= MAC GATEWAY =================
uint8_t gatewayAddress[] = {
  0xB4, 0xBF, 0xE9, 0x05, 0x0C, 0xA0
};

esp_now_peer_info_t peerInfo;

// ======================
// FUNGSI SCANNING CHANNEL GATEWAY (BLOCKING SETUP)
// ======================
int dapatkanChannelGateway(const char* target_ssid) {
  Serial.println("[SCAN] Mencari channel Gateway via SSID...");
  
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
    Serial.println("[SCAN] Gateway belum terdeteksi. Mengulang pemindaian udara (Retrying)...");
    delay(1000);
  }
}

// ================= CALLBACK SEND =================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW: Data berhasil terkirim ke GA");
  } else {
    Serial.println("ESP-NOW: Gagal mengirim data (Interferensi Jarak/Kendala Fisik)");
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Water Level System Starting...");

  // PIN Setup
  pinMode(FLOAT_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Pastikan RELAY MATI saat awal (High = OFF pada Active-Low)

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

  // Ambil MAC Address perangkat ini secara dinamis
  myMacAddress = WiFi.macAddress();
  Serial.println("\n===== WATER LEVEL NODE =====");
  Serial.print("MAC Address Alat Ini: "); Serial.println(myMacAddress);
  Serial.printf("Beroperasi pada Channel Locked: %d\n", currentChannel);

  // Inisialisasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW gagal diinisialisasi");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  
  // Daftarkan Gateway sebagai Peer menggunakan Channel dinamis hasil scan
  memcpy(peerInfo.peer_addr, gatewayAddress, 6);
  peerInfo.channel = currentChannel; 
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Gagal mendaftarkan peer gateway");
    return;
  }
  Serial.printf("Gateway Berhasil Didaftarkan (Channel %d)\n", currentChannel);
  Serial.println("NODE READY");
}

// ================= LOOP =================
void loop() {
  unsigned long currentMillis = millis();
  int floatStatus = digitalRead(FLOAT_PIN);
  int waterLevel = (floatStatus == HIGH) ? 0 : 100;

  // ===== LOGIKA KONTROL RELAY AIR =====
  if (floatStatus == HIGH) {
    // AIR RENDAH -> ISI AIR
    menungguTutup = false;
    waktuPenuh = 0;

    digitalWrite(RELAY_PIN, LOW); // LOW = RELAY ON (Mulai isi air)

    if (relayStateTerakhir != 0) { 
      Serial.println(">>> Perintah: Air RENDAH -> Relay ON <<<");
      relayStateTerakhir = 0;
    }
  } 
  else {
    // AIR PENUH
    if (!menungguTutup) {
      waktuPenuh = currentMillis;
      menungguTutup = true;
      Serial.println(">>> Air PENUH: Memulai Delay OFF 10 Detik... <<<");
      relayStateTerakhir = 1;
    }

    if (currentMillis - waktuPenuh >= 10000) {
      digitalWrite(RELAY_PIN, HIGH); // HIGH = RELAY OFF (Matikan pengisian)

      if (relayStateTerakhir != 2) {
        Serial.println(">>> Perintah: Delay Selesai -> Relay OFF <<<");
        relayStateTerakhir = 2;
      }
    } 
    else {
      // Masih berada dalam masa jeda delay 10 detik, pastikan tetap ON
      digitalWrite(RELAY_PIN, LOW); 
    }
  }

  // ===== ESP NOW SEND JSON MURNI (Setiap 5 Detik) =====
  if (currentMillis - waktuKirimTerakhir >= 5000) {
    waktuKirimTerakhir = currentMillis; 

    // Format JSON
    String jsonPayload = "{";
    jsonPayload += "\"mac\":\"" + myMacAddress + "\",";
    jsonPayload += "\"deviceName\":\"WATER\",";
    jsonPayload += "\"water_level\":" + String(waterLevel);
    jsonPayload += "}";
    
    // Debugging print untuk melihat bentuk JSON di Serial Monitor
    Serial.println("\n===== DATA DIKIRIM KE GA OVER ESP-NOW =====");
    Serial.print("Channel Aktif: "); Serial.println(currentChannel);
    Serial.print("Payload JSON : "); Serial.println(jsonPayload);
    Serial.println("===========================================");

    // Mengirim data JSON via ESP-NOW 
    esp_now_send(gatewayAddress, (uint8_t*)jsonPayload.c_str(), jsonPayload.length() + 1);
  }
  
  delay(200); 
}