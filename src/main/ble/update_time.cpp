#include "update_time.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <TimeLib.h>

#include <cstring>

#include "esp_log.h"

#include "../rtc.h"

#define SERVICE_UUID        "f9ce43b7-d389-4add-adf7-82811c462ca1"
#define CHARACTERISTIC_UUID "e7e3232e-88c0-452f-abd1-003cc2ec24d3"

const char* TAG = "BLE";

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  public:
    virtual ~CharacteristicCallbacks() {}

    virtual void onWrite(BLECharacteristic* characteristic) override {
      std::string value = characteristic->getValue();
      if(7 == value.size()){
        tmElements_t newTime {
          // our incoming data is reverse from the tmElements declaration
          .Second = static_cast<uint8_t>(value[6]),
          .Minute = static_cast<uint8_t>(value[5]),
          .Hour = static_cast<uint8_t>(value[4]),
          .Wday = static_cast<uint8_t>(value[3]), // day of week, 1-7, sunday is 1
          .Day = static_cast<uint8_t>(value[2]),
          .Month = static_cast<uint8_t>(value[1]),
          .Year = static_cast<uint8_t>(value[0]) // offset from 1970
        };
        rtc::setTime(newTime);
        ESP_LOGI(TAG, "Successfully updated time from Phone");
      }
      else {
        ESP_LOGW(TAG, "Received invalid new time of %i bytes", value.size());
      }
    }
};

class ServerCallbacks : public BLEServerCallbacks {
  public:
    bool connected = false;

    virtual ~ServerCallbacks() {}
    virtual void onConnect(BLEServer* server) override {
      ESP_LOGD(TAG, "Client connected");
      connected = true;
    }
    virtual void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t *param) override {
      ESP_LOGD(TAG, "Client connected 2");
    }

    virtual void onDisconnect(BLEServer* server) override {
      ESP_LOGD(TAG, "Client disconnected");
      connected = false;
    }
};

bool ble::updateTime(int64_t timeout/* = 3'000'000*/) {
  ServerCallbacks callbacks;
  CharacteristicCallbacks characteristicCB;

  BLEDevice::init("WatchyOS");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(&callbacks);
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
                                       );
  pCharacteristic->setWriteNoResponseProperty(true);
  pCharacteristic->setWriteProperty(true);
  pCharacteristic->setCallbacks(&characteristicCB);

  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->setMinInterval(0x20);
  pAdvertising->setMaxInterval(0x40);


  BLEDevice::startAdvertising();
  auto begin_time = esp_timer_get_time();

  ESP_LOGD(TAG, "Started advertising");

  // delay until disconnected or 10 secs elapse
  while(!callbacks.connected && esp_timer_get_time() - begin_time < timeout) {
    vTaskDelay(1);
  }
  // update failed
  if(!callbacks.connected) {
    ESP_LOGW(TAG, "BLE Operation timed out");
    return false;
  }

  while(callbacks.connected) {
    vTaskDelay(1);
  }

  ESP_LOGD(TAG, "BLE operation took %llims", (esp_timer_get_time() - begin_time) / 1'000);

  BLEDevice::stopAdvertising();
  BLEDevice::deinit();

  return true;
}
