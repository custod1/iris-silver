#include <Arduino.h>
#include <ESP32QRCodeReader.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// LED引脚
#define LED_GPIO_NUM 4

// BLE UUID定义
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "12345678-1234-5678-1234-56789abcdef1"
#define DEVICE_NAME         "ESP32_CAM_QR"

// 全局变量
ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
String lastQRCode = "";
unsigned long lastDetectionTime = 0;
const unsigned long DETECTION_COOLDOWN = 3000; // 3秒冷却时间

// BLE服务器回调
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE Client Connected");
      digitalWrite(LED_GPIO_NUM, HIGH);
      delay(200);
      digitalWrite(LED_GPIO_NUM, LOW);
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE Client Disconnected");
      // 重新开始广播
      delay(500);
      pServer->startAdvertising();
      Serial.println("BLE Advertising restarted");
    }
};

// 初始化BLE
void initBLE() {
  BLEDevice::init(DEVICE_NAME);
  
  // 创建BLE服务器
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // 创建BLE服务
  BLEService* pService = pServer->createService(SERVICE_UUID);
  
  // 创建BLE特征
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );
  
  // 添加描述符
  pCharacteristic->addDescriptor(new BLE2902());
  
  // 启动服务
  pService->start();
  
  // 开始广播
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE Service started, waiting for connections...");
}

// 发送QR码数据
void sendQRCodeData(const String& qrCode) {
  // 检查冷却时间，避免重复发送
  unsigned long currentTime = millis();
  if (currentTime - lastDetectionTime < DETECTION_COOLDOWN && qrCode == lastQRCode) {
    return;
  }
  
  lastQRCode = qrCode;
  lastDetectionTime = currentTime;
  
  // 构建要发送的消息
  String message = "QR:" + qrCode + ",TIME:" + String(currentTime);
  
  // 串口输出
  Serial.println("\n========================================");
  Serial.println("QR CODE DETECTED!");
  Serial.println("Content: " + qrCode);
  Serial.println("Message: " + message);
  
  // 通过BLE发送（如果已连接）
  if (deviceConnected) {
    pCharacteristic->setValue(message.c_str());
    pCharacteristic->notify();
    Serial.println("Sent via BLE: " + message);
  } else {
    Serial.println("BLE not connected - data saved for later");
  }
  
  Serial.println("========================================\n");
  
  // LED闪烁提示
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_GPIO_NUM, HIGH);
    delay(100);
    digitalWrite(LED_GPIO_NUM, LOW);
    delay(100);
  }
}

// QR码检测任务
void onQrCodeTask(void *pvParameters) {
  struct QRCodeData qrCodeData;

  while (true) {
    if (reader.receiveQrCode(&qrCodeData, 100)) {
      if (qrCodeData.valid && qrCodeData.payload[0] != '\0') {
        String qrCode = String((const char *)qrCodeData.payload);
        sendQRCodeData(qrCode);
      } else if (qrCodeData.payload[0] != '\0') {
        Serial.print("Invalid QR: ");
        Serial.println((const char *)qrCodeData.payload);
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== ESP32-CAM QR Code Scanner with BLE ===");
  
  // 初始化LED
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);
  
  // 初始化QR码读取器
  reader.setup();
  Serial.println("QR Code Reader initialized");
  
  reader.beginOnCore(1);
  Serial.println("QR Reader started on Core 1");
  
  // 创建QR码检测任务
  xTaskCreate(onQrCodeTask, "onQrCode", 4 * 1024, NULL, 4, NULL);
  
  // 初始化BLE
  initBLE();
  
  Serial.println("\n=== System Ready ===");
  Serial.println("1. QR Code detection active");
  Serial.println("2. BLE advertising as: " + String(DEVICE_NAME));
  Serial.println("3. Service UUID: " + String(SERVICE_UUID));
  Serial.println("4. Connect with nRF52840 DK to receive data");
  Serial.println("====================\n");
}

void loop() {
  // 状态指示
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime > 10000) {  // 每10秒
    lastStatusTime = millis();
    
    Serial.print("Status: ");
    if (deviceConnected) {
      Serial.println("BLE Connected ✓ | Scanning for QR codes...");
    } else {
      Serial.println("BLE Advertising... | Scanning for QR codes...");
    }
    
    // 如果有未发送的数据且现在已连接，重新发送
    if (deviceConnected && lastQRCode.length() > 0) {
      String message = "QR:" + lastQRCode + ",TIME:" + String(lastDetectionTime);
      pCharacteristic->setValue(message.c_str());
      pCharacteristic->notify();
      Serial.println("Resent last QR code via BLE");
    }
  }
  
  delay(100);
}