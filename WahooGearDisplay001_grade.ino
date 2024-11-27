/**
* An external gear display for the Wahoo Kickr Bike
* On the LiLiGo T-Display
* Based on the Arduino BLE Example
* v2.1 fixing some bugs with 2.0
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
static const char PROGMEM WIFI_CONNECTED[]  = "Connected to WiFi network with IP Address: ";
static const char PROGMEM BLE_STARTING[]    = "Starting Arduino BLE Client application...";

// Display update control
static unsigned long lastDisplayUpdate             = 0;
static const unsigned long DISPLAY_UPDATE_INTERVAL = 100; // 10 fps
static bool needsGearUpdate                        = false;
static bool needsGradeUpdate                       = false;
static bool needsPowerUpdate                       = false;

// Connection management
static unsigned long lastWiFiRetry             = 0;
static const unsigned long WIFI_RETRY_INTERVAL = 5000;
static unsigned long lastBLERetry              = 0;
static const unsigned long BLE_RETRY_INTERVAL  = 3000;
static uint8_t connectionRetries               = 0;
static const uint8_t MAX_RETRIES               = 5;

// HTTP client reuse
static HTTPClient http;
static WiFiClient wifiClient;

// Camera control
static unsigned long lastCamChange             = 0;
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


const char* ssid = "<yourSSID>";
const char* password = "<yourPassword>";

const char* serverName = "http://<sauceServer>:1080/api/rpc/v1/updateAthleteData";
const char* brakeServer_camera_back = "http://<keypressServer>:8080/api/camback";
const char* brakeServer_camera_frnt = "http://<keypressServer>:8080/api/camfrnt";
const char* brakeServer_camera_over = "http://<keypressServer>:8080/api/camover";

// Global variables for connection management
bool deviceConnected                          = false;
unsigned long lastConnectionAttempt           = 0;
const unsigned long CONNECTION_RETRY_INTERVAL = 5000; // 5 seconds between connection attempts
const int MAX_CONNECTION_ATTEMPTS             = 10;
int connectionAttempts                        = 0;

unsigned long lastSauceUpdate = 0;  // Add this near other global variable declarations

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

// The remote service we wish to connect to.
static BLEUUID serviceUUID("a026ee0d-0a7d-4ab3-97fa-f1500f9feb8b");  // Gear Service
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("a026e03a-0a7d-4ab3-97fa-f1500f9feb8b");  // Gear Notify Characteristic

//for grade
// The remote service we wish to connect to.
static BLEUUID serviceUUID2("a026ee0b-0a7d-4ab3-97fa-f1500f9feb8b"); // Grade Service
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID2("a026e037-0a7d-4ab3-97fa-f1500f9feb8b"); // Grade Characteristic

// Power service
static BLEUUID serviceUUID3("00001818-0000-1000-8000-00805f9b34fb"); // Cycling Power Service
static BLEUUID    charUUID3("00002a63-0000-1000-8000-00805f9b34fb"); // Cycling Power Measurement

// Gear Configuration Service and Characteristic UUIDs
static BLEUUID gearConfigServiceUUID("a026ee0d-0a7d-4ab3-97fa-f1500f9feb8b");
static BLEUUID gearConfigCharUUID("a026e039-0a7d-4ab3-97fa-f1500f9feb8b");

// Global variables for gear configuration
String front_teeth_config  = "[0,0,0]";
String rear_teeth_config   = "[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]";
bool needsGearConfigUpdate = false;

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* pRemoteCharacteristic2;
static BLERemoteCharacteristic* pRemoteCharacteristic3;
static BLERemoteCharacteristic* pRemoteCharacteristic4;
static BLEAdvertisedDevice* myDevice;
static uint8_t bleConnectionRetries = 0;
static const uint8_t MAX_BLE_RETRIES = 3;

String reargear  = "0";
String frontgear = "0";
String grade     = "0.0%";
String power     = "000";
char ACTIVECAM   = '1';

int fg, rg;
uint8_t arr[32];
int tilt_lock = 1;
int negative  = 0;
int brake     = 0; //left = 1, right = 2, off = 0;

char LEFT     = '9'; // overhead
char RIGHT    = '6'; // backwards
char NONE     = '1'; // Front cam

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
                // Convert grade to string with one decimal place and % sign
                char gradeStr[8];
                dtostrf(bikeState.grade, 4, 1, gradeStr);
                strcat(gradeStr, "%");
                grade = String(gradeStr);
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
                // Convert power to 3-digit string with leading zeros
                char powerStr[4];
                snprintf(powerStr, sizeof(powerStr), "%03d", bikeState.power);
                power = String(powerStr);
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

static void gearConfigNotifyCallback(
    BLERemoteCharacteristic* pBLERemoteCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify) {

    Serial.println("========== GEAR CONFIG NOTIFICATION RECEIVED ==========");
    Serial.print("Notification Length: ");
    Serial.println(length);

    // Hex dump of entire notification
    Serial.print("Full Notification HEX: ");
    for (size_t i = 0; i < length; i++) {
        if (pData[i] < 0x10) Serial.print("0");
        Serial.print(pData[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Detailed logging of first few bytes
    Serial.println("Detailed Byte Breakdown:");
    for (size_t i = 0; i < min(length, (size_t)10); i++) {
        Serial.print("Byte ");
        Serial.print(i);
        Serial.print(": 0x");
        if (pData[i] < 0x10) Serial.print("0");
        Serial.print(pData[i], HEX);
        Serial.print(" (Dec: ");
        Serial.print(pData[i]);
        Serial.println(")");
    }

    // Decode the gear configuration
    decodeGearConfig(pData, length);

    Serial.println("========== END GEAR CONFIG NOTIFICATION ==========");
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

void printAllServices(BLEClient* pClient) {
    if (pClient == nullptr) {
        Serial.println("Client is null - cannot print services");
        return;
    }

    std::map<std::string, BLERemoteService*>* services = pClient->getServices();
    if (services == nullptr) {
        Serial.println("No services found");
        return;
    }

    Serial.println("Available Services:");
    for (auto& service : *services) {
        BLEUUID uuid = service.second->getUUID();
        Serial.print(" - Service UUID: ");
        Serial.println(uuid.toString().c_str());
    }
}

bool connectToServer() {
    if (!myDevice) {
        Serial.println("No BLE device to connect to");
        return false;
    }

    // Create a new client
    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient) {
        Serial.println("Failed to create BLE client");
        return false;
    }
    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server
    if (!pClient->connect(myDevice)) {
        Serial.println("Failed to connect to BLE Server");
        delete pClient;
        return false;
    }
    Serial.println("Connected to BLE Server");

    // DEBUG: Print all available services
    printAllServices(pClient);

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

    // Secondary service and characteristic
    BLERemoteService* pRemoteService2 = pClient->getService(serviceUUID2);
    if (pRemoteService2 == nullptr) {
        Serial.print("Failed to find service2 UUID: ");
        Serial.println(serviceUUID2.toString().c_str());
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println(" - Found our service2");

    pRemoteCharacteristic2 = pRemoteService2->getCharacteristic(charUUID2);
    if (pRemoteCharacteristic2 == nullptr) {
        Serial.print("Failed to find characteristic2 UUID: ");
        Serial.println(charUUID2.toString().c_str());
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println(" - Found our characteristic2");

    // Grade or Lock service
    BLERemoteService* pRemoteService3 = pClient->getService(serviceUUID3);
    if (pRemoteService3 == nullptr) {
        Serial.println("grade or lock first read : Failed to find service");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("grade or lock first read :  - Found our service");

    pRemoteCharacteristic3 = pRemoteService3->getCharacteristic(charUUID3);
    if (pRemoteCharacteristic3 == nullptr) {
        Serial.println("grade or lock first read :  - Failed to find characteristic");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("grade or lock first read :  - Found our characteristic");

    // Notifications setup
    if(pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    if(pRemoteCharacteristic2->canNotify()) {
        pRemoteCharacteristic2->registerForNotify(notifyCallback2);
    }

    if(pRemoteCharacteristic3->canNotify()) {
         pRemoteCharacteristic3->registerForNotify(notifyCallback3);
    }

    // Gear Configuration Service
    BLERemoteService* pGearConfigService = pClient->getService(gearConfigServiceUUID);
    if (pGearConfigService == nullptr) {
        Serial.println(" - Gear config service not found");
        // Continue connection even if gear config service is missing
    } else {
        Serial.println(" - Found gear config service");

        pRemoteCharacteristic4 = pGearConfigService->getCharacteristic(gearConfigCharUUID);
        if (pRemoteCharacteristic4 == nullptr) {
            Serial.println(" - Gear config characteristic not found");
        } else {
            Serial.println(" - Found gear config characteristic");

            // Register for notifications
            if(pRemoteCharacteristic4->canNotify()) {
                pRemoteCharacteristic4->registerForNotify(gearConfigNotifyCallback);
            }
        }
    }

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
              advertisedDevice.isAdvertisingService(serviceUUID3) ||
              advertisedDevice.isAdvertisingService(gearConfigServiceUUID)))) {

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

    // tft.fillScreen(TFT_BLACK);
}
int lastGCUpdate = 3601;

void loop() {
    // Connection logic
    if (doConnect) {
        if (connectToServer()) {
            Serial.println("Connected to KICKR BIKE");
            tft.setRotation(1);  // Normal operation rotation
            tft.fillScreen(TFT_BLACK);

            // Initialize gears
            frontgear = "0";
            reargear  = "0";

            update_gear();
            doConnect = false;

            if (lastGCUpdate++>3600){
                // if loop takes 1s = 1 hour.
                // Attempt to query gear config shortly after connection
                if (pRemoteCharacteristic4 != nullptr) {
                    delay(500);  // Short delay to ensure stability
                    queryGearConfig(pRemoteCharacteristic4, 0x03);  // Front gears
                    delay(200);
                    queryGearConfig(pRemoteCharacteristic4, 0x04);  // Rear gears
                }
                lastGCUpdate=0;
            }

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
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(frontgear, 10, 0, 7);
    // tft.drawString(":", 35, 0, 7);
    tft.fillRect(10, 50, tft.width() - 10, 100, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(reargear, 10, 50, 7);
}

void update_grade(void) {
    tft.setRotation(0);
    tft.fillRect(10, 110, 100, 20, TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(grade, 10, 110, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void update_power(void) {
    tft.setRotation(0);
    tft.fillRect(10, 130, 100, 20, TFT_BLACK);
    if      (power.toInt()<100){  tft.setTextColor(TFT_WHITE  , TFT_BLACK); }
    else if (power.toInt()<250) { tft.setTextColor(TFT_YELLOW , TFT_BLACK); }
    else if (power.toInt()<500) { tft.setTextColor(TFT_RED    , TFT_BLACK); }
    else {                        tft.setTextColor(TFT_MAGENTA, TFT_BLACK); }
    tft.drawString(power, 10, 130, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
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
        tft.fillRect(70, 0, 10, 10, TFT_WHITE);
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
        lastRearGear  = reargear;
        anyUpdate     = true;
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
      // Reuse the global HTTP client
      if(!http.begin(wifiClient, serverName)) {
          Serial.println("HTTP Begin failed");
          displayConnectionStatus("HTTP", "Failed", TFT_RED);
          return;
      }

      // Add headers
      http.addHeader("Content-Type", "application/json");

      // Format the tilt lock state
      String tilty = bikeState.tiltLock ? "true" : "false";
      String brakeState = "false"; // Default brake state if needed

      // Construct the JSON payload in the expected format
      String jsonPayload = "[\"self\",{"
                          "\"KICKRgear\":{\"cr\":\"" + frontgear + "\",\"gr\":\"" + reargear + "\"},"
                          "\"KICKRconfig\":{\"front\":" + front_teeth_config + ",\"rear\":" + rear_teeth_config + "},"
                          "\"KICKRpower\":\"" + power + "\","
                          "\"KICKRgrade\":\"" + grade + "\","
                          "\"KICKRtiltLock\":\"" + tilty + "\","
                          "\"KICKRbrake\":\"" + brakeState + "\""
                          "}]";

      Serial.print("Sending to sauce: ");
      Serial.println(jsonPayload);

      // Send POST request
      int httpResponseCode = http.POST(jsonPayload);

      if (httpResponseCode > 0) {
          Serial.print("HTTP Response code: ");
          Serial.println(httpResponseCode);
          // String response = http.getString();
          // Serial.println(response);
          displayConnectionStatus("HTTP", "OK", TFT_GREEN);
      } else {
          Serial.print("Error code: ");
          Serial.println(httpResponseCode);
          displayConnectionStatus("HTTP", "Error", TFT_RED);
      }

      // Clean up
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

void queryGearConfig(BLERemoteCharacteristic* pChar, uint8_t gearType) {
    if (pChar == nullptr) {
        Serial.println("Error: Characteristic is null for gear config query");
        return;
    }

    // EXACT query bytes from Python script
    uint8_t queryBytes[] = {0x01, gearType};

    Serial.print("Sending PRECISE gear config query for type: 0x");
    Serial.println(gearType, HEX);

    pChar->writeValue(queryBytes, sizeof(queryBytes));
}

void decodeGearConfig(uint8_t* pData, size_t length) {
    // ULTRA VERBOSE DEBUG LOGGING
    Serial.println("==== GEAR CONFIG DECODE START ====");
    Serial.print("Total Bytes Received: ");
    Serial.println(length);

    // Print FULL hex dump
    Serial.print("HEX DUMP: ");
    for (size_t i = 0; i < length; i++) {
        if (pData[i] < 0x10) Serial.print("0"); // Add leading zero for single-digit hex
        Serial.print(pData[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Detailed byte-by-byte logging
    Serial.println("Byte-by-Byte Analysis:");
    for (size_t i = 0; i < length; i++) {
        Serial.print("Byte ");
        Serial.print(i);
        Serial.print(": 0x");
        if (pData[i] < 0x10) Serial.print("0"); // Add leading zero for single-digit hex
        Serial.print(pData[i], HEX);
        Serial.print(" (Dec: ");
        Serial.print(pData[i]);
        Serial.println(")");
    }

    // Sanity checks
    if (length < 8) {
        Serial.println("ERROR: Insufficient data for gear config - ABORT");
        return;
    }

    // Detailed type checking
    Serial.print("Packet Type Byte (index 3): 0x");
    Serial.println(pData[3], HEX);

    // Front Gear Configuration
    if (pData[3] == 0x83) {
        Serial.println("FRONT GEAR CONFIGURATION DETECTED");
        String frontConfig = "[";
        bool firstGear = true;

        // Explicitly log potential front gear teeth
        Serial.println("Front Gear Teeth Candidates:");
        for (int i = 0; i < 3; i++) {
            uint8_t gearTeeth = pData[5+i];
            Serial.print("Potential Front Gear ");
            Serial.print(i);
            Serial.print(": ");
            Serial.println(gearTeeth);

            if (gearTeeth != 0) {
                if (!firstGear) frontConfig += ",";
                frontConfig += String(gearTeeth);
                firstGear = false;
            }
        }
        frontConfig += "]";

        front_teeth_config = frontConfig;
        Serial.print("DECODED Front Gear Config: ");
        Serial.println(front_teeth_config);
        needsGearConfigUpdate = true;
    }
    // Rear Gear Configuration
    else if (pData[3] == 0x84) {
        Serial.println("REAR GEAR CONFIGURATION DETECTED");
        String rearConfig = "[";
        bool firstGear = true;

        // Explicitly log potential rear gear teeth
        Serial.println("Rear Gear Teeth Candidates:");
        for (int i = 4; i < length; i++) {
            uint8_t gearTeeth = pData[i];

            Serial.print("Potential Rear Gear ");
            Serial.print(i-4);
            Serial.print(": ");
            Serial.println(gearTeeth);

            if (gearTeeth == 0) break;

            if (!firstGear) rearConfig += ",";
            rearConfig += String(gearTeeth);
            firstGear = false;
        }
        rearConfig += "]";

        rear_teeth_config = rearConfig;
        Serial.print("DECODED Rear Gear Config: ");
        Serial.println(rear_teeth_config);
        needsGearConfigUpdate = true;
    }
    else {
        Serial.print("UNRECOGNIZED Gear Config Packet Type: 0x");
        Serial.println(pData[3], HEX);
    }

    Serial.println("==== GEAR CONFIG DECODE END ====");
}

void queryGearConfigAfterConnection() {
    if (!connected || pRemoteCharacteristic4 == nullptr) {
        Serial.println("Cannot query gear config - not connected or no characteristic");
        return;
    }

    Serial.println("Querying Gear Configuration AFTER Connection");

    // Query front gears
    queryGearConfig(pRemoteCharacteristic4, 0x03);
    delay(200);

    // Query rear gears
    queryGearConfig(pRemoteCharacteristic4, 0x04);
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
