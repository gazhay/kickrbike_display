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
