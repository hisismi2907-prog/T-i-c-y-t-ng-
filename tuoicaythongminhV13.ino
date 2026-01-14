#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================== KHAI BÁO CHÂN ==================
#define DHTPIN 27
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define SOIL_AO 34      // Cảm biến độ ẩm đất analog

#define RELAY_PUMP 25  // Relay chính - bơm tưới
#define RELAY_EXTRA 26  // Relay phụ (đèn, quạt, bơm phụ...)

int soilValue = 0;
int soilPercent = 0;
int threshold = 40;     // Ngưỡng độ ẩm mặc định (%)
bool pumpState = false; // Trạng thái bơm chính
bool extraState = false;// Trạng thái relay phụ
String mode = "auto";   // "auto" hoặc "manual"

// ================== WIFI & MQTT ==================
const char* ssid = "KHAI KIET";
const char* password = "06122012";
const char* mqtt_server = "test.mosquitto.org";

WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);

// ================== SETUP WIFI ==================
void setup_wifi() {
  Serial.println("Đang kết nối WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi đã kết nối!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ================== CẬP NHẬT OLED ==================
void updateOLED(float t, float h, int soilHum) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Nhiet do: ");
  display.print(t,1);
  display.println(" C");

  display.setCursor(0, 12);
  display.print("Do am KK: ");
  display.print(h,1);
  display.println(" %");

  display.setCursor(0, 24);
  display.print("Do am dat: ");
  display.print(soilHum);
  display.println(" %");

  display.setCursor(0, 36);
  display.print("Mode: ");
  display.println(mode);

  display.setCursor(0, 48);
  display.print("Bom: ");
  display.print(pumpState ? "ON " : "OFF");
  display.print(" | Phu: ");
  display.println(extraState ? "ON" : "OFF");

  display.display();
}

// ================== ĐIỀU KHIỂN QUA HTTP ==================
void handleExtraOn() {
  if (mode == "manual") {
    extraState = true;
    digitalWrite(RELAY_EXTRA, HIGH);  // Relay active HIGH (tùy module, nếu active LOW thì đổi thành LOW)
  }
  server.send(200, "text/plain", extraState ? "extra ON" : "Blocked (Auto mode)");
  Serial.println("HTTP: extra ON requested");
}

void handleExtraOff() {
  if (mode == "manual") {
    extraState = false;
    digitalWrite(RELAY_EXTRA, LOW);
  }
  server.send(200, "text/plain", "extra OFF");
  Serial.println("HTTP: extra OFF requested");
}

void handlePumpOn() {
  extraState = true;
  digitalWrite(RELAY_PUMP, HIGH);//PUMP
  server.send(200, "text/plain", "Relay Extra ON");
}

void handlePumpOff() {
  pumpState = false;
  digitalWrite(RELAY_PUMP, LOW);//EXTRA
  server.send(200, "text/plain", "Relay pump OFF");
}

// ================== MQTT CALLBACK ==================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.print("MQTT nhận [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  if (String(topic) == "relay/control") {
    if (mode == "manual") {
      if (msg == "true" || msg == "1" || msg == "on") {
        pumpState = true;
        digitalWrite(RELAY_PUMP, HIGH);
      } else {
        pumpState = false;
        digitalWrite(RELAY_PUMP, LOW);
      }
      client.publish("relay/state", pumpState ? "1" : "0");
    } else {
      Serial.println("⚠️ Chế độ AUTO → bỏ qua lệnh thủ công bơm!");
    }
  }

  if (String(topic) == "relay/extra") {
    if (msg == "true" || msg == "1" || msg == "on") {
      extraState = true;
      digitalWrite(RELAY_EXTRA, HIGH);
    } else {
      extraState = false;
      digitalWrite(RELAY_EXTRA, LOW);
    }
    client.publish("relay/extra/state", extraState ? "1" : "0");
  }

  if (String(topic) == "cambiendat/tuychon") {
    threshold = msg.toInt();
    threshold = constrain(threshold, 10, 90);
    Serial.print("Ngưỡng mới: ");
    Serial.println(threshold);
  }

  if (String(topic) == "control/mode") {
    msg.toLowerCase();
    if (msg == "auto" || msg == "manual") {
      mode = msg;
      Serial.print("Chuyển chế độ: ");
      Serial.println(mode);
      client.publish("control/mode/state", mode.c_str());

      if (mode == "auto") {
        // Tắt bơm khi chuyển sang auto (an toàn)
        pumpState = false;
        digitalWrite(RELAY_PUMP, LOW);
      }
    }
  }
}

// ================== MQTT RECONNECT ==================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Đang kết nối MQTT...");
    if (client.connect("ESP32Client_TuoiCay")) {
      Serial.println("✅ MQTT Connected!");
      client.subscribe("relay/control");
      client.subscribe("relay/extra");
      client.subscribe("cambiendat/tuychon");
      client.subscribe("control/mode");

      client.publish("control/mode/state", mode.c_str());
      client.publish("relay/state", pumpState ? "1" : "0");
      client.publish("relay/extra/state", extraState ? "1" : "0");
    } else {
      Serial.print("Lỗi, rc=");
      Serial.print(client.state());
      Serial.println(" → thử lại sau 5s");
      delay(5000);
    }
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  // Khởi tạo các chân relay
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_EXTRA, OUTPUT);
  digitalWrite(RELAY_PUMP, LOW);
  digitalWrite(RELAY_EXTRA, LOW);

  dht.begin();
  setup_wifi();

  // MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Web Server routes
  server.on("/pump/on", HTTP_GET, handlePumpOn);
  server.on("/pump/off", HTTP_GET, handlePumpOff);
  server.on("/extra/on", HTTP_GET, handleExtraOn);
  server.on("/extra/off", HTTP_GET, handleExtraOff);
  server.begin();
  Serial.println("HTTP server started");

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("❌ Không tìm thấy OLED!"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("Smart Plant Ready!");
  display.display();
  delay(2000);
}

// ================== LOOP ==================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  server.handleClient();  // Xử lý request HTTP

  // Đọc cảm biến độ ẩm đất
  soilValue = analogRead(SOIL_AO);
  soilPercent = map(soilValue, 0, 4095, 100, 0);  // 0 = khô, 4095 = ướt → map ngược

  // Đọc DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // Publish dữ liệu và cập nhật OLED
  if (!isnan(h) && !isnan(t)) {
    Serial.printf("🌡️ %.1f°C | 💧 %.1f%% | 🌱 %d%% | Mode: %s\n", t, h, soilPercent, mode.c_str());
    client.publish("Ehab/DHT/Temp", String(t).c_str());
    client.publish("Ehab/DHT/Humidity", String(h).c_str());
    client.publish("Ehab/doamdat", String(soilPercent).c_str());  // Sửa topic cho phù hợp (bỏ /Temp nếu là độ ẩm đất)
    updateOLED(t, h, soilPercent);
  }

  // Logic tự động bơm (chỉ ở mode auto)
  if (mode == "auto") {
    if (soilPercent < threshold) {
      pumpState = true;
      digitalWrite(RELAY_PUMP, HIGH);
    } else {
      pumpState = false;
      digitalWrite(RELAY_PUMP, LOW);
    }
    client.publish("relay/state", pumpState ? "1" : "0");
  }
  // Ở chế độ manual thì không can thiệp tự động, chỉ điều khiển qua MQTT/HTTP

  // Xử lý lỗi cảm biến (soilPercent == 0 có thể là lỗi)
  if (soilPercent == 0 && mode == "auto") {
    mode = "manual";
    pumpState = false;
    digitalWrite(RELAY_PUMP, LOW);
    Serial.println("⚠️ Cảm biến lỗi → Chuyển MANUAL & tắt bơm!");
    client.publish("control/mode/state", mode.c_str());
  }
  else if (soilPercent < threshold) {
    if (mode != "auto") {
      mode = "auto";
      Serial.println("🌱 Độ ẩm < 40% → Chuyển sang AUTO!");
      client.publish("control/mode/state", mode.c_str());
    }
    pumpState = true;  // Bật bơm tự động
  }

  else if (soilPercent >= threshold) {
    // Chỉ tắt bơm nếu đang ở AUTO
    if (mode == "auto" && pumpState) {
     pumpState = false;
      Serial.println("💧 Độ ẩm > 40% → AUTO tự tắt bơm!");
      client.publish("relay/state", "0");
    }

    // Sau đó chuyển sang manual
    if (mode != "manual") {
      mode = "manual";
      Serial.println("⚙️ Chuyển sang MANUAL → có thể bật bơm thủ công!");
      client.publish("control/mode/state", mode.c_str());
    }
  }

  delay(2000);
}