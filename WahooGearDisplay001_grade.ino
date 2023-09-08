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

const char* ssid = "<yourSSID>";
const char* password = "<yourPassword>";

const char* serverName = "http://<sauceServer>:1080/api/rpc/v1/updateAthleteData";
const char* brakeServer_camera_back = "http://<keypressServer>:8080/api/camback";
const char* brakeServer_camera_frnt = "http://<keypressServer>:8080/api/camfrnt";
const char* brakeServer_camera_over = "http://<keypressServer>:8080/api/camover";

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

String reargear = ("0");
String frontgear = ("0");
String grade = ("0.0");
String power = ("000");

char ACTIVECAM = '1';

int fg, rg;
uint8_t arr[32];
int tilt_lock = 1;
int negative = 0;
int brake = 0; //left = 1, right = 2, off = 0;

char LEFT = '9'; // overhead
char RIGHT = '6'; // backwards
char NONE = '1'; // Front cam

void calc_tilt(uint8_t *pData, size_t length){


  for (int i = 0; i < length; i++) {

    Serial.print(pData[i], HEX);
    Serial.print(" "); //separate values with a space
  }
  Serial.println("");

    //lock, unlock update
  if(length == 3 && pData[0] == 0xfd && pData[1] == 0x33) {
    tilt_lock = pData[2] == 0x01;
  }

  //grade update
  if(length == 4 && pData[0] == 0xfd && pData[1] == 0x34){
    char s[10] = {};
    if(pData[3] < 0x80)
    {
      negative = 0;
      float tmp = (float)(pData[3] << 8 | pData[2]) / 100;
      Serial.println(tmp);
      sprintf(s, "+ %.1f %%", tmp);
      grade = s;
      Serial.println(grade);
    }
    else
    {
      negative = 1;
      uint16_t tmp16 = 0xffff - (pData[3] << 8 | pData[2]);
      float tmp = (float)tmp16 / 100;
      Serial.println(tmp16);
      Serial.println(tmp);
      sprintf(s, "- %.1f %%",tmp);
      grade = s;
      Serial.println(grade);
    }
  }

}


static void notifyCallback2(
  BLERemoteCharacteristic* pBLERemoteCharacteristic2,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  // Serial.print("noty2 : ");
  calc_tilt(pData, length);
}

static void notifyCallback3(
  BLERemoteCharacteristic* pBLERemoteCharacteristic3,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  // Serial.print("noty3 : ");
  // calc_tilt(pData, length);
  uint16_t tmp16 = (pData[3] << 8 | pData[2]);
  power = (int)(tmp16);
  // updatedisp();
}

static void changeCam(void){
  // Serial.println("Change Camera");
  if(WiFi.status()== WL_CONNECTED){
      WiFiClient client;
      HTTPClient http;

      String urlstring = "";

      if (ACTIVECAM=='1'){
        http.begin(client, brakeServer_camera_frnt);
      } else if (ACTIVECAM=='6'){
        http.begin(client, brakeServer_camera_back);
      } else if (ACTIVECAM=='9'){
        http.begin(client, brakeServer_camera_over);
      }
      // Serial.print("Changing camera to ");
      // Serial.println(ACTIVECAM);
      http.setConnectTimeout(100);
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode  = http.GET();
      http.end();
  }

}

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  if (pData[0]==7){
    // Status packet
    frontgear = (1+pData[2]);
    reargear = (1+pData[3]);

    fg = (int)(1+pData[2]);
    rg = (int)(1+pData[3]);
  } else if (pData[0]==2){
    // Brake lever
    char before = ACTIVECAM;
    if (pData[2]==90){
      Serial.println("Left Shifter depress");
      if (ACTIVECAM==LEFT){
        ACTIVECAM = NONE;
      } else {
        ACTIVECAM = LEFT;
      }
    } else if (pData[2]==227){
      Serial.println("Right Shifter depress");
      if (ACTIVECAM==RIGHT){
        ACTIVECAM = NONE;
      } else {
        ACTIVECAM = RIGHT;
      }
    } else {
      Serial.println("Shifter release");
    }
    if (ACTIVECAM!=before){
      changeCam();
    }
    Serial.print("Current Cam ");
    Serial.println(ACTIVECAM);
  } else {//if (pData[0]==1){
    // shifter
    frontgear = (1+pData[2]);
    reargear = (1+pData[3]);

    fg = (int)(1+pData[2]);
    rg = (int)(1+pData[3]);
  }

  updatedisp();
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
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());

    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteCharacteristic->canNotify())
    {
         pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    // tilt
    // Obtain a reference to the service we are after in the remote BLE server.
    pRemoteService = pClient->getService(serviceUUID2);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service2");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic2 = pRemoteService->getCharacteristic(charUUID2);
    if (pRemoteCharacteristic2 == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID2.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic2");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic2->canRead()) {

      std::string value = pRemoteCharacteristic2->readValue();
      std::copy(value.begin(), value.end(), std::begin(arr));
      Serial.print("grade or lock first read : ");
      calc_tilt(arr, value.size());
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
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic3 = pRemoteService2->getCharacteristic(charUUID3);
    if (pRemoteCharacteristic3 == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID3.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");


    // Read the value of the characteristic.
    if(pRemoteCharacteristic3->canRead()) {

      std::string value = pRemoteCharacteristic3->readValue();
      std::copy(value.begin(), value.end(), std::begin(arr));
      Serial.print("power first read : ");
      uint16_t tmp16 = (arr[3] << 8 | arr[2]);
      power = (int)(tmp16);
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
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
  tft.fillRect(0, 48, 10, 10, TFT_GREEN);

  tft.init();
  tft.setRotation(1);

  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  tft.drawString("WAHOO", 0, 0, 4);
  tft.drawString("Dataviz", 0, 23, 4);
  // tft.drawString("Connecting....", 0, 48, 4);
  tft.fillRect(0, 58, 10, 10, TFT_RED);

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.


// This is the Arduino main loop function.
void loop() {

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      tft.fillRect(0, 58, 10, 10, TFT_GREEN);
      tft.setRotation(0);
      Serial.println("We are now connected to the BLE Server.");
      tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
      //tft.drawXBitmap(155, 0, zwiftlogo, 100, 100, TFT_BLACK, TFT_ORANGE);
      tft.fillRect(0, 0, 5, 5, TFT_SKYBLUE);
      tft.fillScreen(TFT_BLACK);
      //tft.drawXBitmap(155, 0, zwiftlogo, 100, 100, TFT_BLACK, TFT_ORANGE);
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
      String newValue = "Time since boot: " + String(millis()/1000);
  //  Serial.println("Setting new characteristic value to \"" + newValue + "\"");

     // Set the characteristic's value to be the array of bytes that is actually a string.
    pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
  }else if(doScan){
    BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  delay(1000); // Delay a second between loops.
  // Serial.println("End of loop delay."+doConnect);
} // End of loop


void update_front(void)
{
  static int pre_pos = -1;
  if(pre_pos == fg) return;
  if(fg == 1) {
    tft.fillRect(10, 70, 5, 50, TFT_GREEN);
    tft.fillRect(20, 60, 5, 70, TFT_BLACK);
    tft.drawRect(20, 60, 5, 70, TFT_WHITE);
  }
  else{
    tft.fillRect(10, 70, 5, 50, TFT_BLACK);
    tft.drawRect(10, 70, 5, 50, TFT_WHITE);
    tft.fillRect(20, 60, 5, 70, TFT_GREEN);
  }
  pre_pos = fg;
}

void clear_cassette(int i)
{
  i--;
   tft.fillRect(40 + i*10, 60 + i*2, 5, 70 - i*4, TFT_BLACK);
   tft.drawRect(40 + i*10, 60 + i*2, 5, 70 - i*4, TFT_WHITE);
}

void fill_cassette(int i)
{
  i--;
  tft.fillRect(40 + i*10, 60 + i*2, 5, 70 - i*4, TFT_GREEN);
}
void update_rear(void)
{
  static int pre_rg = -1;
  if(pre_rg == -1)
  {
    for(int i = 0 ; i < 12 ; i++)
      tft.drawRect(40 + i*10,60 + i*2,5,70 - i*4, TFT_WHITE);
    pre_rg = 3;
  }
  clear_cassette(pre_rg);
  fill_cassette(rg);
  pre_rg = rg;
}

void update_gear(void){
  tft.drawString(frontgear, 10, 0, 7);
  // tft.drawString(":", 35, 0, 7);
  tft.fillRect(10,50, tft.width() - 10,100,TFT_BLACK);
  tft.drawString(reargear, 10, 50, 7);
}

void update_grade(void) {
  tft.drawString(grade, 10, 110, 4);
}

void update_power(void){
  tft.drawString(power, 10, 130, 4);
}

void update_lock(void)
{
  static int prelock = -1;
  if(prelock == tilt_lock)
    return;

  tft.fillRect(70, 00, 10, 10, TFT_BLACK);
  tft.setSwapBytes(true);
  if(tilt_lock)
    tft.fillRect(70, 00, 10, 10, TFT_RED);
    // tft.pushImage(180, 60, 64, 64, lock64);
  else
    tft.fillRect(70, 00, 10, 10, TFT_BLUE);
  // tft.pushImage(180, 60, 64, 64, unlock64);

   prelock = tilt_lock;
}

void send_to_sauce(void){
  if(WiFi.status()== WL_CONNECTED){
      WiFiClient client;
      HTTPClient http;

      // Your Domain name with URL path or IP address with path
      http.begin(client, serverName);
      // if (! http.begin(client, serverName)) {
        // Serial.println("No sauce!");
      // } else {
      http.setConnectTimeout(100);
      Serial.println("posting sauce!");
      http.addHeader("Content-Type", "application/json");
      String tilty = String(tilt_lock);
      String brakeState = String(brake);
      int httpResponseCode  = http.POST("[\"self\",{ \"KICKRgear\":{\"cr\":\""+frontgear+"\",\"gr\":\""+reargear+"\"}, \"KICKRpower\":\""+power+"\", \"KICKRgrade\":\""+grade+"\",  \"KICKRtiltLock\":\""+tilty+"\", \"KICKRbrake\":\""+brakeState+"\" }]");
      http.end();
      delay(300);
  }
}

void updatedisp(){
  update_gear();
  update_grade();
  // update_front();
  // update_rear();
  update_lock();
  update_power();
  send_to_sauce();
}
