#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>   
#include <esp_wifi.h> 
#include <WiFiManager.h>   

// =========================
// LCD
// =========================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =========================
// LED STATUS
// =========================
#define LED_SOIL   25
#define LED_WATER  26
#define LED_DHT    27

// =========================
// MAC ADDRESS NODE
// =========================
uint8_t soilMAC[]  = {0x30, 0x76, 0xF5, 0xF0, 0x4A, 0xAC};
uint8_t waterMAC[] = {0x30, 0x76, 0xF5, 0xF0, 0x5D, 0x30};
uint8_t dhtMAC[]   = {0xB4, 0xBF, 0xE9, 0x1C, 0x67, 0x10};

// =========================
// DATA VARIABEL UNTUK LCD
// =========================
float valSuhu = 0.0;
int valWater = 0;
int valKelembapan = 0;

// =========================
// TIMER ONLINE/OFFLINE
// =========================
unsigned long lastSoilReceive  = 0;
unsigned long lastWaterReceive = 0;
unsigned long lastDhtReceive   = 0;
const unsigned long TIMEOUT = 15000;

// =========================
// CONFIG RMQ / MQTT 
// =========================
const char* mqtt_server = "195.35.23.135";
const int mqtt_port = 1883;
const char* mqtt_user = "/vh-iot-cps-2026:iot-cps-2026"; 
const char* mqtt_pass = "iotcihuy71.";
const char* mqtt_topic = "gateway_tim2";

WiFiClient espClient;
PubSubClient client(espClient);
String macAddress;

// Parsing JSON Ringan
String ambilNilaiJSON(String json, String key) {
  String targetKey = "\"" + key + "\":";
  int pos = json.indexOf(targetKey);
  if (pos == -1) return "";
  int startPos = pos + targetKey.length();
  int endPos = json.indexOf(",", startPos);
  if (endPos == -1) endPos = json.indexOf("}", startPos);
  String nilai = json.substring(startPos, endPos);
  nilai.replace("\"", "");
  nilai.trim();
  return nilai;
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Pesan masuk dari RMQ: ");
  for (int i = 0; i < length; i++) Serial.print((char)payload[i]);
  Serial.println();
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return; // Hanya konek MQTT jika Wi-Fi aktif
  if (client.connected()) return;
  Serial.print("MQTT connect...");
  String clientId = "98384675" + macAddress;
  if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    Serial.println("Connected");
    client.subscribe(mqtt_topic);
  } else {
    Serial.print("Gagal rc=");
    Serial.println(client.state());
  }
}

// RECEIVE CALLBACK ESP-NOW
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char buffer[len + 1];
  memcpy(buffer, buffer, len);
  buffer[len] = '\0'; 
  String incomingJson = String((char*)data); // Ambil langsung dari data cast untuk keamanan

  Serial.println("\n================================");
  Serial.print("Payload : "); Serial.println(incomingJson);

  if (memcmp(info->src_addr, soilMAC, 6) == 0) {
    lastSoilReceive = millis();
    String nilaiStr = ambilNilaiJSON(incomingJson, "kelembapan");
    if(nilaiStr != "") valKelembapan = nilaiStr.toInt();
  } 
  else if (memcmp(info->src_addr, waterMAC, 6) == 0) {
    lastWaterReceive = millis();
    String nilaiStr = ambilNilaiJSON(incomingJson, "water_level");
    if(nilaiStr != "") valWater = nilaiStr.toInt();
  } 
  else if (memcmp(info->src_addr, dhtMAC, 6) == 0) {
    lastDhtReceive = millis();
    String nilaiStr = ambilNilaiJSON(incomingJson, "suhu");
    if(nilaiStr != "") valSuhu = nilaiStr.toFloat();
  }

  if (client.connected()) {
    client.publish(mqtt_topic, incomingJson.c_str());
  }
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Config Mode");

  pinMode(LED_SOIL, OUTPUT);
  pinMode(LED_WATER, OUTPUT);
  pinMode(LED_DHT, OUTPUT);

  // --------------------------------------------------
  // PROSEDUR WIFI MANAGER + SMART TIMEOUT
  // --------------------------------------------------
  WiFiManager wm;
  
  // Konfigurasi Batas Waktu Otomatis
  wm.setConnectTimeout(15);         // Batas mencoba konek ke Wi-Fi lama (15 detik)
  wm.setConfigPortalTimeout(120);    // Batas portal AP menyala di lapangan (120 detik / 2 menit)

  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  // Proses autoConnect tidak akan mengunci/merestart ESP jika gagal
  if (!wm.autoConnect("GATEWAY_CONFIG_AP")) {
    Serial.println("[WIFI] Timeout tercapai atau gagal terhubung. Masuk mode Offline (Bypass ke ESP-NOW).");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Offline Mode");
    lcd.setCursor(0, 1);
    lcd.print("ESP-NOW Only");
    delay(3000);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    delay(1000);
  }

  // --------------------------------------------------
  // MODIFIKASI TRICK PILIHAN B (AP AKTIF PERMANEN)
  // --------------------------------------------------
  // Pastikan mode dual STA + AP tetap dipaksa aktif terlepas dari status koneksi internet
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("GATEWAY_CONFIG_AP", NULL); 
  Serial.println("[SYSTEM] Mode WIFI_AP_STA Aktif Secara Permanen.");

  macAddress = WiFi.macAddress();
  Serial.print("Gateway MAC : "); Serial.println(macAddress);

  // Dapatkan channel radio operasional aktif
  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  
  // Jika Wi-Fi router tidak terkoneksi, default channel internal ESP32 biasanya ada di Channel 1
  if(primaryChan == 0) primaryChan = 1; 

  Serial.printf("WiFi Radio Channel Terunci: %d\n", primaryChan);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  lcd.setCursor(0, 1);
  lcd.print("Chan Locked: "); lcd.print(primaryChan);

  // Inisialisasi Jalur Komunikasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW READY");

  // Inisialisasi MQTT Broker (Hanya akan berhasil jika status internet aktif)
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  connectMQTT();

  delay(2000);
  lcd.clear();
}

void loop() {
  // Hanya jalankan client MQTT jika hardware benar-benar terhubung ke internet router
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      connectMQTT();
    }
    client.loop();
  }

  unsigned long currentMillis = millis();

  bool soilOnline  = (lastSoilReceive  > 0) && ((currentMillis - lastSoilReceive)  < TIMEOUT);
  bool waterOnline = (lastWaterReceive > 0) && ((currentMillis - lastWaterReceive) < TIMEOUT);
  bool dhtOnline   = (lastDhtReceive   > 0) && ((currentMillis - lastDhtReceive)   < TIMEOUT);

  digitalWrite(LED_SOIL, soilOnline);
  digitalWrite(LED_WATER, waterOnline);
  digitalWrite(LED_DHT, dhtOnline);

  // Rolling Display LCD 3 Detik
  static unsigned long lastLcdUpdate = 0;
  static int halamanLcd = 0;

  if (currentMillis - lastLcdUpdate > 3000) {
    lastLcdUpdate = currentMillis;
    lcd.clear();
    switch (halamanLcd) {
      case 0:
        lcd.setCursor(0, 0);
        if (dhtOnline) lcd.printf("Suhu : %.1f C", valSuhu);
        else           lcd.print("DHT  : OFFLINE");
        lcd.setCursor(0, 1);
        if (waterOnline) lcd.printf("Water: %d %%", valWater);
        else             lcd.print("WATER: OFFLINE");
        halamanLcd = 1;
        break;
      case 1:
        lcd.setCursor(0, 0);
        if (soilOnline) lcd.printf("Kelembaban: %d %%", valKelembapan);
        else            lcd.print("SOIL    : OFFLINE");
        halamanLcd = 0;
        break;
    }
  }
}