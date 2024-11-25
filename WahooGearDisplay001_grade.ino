/**
* An external gear display for the Wahoo Kickr Bike
* On the LiLiGo T-Display
* Based on the Arduino BLE Example
* Extremely dirty code missing any kind of comments....
 */
// New background colour
//#define TFT_BROWN 0x38E0

#include "BLEDevice.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
//#include "zwiftlogo.h"
//#include "BLEScan.h"
#include "lock64.h"
#include "unlock64.h"

#include <WiFi.h>
#include <HTTPClient.h>

// Constants stored in PROGMEM to save RAM
static const char PROGMEM WIFI_CONNECTING[] = "Connecting";
static const char PROGMEM WIFI_CONNECTED[] = "Connected to WiFi network with IP Address: ";
static const char PROGMEM BLE_STARTING[] = "Starting Arduino BLE Client application...";

// Display update control
static unsigned long lastDisplayUpdate = 0;
static const unsigned long DISPLAY_UPDATE_INTERVAL = 100; // 10 fps
static bool needsGearUpdate = false;
static bool needsGradeUpdate = false;
static bool needsPowerUpdate = false;

// Connection management
static unsigned long lastWiFiRetry = 0;
static const unsigned long WIFI_RETRY_INTERVAL = 5000;
static unsigned long lastBLERetry = 0;
static const unsigned long BLE_RETRY_INTERVAL = 3000;
static uint8_t connectionRetries = 0;
static const uint8_t MAX_RETRIES = 5;

// HTTP client reuse
static HTTPClient http;
static WiFiClient wifiClient;

// Camera control
static unsigned long lastCamChange = 0;
static const unsigned long CAM_CHANGE_DEBOUNCE = 100; // ms

// Gear state tracking
static struct {
    uint8_t front;
    uint8_t rear;
    float grade;
    uint16_t power;
    bool tiltLock;
    bool isDirty;
} bikeState = {0, 0, 0.0, 0, false, true};

// Gear configuration
static struct GearConfig {
    uint8_t gearType;       // Type of gear system (e.g., 0=Shimano, 4=Campagnolo)
    uint8_t frontTeeth[3];  // 3 front chainrings
    uint8_t rearTeeth[13];  // Max 13 rear cogs
    bool isConfigured;
} gearConfig;

const char* ssid = "<yourSSID>";
const char* password = "<yourPassword>";

const char* serverName = "http://<sauceServer>:1080/api/rpc/v1/updateAthleteData";
const char* brakeServer_camera_back = "http://<keypressServer>:8080/api/camback";
const char* brakeServer_camera_frnt = "http://<keypressServer>:8080/api/camfrnt";
const char* brakeServer_camera_over = "http://<keypressServer>:8080/api/camover";

// Global variables for connection management
bool deviceConnected = false;
unsigned long lastConnectionAttempt = 0;
const unsigned long CONNECTION_RETRY_INTERVAL = 5000;  // 5 seconds between connection attempts
const int MAX_CONNECTION_ATTEMPTS = 10;
int connectionAttempts = 0;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

// The remote service we wish to connect to.
static BLEUUID serviceUUID("a026ee0d-0a7d-4ab3-97fa-f1500f9feb8b");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("a026e03a-0a7d-4ab3-97fa-f1500f9feb8b");

//for grade
// The remote service we wish to connect to.
static BLEUUID serviceUUID2("a026ee0b-0a7d-4ab3-97fa-f1500f9feb8b");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID2("a026e037-0a7d-4ab3-97fa-f1500f9feb8b");

static BLEUUID serviceUUID3("00001818-0000-1000-8000-00805f9b34fb");
static BLEUUID    charUUID3("00002a63-0000-1000-8000-00805f9b34fb");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* pRemoteCharacteristic2;
static BLERemoteCharacteristic* pRemoteCharacteristic3;
static BLEAdvertisedDevice* myDevice;
static uint8_t bleConnectionRetries = 0;
static const uint8_t MAX_BLE_RETRIES = 3;

String reargear = "0";
String frontgear = "0";
String grade = "0.0%";
String power = "000";
char ACTIVECAM = '1';

int fg, rg;
uint8_t arr[32];
int tilt_lock = 1;
int negative = 0;
int brake = 0; //left = 1, right = 2, off = 0;

char LEFT = '9'; // overhead
char RIGHT = '6'; // backwards
char NONE = '1'; // Front cam

void handleBLENotification(uint8_t* pData, size_t length, uint8_t type) {
    switch(type) {
        case 1: // Gear data
            if (pData[0] == 7) {
                // Status packet
                frontgear = String(1+pData[2]);
                reargear  = String(1+pData[3]);

                fg = (int)(1+pData[2]);
                rg = (int)(1+pData[3]);

                needsGearUpdate = true;
            } else if (pData[0] == 2) {
                char before = ACTIVECAM;
                if (pData[2] == 90) {
                    ACTIVECAM = (ACTIVECAM == LEFT) ? NONE : LEFT;
                } else if (pData[2] == 227) {
                    ACTIVECAM = (ACTIVECAM == RIGHT) ? NONE : RIGHT;
                }
                if (ACTIVECAM != before) {
                    changeCam();
                }
            } else if (pData[0] == 0xFE && pData[1] == 0x01 && pData[2] == 0x01) {
                // Gear config data
                if (pData[3] == 0x84 && length >= 17) {  // Rear gear config
                    // Parse rear teeth values (starting at index 4)
                    for (int i = 0; i < 13; i++) {
                        gearConfig.rearTeeth[i] = pData[i + 4];
                    }
                }
                else if (pData[3] == 0x83 && length >= 8) {  // Front gear config
                    gearConfig.gearType = pData[4];  // Store gear type
                    // Use next 3 values for teeth counts
                    for (int i = 0; i < 3; i++) {
                        gearConfig.frontTeeth[i] = pData[i + 5];
                    }
                    gearConfig.isConfigured = true;
                }
            }
            break;

        case 2: // Grade data
            if (length == 4 && pData[0] == 0xfd && pData[1] == 0x34) {
                if (pData[3] < 0x80) {
                    bikeState.grade = (float)(pData[3] << 8 | pData[2]) / 100;
                } else {
                    uint16_t tmp16 = 0xffff - (pData[3] << 8 | pData[2]);
                    bikeState.grade = -(float)tmp16 / 100;
                }
                needsGradeUpdate = true;
            } else if (length == 3 && pData[0] == 0xfd && pData[1] == 0x33) {
                bool newTiltLock = pData[2] == 0x01;
                if (bikeState.tiltLock != newTiltLock) {
                    bikeState.tiltLock = newTiltLock;
                    tilt_lock = bikeState.tiltLock;
                    update_lock(); // Only update display when state changes
                }
            }
            break;

        case 3: // Power data
            if (length >= 4) {
                bikeState.power = (pData[3] << 8 | pData[2]);
                needsPowerUpdate = true;
            }
            break;
    }

    bikeState.isDirty = true;
}

static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    handleBLENotification(pData, length, 1);
}

static void notifyCallback2(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    handleBLENotification(pData, length, 2);
}

static void notifyCallback3(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    handleBLENotification(pData, length, 3);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
    if (!myDevice) {
        Serial.println("No BLE device to connect to");
        return false;
    }

    Serial.print("Attempting to connect to ");
    Serial.println(myDevice->getAddress().toString().c_str());

    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println("Created BLE Client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Attempt connection
    if (!pClient->connect(myDevice)) {
        Serial.println("Connection Failed");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("Connected to BLE Server");

    // Service discovery with more logging
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.print("Failed to find service UUID: ");
        Serial.println(serviceUUID.toString().c_str());
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("Found Primary Service");

    // Characteristic discovery
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.print("Failed to find characteristic UUID: ");
        Serial.println(charUUID.toString().c_str());
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("Found Primary Characteristic");

    // Notification setup for primary characteristic
    if(pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    // tilt
    // Obtain a reference to the service we are after in the remote BLE server.
    pRemoteService = pClient->getService(serviceUUID2);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      delete pClient;
      return false;
    }
    Serial.println(" - Found our service2");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic2 = pRemoteService->getCharacteristic(charUUID2);
    if (pRemoteCharacteristic2 == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID2.toString().c_str());
      pClient->disconnect();
      delete pClient;
      return false;
    }
    Serial.println(" - Found our characteristic2");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic2->canRead()) {

      std::string value = pRemoteCharacteristic2->readValue();
      std::copy(value.begin(), value.end(), std::begin(arr));
      Serial.print("grade or lock first read : ");
      handleBLENotification(arr, value.size(), 2);
    }

    if(pRemoteCharacteristic2->canNotify()) {
         pRemoteCharacteristic2->registerForNotify(notifyCallback2);
    }

    // Power
    BLERemoteService* pRemoteService2 = pClient->getService(serviceUUID3);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID3: ");
      Serial.println(serviceUUID3.toString().c_str());
      pClient->disconnect();
      delete pClient;
      return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic3 = pRemoteService2->getCharacteristic(charUUID3);
    if (pRemoteCharacteristic3 == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID3.toString().c_str());
      pClient->disconnect();
      delete pClient;
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic3->canRead()) {

      std::string value = pRemoteCharacteristic3->readValue();
      std::copy(value.begin(), value.end(), std::begin(arr));
      Serial.print("power first read : ");
      handleBLENotification(arr, value.size(), 3);
    }

    if(pRemoteCharacteristic3->canNotify()) {
         pRemoteCharacteristic3->registerForNotify(notifyCallback3);
    }
    // power

    connected = true;
    return true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 public:
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("BLE Device Found: ");
        Serial.print(advertisedDevice.getName().c_str());
        Serial.print(" | Address: ");
        Serial.print(advertisedDevice.getAddress().toString().c_str());
        Serial.print(" | RSSI: ");
        Serial.println(advertisedDevice.getRSSI());

        // Specifically look for KICKR BIKE
        if (advertisedDevice.getName() == "KICKR BIKE 1097" ||
            (advertisedDevice.haveServiceUUID() &&
             (advertisedDevice.isAdvertisingService(serviceUUID) ||
              advertisedDevice.isAdvertisingService(serviceUUID2) ||
              advertisedDevice.isAdvertisingService(serviceUUID3)))) {

            BLEDevice::getScan()->stop();

            // Clean up any existing device
            if (myDevice != nullptr) {
                delete myDevice;
            }

            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
            doScan = false;
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println(F("Starting Arduino BLE Client application..."));

    // Initialize display
    tft.init();
    tft.setRotation(3);  // Boot orientation
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);

    // WiFi Connection Phase
    tft.drawString("Connecting WiFi...", 0, 0, 2);
    Serial.println(F("Initializing WiFi"));

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long wifiStartTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 15000) {
        delay(500);
        Serial.print(".");
        tft.drawString("WiFi: Connecting", 0, 20, 2);
    }

    if (WiFi.status() == WL_CONNECTED) {
        tft.drawString("WiFi: Connected", 0, 20, 2);
        Serial.println(F("\nWiFi Connected"));
        Serial.print(F("IP Address: "));
        Serial.println(WiFi.localIP());
    } else {
        tft.drawString("WiFi: Failed", 0, 20, 2);
        Serial.println(F("\nWiFi Connection Failed"));
        WiFi.disconnect();
    }

    delay(2000);

    // BLE Initialization Phase
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Initializing BLE...", 0, 0, 2);
    Serial.println(F("Initializing BLE"));

    BLEDevice::init("");

    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);

    tft.drawString("Scanning for KICKR...", 0, 20, 2);
    pBLEScan->start(30, false);  // Longer scan duration

    tft.fillScreen(TFT_BLACK);
}

void loop() {
    // Connection logic
    if (doConnect) {
        if (connectToServer()) {
            Serial.println("Connected to KICKR BIKE");
            tft.setRotation(1);  // Normal operation rotation
            tft.fillScreen(TFT_BLACK);

            // Initialize gears
            frontgear = "0";
            reargear = "0";

            update_gear();
            doConnect = false;
        } else {
            Serial.println("Connection failed. Rescanning...");
            doScan = true;
        }
    }

    // Rescan if needed
    if (doScan) {
        BLEDevice::getScan()->start(10, false);
        doScan = false;
    }

    // Existing connected device logic
    if (connected) {
        // Your existing logic for sending data, updating display, etc.
        static unsigned long lastSauceUpdate = 0;
        unsigned long now = millis();

        if (now - lastSauceUpdate >= 300) {
            send_to_sauce();
            lastSauceUpdate = now;
        }

        // Update display if needed
        if (bikeState.isDirty) {
            updatedisp();
            bikeState.isDirty = false;
        }
    }

    delay(100);  // Prevent tight loop
}

void update_gear(void) {
    // Set correct rotation for gear display
    tft.setRotation(0);
    tft.drawString(frontgear, 10, 0, 7);
    // tft.drawString(":", 35, 0, 7);
    tft.fillRect(10, 50, tft.width() - 10, 100, TFT_BLACK);
    tft.drawString(reargear, 10, 50, 7);
}

void update_grade(void) {
    tft.setRotation(0);
    tft.drawString(grade, 10, 110, 4);
}

void update_power(void) {
    tft.setRotation(0);
    tft.drawString(power, 10, 130, 4);
}

void update_lock(void){
    static int prelock = -1;

    // Only update if the state has actually changed
    if (prelock == tilt_lock) return;

    tft.setRotation(0);
    tft.fillRect(70, 0, 10, 10, TFT_BLACK);

    if (tilt_lock) {
        tft.fillRect(70, 0, 10, 10, TFT_RED);
    } else {
        tft.fillRect(70, 0, 10, 10, TFT_BLUE);
    }

    prelock = tilt_lock;
}

void updatedisp(void) {
    tft.setRotation(0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    // Track if any update is actually needed
    bool anyUpdate = false;

    // Only update gear if front or rear gear has changed
    static String lastFrontGear = "";
    static String lastRearGear = "";
    if (frontgear != lastFrontGear || reargear != lastRearGear) {
        update_gear();
        lastFrontGear = frontgear;
        lastRearGear = reargear;
        anyUpdate = true;
    }

    // Only update grade if it has changed
    static String lastGrade = "";
    if (grade != lastGrade) {
        update_grade();
        lastGrade = grade;
        anyUpdate = true;
    }

    // Only update power if it has changed
    static String lastPower = "";
    if (power != lastPower) {
        update_power();
        lastPower = power;
        anyUpdate = true;
    }

    // Always update lock status
    update_lock();
}

void send_to_sauce(void){
  if(connectToWiFi()){
      if(!http.begin(wifiClient, serverName)) {
          displayConnectionStatus("HTTP", "Failed", TFT_RED);
          return;
      }

      http.addHeader("Content-Type", "application/json");

      String tilty = bikeState.tiltLock ? "true" : "false";
      String brakeState = "false";

      // Create gear config arrays
      String frontArray = "[";
      for (int i = 0; i < 3; i++) {
          if (i > 0) frontArray += ",";
          frontArray += String(gearConfig.frontTeeth[i]);
      }
      frontArray += "]";

      String rearArray = "[";
      for (int i = 0; i < 13 && gearConfig.rearTeeth[i] > 0; i++) {
          if (i > 0) rearArray += ",";
          rearArray += String(gearConfig.rearTeeth[i]);
      }
      rearArray += "]";

      String jsonPayload = "[\"self\",{"
                          "\"KICKRgear\":{\"cr\":\"" + frontgear + "\",\"gr\":\"" + reargear + "\"},"
                          "\"KICKRpower\":\"" + power + "\","
                          "\"KICKRgrade\":\"" + grade + "\","
                          "\"KICKRtiltLock\":\"" + tilty + "\","
                          "\"KICKRbrake\":\"" + brakeState + "\"";

      // Only add config if we have it
      if (gearConfig.isConfigured) {
          jsonPayload += ",\"KICKRconfig\":{\"type\":" + String(gearConfig.gearType) +
                        ",\"front\":" + frontArray +
                        ",\"rear\":" + rearArray + "}";
      }

      jsonPayload += "}]";

      int httpResponseCode = http.POST(jsonPayload);

      if (httpResponseCode > 0) {
          displayConnectionStatus("HTTP", "OK", TFT_GREEN);
      } else {
          displayConnectionStatus("HTTP", "Error", TFT_RED);
      }

      http.end();
  } else {
      displayConnectionStatus("HTTP", "No WiFi", TFT_YELLOW);
  }
}

bool connectToWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        displayConnectionStatus("WIFI", "Connected", TFT_GREEN);
        return true;
    }

    unsigned long now = millis();
    if (now - lastWiFiRetry < WIFI_RETRY_INTERVAL) return false;

    lastWiFiRetry = now;

    if (connectionRetries >= MAX_RETRIES) {
        connectionRetries = 0;
        WiFi.disconnect();
        delay(100);
        displayConnectionStatus("WIFI", "Failed", TFT_RED);
    }

    if (WiFi.status() != WL_CONNECTED) {
        displayConnectionStatus("WIFI", "Connecting", TFT_YELLOW);
        WiFi.begin(ssid, password);
        connectionRetries++;
    }

    return WiFi.status() == WL_CONNECTED;
}

static void changeCam(void){
  // Serial.println("Change Camera");
  if(connectToWiFi()){
      const char* endpoint;
      unsigned long now = millis();
      if (now - lastCamChange < CAM_CHANGE_DEBOUNCE) return;
      lastCamChange = now;

      switch(ACTIVECAM) {
        case '1': endpoint = brakeServer_camera_frnt; break;
        case '6': endpoint = brakeServer_camera_back; break;
        case '9': endpoint = brakeServer_camera_over; break;
        default: return;
      }

      http.begin(wifiClient, endpoint);
      http.setConnectTimeout(100);
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode  = http.GET();
      http.end();
  }

}

void displayConnectionStatus(const char* network, const char* status, uint16_t color) {
    static uint8_t yOffset = 220; // Bottom of the screen
    tft.setTextColor(color, TFT_YELLOW);
    tft.fillRect(0, yOffset, tft.width(), 20, TFT_BLACK);
    char statusText[30];
    snprintf(statusText, sizeof(statusText), "%s:%s", network, status);
    tft.drawString(statusText, 0, yOffset, 2);
}
