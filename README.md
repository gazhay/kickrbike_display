## This Fork

- Sketch updated for [Lilygo Dongle](https://www.lilygo.cc/products/t-dongle-s3)
- Added cycling power metric
- Relays all data to [sauce4zwift](https://www.sauce.llc/products/sauce4zwift/)

- Bonus - If you can pursuade Justin to allow a keypress solution in sauce you can switch cams with brake levers.
(Currently I ping my own local running server which does the camera switching)

## Data injected into sauce4zwift (into the "self" athelete)

```
{ "KICKRgear":{"cr":<frontgear>,"gr":<reargear>}, "KICKRpower":<power>, "KICKRgrade":<grade>,  "KICKRtiltLock":<tilt> }
```

You *must* configure these lines for your own use

```
const char* ssid = "<yourSSID>";
const char* password = "<yourPassword>";

const char* serverName = "http://<sauceServer>:1080/api/rpc/v1/updateAthleteData";
```

You *may* have to increase the timeout value here if your network is slow (value is ms)

```http.setConnectTimeout(100);```


# kickrbike_display
Wahoo Kickr Bike external display
Runs on LiLyGo T-Display.

* gear display
* grade(tilt) percentage display
* ~~chainrings/cassette display~~ (commented out on this version)
* lock / unlock display


enjoy~

# Recent arduino versions issues

It appears that recent versions of the board firmware and TFT lib are incompatible.
I can get them to compile and upload firmware with 

* esp32 v2.0.12
* TFT_eSPI v2.5.0

  You will also need Arduino IDE v2.2.1 (the latest will compile but TFT screen will be blank)

The issue is reported to [Lilygo](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/issues/26) but they don't seem to be monitoring or responding. 
Anyone with more knowledge is free to PR anything that may help, but I don't have the time to continue bug hunting.

