# arcwatch
A simple daemon to log and mail events of a Areca Raid Controller.

List of features (as i said, this is a small and simple tool ;-)):
 * List all events
 * Log new events to syslog
 * Mail new events via sendmail
 * fork into background

## install

Get the current areca API arclib from [ftp://ftp.areca.com.tw/RaidCards/AP_Drivers/API](ftp://ftp.areca.com.tw/RaidCards/AP_Drivers/API/Arclib_Build350_20150519.zip) or [https://areca.starline.de/API/](https://areca.starline.de/API/Arclib_Build350_20150519.zip)

### compile
```
g++ -static -I Arclib/include -o arcwatch arcwatch.cpp Arclib/linux/x86-64/release/arclib64.a -lpthread -Wall
```
