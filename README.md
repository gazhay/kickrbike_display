## Complete rework version 2

- Rewrote most of the wifi and BLE connection code to make it more robust
- Improvements to on dongle display
- Improvements to sauce injection
- Added a query to the bike to get the teeth numbers of each ring.
- Inject the teeth into the athlete data on sauce.

- You still need to compile with very specific libs for arduino see notes below.

## This Fork

- Sketch updated for [Lilygo Dongle](https://www.lilygo.cc/products/t-dongle-s3)
- Added cycling power metric
- Relays all data to [sauce4zwift](https://www.sauce.llc/products/sauce4zwift/)

- Bonus - If you can pursuade Justin to allow a keypress solution in sauce you can switch cams with brake levers.
(Currently I ping my own local running server which does the camera switching)

## Data injected into sauce4zwift (into the "self" athelete)

```
{
  "KICKRgear":{"cr":<frontgear>,"gr":<reargear>},
  "KICKRpower":<powerInWatts>,
  "KICKRgrade":<inclineOfBikeIn%>,
  "KICKRtiltLock":<tiltLockBoolean>,
  "KICKRbrake":<BrakeInfo>,
  "KICKRconfig":{
    "front":[30,39,50], // chainring teeth counts
    "rear":[21,19,18,17,16,15,14,13,12,11] // rear cog teethcounts
  }
}
```

You *must* configure these lines for your own use

```
const char* ssid = "<yourSSID>";
const char* password = "<yourPassword>";

const char* serverName = "http://<sauceServer>:1080/api/rpc/v1/updateAthleteData";
```


# kickrbike_display
Wahoo Kickr Bike external display
Runs on LiLyGo T-Display.

* gear display
* grade(tilt) percentage display
* ~~chainrings/cassette display~~ (commented out on this version)
* lock / unlock display

* now with gear teeth counts too


enjoy~

# Recent arduino versions issues

It appears that recent versions of the board firmware and TFT lib are incompatible.
I can get them to compile and upload firmware with

* esp32 v2.0.12
* TFT_eSPI v2.5.0

  You will also need Arduino IDE v2.2.1 (the latest will compile but TFT screen will be blank)

The issue is reported to [Lilygo](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/issues/26) but they don't seem to be monitoring or responding.
Anyone with more knowledge is free to PR anything that may help, but I don't have the time to continue bug hunting.
