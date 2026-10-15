#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

const int ledPin = 1;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// This class listens for incoming messages from the phone
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      // Grab the incoming data
      String rxValue = pCharacteristic->getValue().c_str();

      if (rxValue.length() > 0) {
        Serial.print("Received from phone: ");
        Serial.println(rxValue);

        // Check if the phone sent the word "ON" or "OFF"
        if (rxValue == "ON") {
          digitalWrite(ledPin, HIGH);
          Serial.println("Action: LED turned ON");
        } 
        else if (rxValue == "OFF") {
          digitalWrite(ledPin, LOW);
          Serial.println("Action: LED turned OFF");
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  delay(2000);

  Serial.println("\n--- Starting BLE Server ---");
  BLEDevice::init("ESP32_BLE_Node");
  
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Notice we added "PROPERTY_WRITE" so the phone is allowed to send data
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  // Attach our listener class to the characteristic
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("Waiting for command...");
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE Ready! Waiting for phone connection...");
}

void loop() {
  delay(2000); // Do nothing, the BLE Callback handles everything!
}