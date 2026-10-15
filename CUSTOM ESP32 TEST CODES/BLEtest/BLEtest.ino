#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// Unique IDs for your custom Bluetooth channel
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// --- THE NEW BOUNCER (Bulletproof Version) ---
class MyServerCallbacks: public BLEServerCallbacks {
    // This triggers the exact millisecond a device connects
    void onConnect(BLEServer* pServer) {
      Serial.println("\n[ >>> PHONE CONNECTED! <<< ]");
      Serial.println("Ready to receive data...");
    }

    // This triggers the moment the device drops the connection
    void onDisconnect(BLEServer* pServer) {
      Serial.println("\n[ <<< PHONE DISCONNECTED <<< ]");
      Serial.println("Restarting advertising so you can reconnect...");
      
      // Restart the broadcast so the board doesn't go invisible
      BLEDevice::startAdvertising(); 
    }
};

void setup() {
  Serial.begin(115200);
  delay(2000); // Give the serial port time to connect
  
  Serial.println("\n=================================");
  Serial.println("    ESP32-S3 BLE Beacon Test     ");
  Serial.println("=================================");
  Serial.println("Starting BLE Server...");

  BLEDevice::init("My_Custom_S3_Board");

  BLEServer *pServer = BLEDevice::createServer();
  
  // Attach our bouncer to the server!
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setValue("Hardware test passed. Hello World!");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  
  Serial.println("BLE is active and broadcasting!");
  Serial.println("Open 'nRF Connect' on your phone, find the board, and click Connect.");
}

void loop() {
  // BLE handles itself in the background
  delay(10000);
}