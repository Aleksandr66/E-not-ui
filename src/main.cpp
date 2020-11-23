#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
////MQTT////
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
////////////
#include <functions.h>
#define ONE_WIRE_BUS D5 ////D4////D4////
OneWire oneWire(ONE_WIRE_BUS);
// передаем объект oneWire объекту DS18B20:
DallasTemperature DS18B20(&oneWire);
char temperatureCString[6];
char temperatureFString[6];

#include <LittleFS.h>

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Rtc_Pcf8563.h>

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;

Rtc_Pcf8563 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

uint32_t beep = 1;
int old_n_beep = -1;
int n_beep;
//unsigned long currentMillis = 0;
uint32_t last, currentMillis, ota_ws, one_sec, ap_sec, sta_rec, bloonk, lastReconnectAttempt;
int num_beep = 0;
bool ws_enable = false;
bool now = false;
bool shouldReboot = false;
bool on_rec = false;
String json;
String encrypt = "";
int retry;

WiFiEventHandler stationConnectedHandler;
WiFiEventHandler stationDisconnectedHandler;
WiFiEventHandler probeRequestPrintHandler;
WiFiEventHandler probeRequestBlinkHandler;
bool blinkFlag;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    ws_enable = true;
    now = true;
    //client->printf("Hello Client %u :)", client->id());
    client->ping();
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
    ws_enable = false;
  }
  else if (type == WS_EVT_ERROR)
  {
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_PONG)
  {
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
  }
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg = "";
    if (info->final && info->index == 0 && info->len == len)
    {
      //the whole message is in a single frame and we got all of it's data

      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

      if (info->opcode == WS_TEXT)
      {
        for (size_t i = 0; i < info->len; i++)
        {
          msg += (char)data[i];
        }
      }
      else
      {
        char buff[3];
        for (size_t i = 0; i < info->len; i++)
        {
          sprintf(buff, "%02x ", (uint8_t)data[i]);
          msg += buff;
        }
      }
      /*
      Serial.printf("%s\n", msg.c_str());
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, msg);
      const char *n_ssid = doc["new_wifi"];
      const char *n_pass = doc["new_pass"];
      //Serial.println(sensor);
      //WiFi.persistent(false);
      // WiFi.disconnect();
      //WiFi.persistent(true);
      Serial.println("Set reconnect " + String(WiFi.setAutoReconnect(true)));
      Serial.println("Wifi begin " + String(WiFi.begin(n_ssid, n_pass)));
      Serial.println("Reconnect " + String(WiFi.reconnect()));
      delay(500);
*/
      if (info->opcode == WS_TEXT)
        Serial.println("I got your text message"); // client->text("I got your text message");
      else
        Serial.println("I got your binary message"); // client->binary("I got your binary message");
    }
    else
    {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0)
      {
        if (info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);

      if (info->opcode == WS_TEXT)
      {
        for (size_t i = 0; i < len; i++)
        {
          msg += (char)data[i];
        }
      }
      else
      {
        char buff[3];
        for (size_t i = 0; i < len; i++)
        {
          sprintf(buff, "%02x ", (uint8_t)data[i]);
          msg += buff;
        }
      }
      Serial.printf("%s\n", msg.c_str());

      if ((info->index + len) == info->len)
      {
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if (info->final)
        {
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          if (info->message_opcode == WS_TEXT)
            Serial.println(); //client->text("I got your text message");
          else
            Serial.println(); //client->binary("I got your binary message");
        }
      }
    }
  }
}

class CaptiveRequestHandler : public AsyncWebHandler
{
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request)
  {
    //request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html><head><title>Название</title></head><body>");
    response->print("<p>Это Ваше Устройство.</p>");
    response->printf("<p>Вы попытались зайти на этот сайт: http://%s%s</p>", request->host().c_str(), request->url().c_str());
    response->printf("<p>Перейдите <a href='http://%s'>по этой</a> ссылке</p>", WiFi.softAPIP().toString().c_str());
    response->print("</body></html>");
    //request->send(response);
    request->send(LittleFS, "/index.html");
  }
};

//////////////////////

//////////////////////
void i2cscan()
{
  byte error, address;
  int nDevices;

  Serial.println("Scanning...");

  nDevices = 0;
  for (address = 1; address < 127; address++)
  {

    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");

      nDevices++;
    }
    else if (error == 4)
    {
      Serial.print("Unknow error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");

  delay(500); // wait 5 seconds for next scan
}
/////////////////////
void printValues()
{
  Serial.print("Temperature = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");

  Serial.print("Pressure = ");

  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");

  Serial.print("Approx. Altitude = ");
  Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
  Serial.println(" m");

  Serial.print("Humidity = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");

  //Serial.println();
}
/////////////////////////////
String macToString(const unsigned char *mac)
{
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}
/////////////////////////////
void onStationConnected(const WiFiEventSoftAPModeStationConnected &evt)
{
  Serial.print("Station connected: ");
  Serial.println(macToString(evt.mac));
}

void onStationDisconnected(const WiFiEventSoftAPModeStationDisconnected &evt)
{
  Serial.print("Station disconnected: ");
  Serial.println(macToString(evt.mac));
  ws_enable = false;
}

void onProbeRequestPrint(const WiFiEventSoftAPModeProbeRequestReceived &evt)
{
  Serial.print("Probe request from: ");
  Serial.print(macToString(evt.mac));
  Serial.print(" RSSI: ");
  Serial.println(evt.rssi);
}

void onProbeRequestBlink(const WiFiEventSoftAPModeProbeRequestReceived &)
{
  // We can't use "delay" or other blocking functions in the event handler.
  // Therefore we set a flag here and then check it inside "loop" function.
  blinkFlag = true;
}
/////////////////////////////
void onSTAConnected(WiFiEventStationModeConnected ipInfo)
{
  Serial.printf("Connected to %s\r\n", ipInfo.ssid.c_str());
  beep = 50;
  n_beep = 0;
}

bool off_ap = false;

void onSTAGotIP(WiFiEventStationModeGotIP ipInfo)
{
  String new_ip = ipInfo.ip.toString();
  String new_ssid = WiFi.SSID();
  Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
  Serial.printf("Connected: %s\r\n", WiFi.status() == WL_CONNECTED ? "yes" : "no");
  json = "{\"notif\": {\"text\":\"My <a href='ya.ru'>NEW</a> IP" + new_ip + " ssid: " + new_ssid + "\",\"duration\": 120000,\"type\":\"info\"}}";
  ws.printfAll(json.c_str());
  off_ap = true;
  ap_sec = millis();
  retry = 0;
  beep = 0;
  //WiFi.softAPdisconnect(true);
  //_wifi_connected = true;
}

void onSTADisconnected(WiFiEventStationModeDisconnected event_info)
{
  if (retry > 9)
  {
    //WiFi.setAutoReconnect(false);
    wifi_station_set_reconnect_policy(false);
    sta_rec = millis();
    on_rec = true;
    beep = 2000;
    n_beep = 1;
    if (WiFi.getMode() != 3)
    {
      Serial.println("Start softAp");
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("Horizont");
    }
    else
    {
      Serial.println("softAP резервный уже запущен");
    }
  }
  else
  {

    retry++;
    beep = 500;
    n_beep = 2;
    Serial.printf("Retry: %d\n", retry);
    Serial.printf("Disconnected from SSID: %s\n", event_info.ssid.c_str());
    Serial.printf("Reason: %d\n", event_info.reason);
    json = "{\"notif\": {\"text\":\"Reason: " + String(event_info.reason) + "\",\"duration\": 5000,\"type\":\"warning\"}}";
    ws.printfAll(json.c_str());
  }
  //_wifi_connected = false;
}

void onWFchange(WiFiEventModeChange event_info)
{
  Serial.println("Change#####################################");
  Serial.println(String(event_info.newMode));
  Serial.println(String(event_info.oldMode));
}
/////////////////////////////
void test()
{
  Serial.print("Yeah");
}
/////////////////////////////
int pwmk;
void pwmka(){
  if(millis()-pwmk>10){

  }
}
/////////////////////////////
void blink(uint32_t mode, int beepp)
{
  if (beepp != old_n_beep)
  {
    old_n_beep = beepp;
    digitalWrite(D4, HIGH);
  }
  if (mode > 1)
  {
    if (beepp > 0)
    {
      if (num_beep < beepp * 2)
      {
        mode = 100;
      }
    }
    if (millis() - bloonk > mode)
    {
      if (num_beep >= beepp * 2)
      {
        num_beep = 0;
      }
      bloonk = millis();
      digitalWrite(D4, !digitalRead(D4));
      num_beep++;
    }
  }
  else
  {
    digitalWrite(D4, mode);
  }
  /*if (millis() - bloonk > mode)
    {
      bloonk = millis();
      digitalWrite(D4, !digitalRead(D4));
    }
  }else{
    digitalWrite(D4, mode);
  }*/
}
/////////////////////////////
String ttopic,ppayload;
uint8_t pins[]={D0,D0,D2,D3,D4,D5,D6,D7,D8};
void callback(char *topic, byte *payload, unsigned int length)
{
  ttopic=String(topic);
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  ppayload="";
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
    ppayload+=String((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
if(String(topic).indexOf("pwm")!=-1){
  Serial.println(topic[strlen(topic)-1]);
  Serial.println(ttopic);
  Serial.println(ppayload);
  Serial.println(pins[6]);
  analogWrite(pins[6], ppayload.toInt());
}

  if ((char)payload[0] == '1')
  {
    //digitalWrite(BUILTIN_LED, LOW); // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  }
  else
  {
    //digitalWrite(BUILTIN_LED, HIGH); // Turn the LED off by making the voltage HIGH
  }
}

bool reconnect()
{
  // Loop until we're reconnected
  String clientId = "ESP8266Client-";
  clientId += String(random(0xffff), HEX);
  // Attempt to connect
  if (client.connect(clientId.c_str(), "mgtiorid", "WOVVw2j9Rm2t"))
  {
    Serial.println("Connect welldone");
    // Once connected, publish an announcement...
    Serial.print("Publish msg: ");
    Serial.println(client.publish("outTopic", "hello world"));
    // ... and resubscribe
    Serial.print("Subscribe topic_name: ");
    Serial.println(client.subscribe("in/#"));
  }
  else
  {
    Serial.print("failed, rc=");
    Serial.println(client.state());
  }
  return client.connected();
}
/////////////////////////////
void setup()
{
  Serial.begin(115200);
  Serial.println();
  pinMode(D4, OUTPUT);
  pinMode(D6, OUTPUT);
  analogWriteFreq(300);
  //digitalWrite(D4, HIGH); //Выключить
  //your other setup stuff...
  if (LittleFS.begin())
  {
    Serial.println("LittleFS is started");
  }
  else
  {
    Serial.println("LittleFS is problem");
  }
  Serial.println(LittleFS.exists("/param.json"));
  //WiFi.setSleepMode(WIFI_NONE_SLEEP);
  //WiFi.disconnect();
  //WiFi.softAPdisconnect();
  //WiFi.mode(WIFI_AP_STA);
  if (WiFi.getAutoConnect() != true)
  {
    Serial.println("Auto connect set TRUE!");
    WiFi.setAutoConnect(true);
  }
  else
  {
    Serial.println("Auto connect is true");
  }
  //WiFi.setAutoConnect(false);
  //WiFi.begin();
  //WiFi.softAP("Horizont");

  stationConnectedHandler = WiFi.onSoftAPModeStationConnected(&onStationConnected);
  // Call "onStationDisconnected" each time a station disconnects
  stationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected(&onStationDisconnected);

  static WiFiEventHandler e1, e2, e3, e4;
  e1 = WiFi.onStationModeGotIP(onSTAGotIP);
  e2 = WiFi.onStationModeDisconnected(onSTADisconnected);
  e3 = WiFi.onStationModeConnected(onSTAConnected);
  e4 = WiFi.onWiFiModeChange(onWFchange);
  // Call "onProbeRequestPrint" and "onProbeRequestBlink" each time
  // a probe request is received.
  // Former will print MAC address of the station and RSSI to Serial,
  // latter will blink an LED.
  //probeRequestPrintHandler = WiFi.onSoftAPModeProbeRequestReceived(&onProbeRequestPrint);
  //probeRequestBlinkHandler = WiFi.onSoftAPModeProbeRequestReceived(&onProbeRequestBlink);
  ////////////////////////////////////////
  dnsServer.start(53, "*", WiFi.softAPIP());
  //server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER); //only when requested from AP
  ws.onEvent(onWsEvent);

  server.addHandler(&ws);

  server.on(
      "/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response); }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      Serial.println(index);
      //Serial.println(final);
      //Serial.println(len);
        //json = "{\"ota\": \""+String(index)+"\"}";
       // ws.printfAll(json.c_str());
      //Serial.println(filename);
      if(millis()-ota_ws>400){
        ota_ws=millis();
        json = "{\"ota\": \""+String(index+len)+"\"}";
       ws.printfAll(json.c_str());
      }
      //json = "{\"notif\": {\"text\":\"Чееее за длинна"+String(len)+" \",\"duration\": 4000,\"type\":\"warning\"}}";
      //ws.printfAll(json.c_str());
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      Update.runAsync(true);
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        json = "{\"notif\": {\"text\":\"Обновление завершено\",\"duration\": 3000,\"type\":\"success\"}}";
        ws.printfAll(json.c_str());
        json = "{\"ota\": \"0\"}";
       ws.printfAll(json.c_str());
        Serial.printf("Update Success: %uB\n", index+len);
      } else {
        Update.printError(Serial);
      }
    } });

  server
      .serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html");
  //.setAuthentication("admin", "admin");
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  //Send OTA events to the browser
  /*ArduinoOTA.onStart([]() { ws.printfAll("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { ws.printfAll("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress / (total / 100)));
    ws.printfAll(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR)
      ws.printfAll("Auth Failed", "ota");
    else if (error == OTA_BEGIN_ERROR)
      ws.printfAll("Begin Failed", "ota");
    else if (error == OTA_CONNECT_ERROR)
      ws.printfAll("Connect Failed", "ota");
    else if (error == OTA_RECEIVE_ERROR)
      ws.printfAll("Recieve Failed", "ota");
    else if (error == OTA_END_ERROR)
      ws.printfAll("End Failed", "ota");
  });
  //ArduinoOTA.setHostname(hostName);
  //ArduinoOTA.begin();*/

  //more handlers...
  server.begin();
  Serial.println("...");
  Wire.begin(D1, D2);
  i2cscan();
  //rtc.begin();
  //rtc.initClock();//Сброс
  //Serial.println(bgins);
  //set a time to start with.
  //day, weekday, month, century, year
  //rtc.setDate(14, 6, 3, 0, 10);
  //const char dater[] = __DATE__;

  //int day =  atoi(&now[0]);
  //int wkday = atoi(&now[3]);
  //int month = atoi(&now[6]);
  //int month = atoi(&now[6]);
  //int month = atoi(&now[6]);
  /*
  const char timer[] = __TIME__;

  int hour = atoi(&timer[0]);
  int minute = atoi(&timer[3]);
  int seconds = atoi(&timer[6]);
  //hr, min, sec
  //rtc.setTime(hour, minute, seconds);*/
  unsigned status;
  // default settings
  status = bme.begin(0x76);
  // You can also pass in a Wire library object like &Wire2
  // status = bme.begin(0x76, &Wire2)
  if (!status)
  {
    //Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x");
    Serial.println(bme.sensorID(), 16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    //while (1)
    //  delay(10);
  }

  DS18B20.begin();
  //////////GPIO_SETUP///////////
  pinMode(D3, OUTPUT);
  analogWriteFreq(500);

  Serial.println("Start>>>");
  ws_enable = false;
  ////////FS///////
  /*
  Serial.println(F("Inizializing FS..."));
  if (LittleFS.begin())
  {
    Serial.println(F("done."));
  }
  else
  {
    Serial.println(F("fail."));
  }

  // To format all space in LittleFS
  // LittleFS.format()

  // Get all information of your LittleFS
  FSInfo fs_info;
  LittleFS.info(fs_info);

  Serial.println("File sistem info.");

  Serial.print("Total space:      ");
  Serial.print(fs_info.totalBytes);
  Serial.println("byte");

  Serial.print("Total space used: ");
  Serial.print(fs_info.usedBytes);
  Serial.println("byte");

  Serial.print("Block size:       ");
  Serial.print(fs_info.blockSize);
  Serial.println("byte");

  Serial.print("Page size:        ");
  Serial.print(fs_info.totalBytes);
  Serial.println("byte");

  Serial.print("Max open files:   ");
  Serial.println(fs_info.maxOpenFiles);

  Serial.print("Max path lenght:  ");
  Serial.println(fs_info.maxPathLength);

  Serial.println();

  // Open dir folder
  Dir dir = LittleFS.openDir("/");
  // Cycle all the content
  while (dir.next())
  {
    // get filename
    Serial.print(dir.fileName());
    Serial.print(" - ");
    // If element have a size display It else write 0
    if (dir.fileSize())
    {
      File f = dir.openFile("r");
      Serial.println(f.size());
      f.close();
    }
    else
    {
      Serial.println("0");
    }
  }
  /////////////////
  File testFile = LittleFS.open(F("/index.html"), "r");
  if (testFile)
  {
    Serial.println("Read file content!");
  
    Serial.println(testFile.readString());
    testFile.close();
  }
  else
  {
    Serial.println("Problem on read file!");
  }*/
  client.setServer("m15.cloudmqtt.com", 13843);
  client.setCallback(callback);
}

int brightness = 301; // how bright the LED is
int fadeAmount = 1;

void loop()
{
  if (millis() - last >= 3000 && false) //enable
  {
    last = millis();
    /* float tempC;
    float tempF;
    DS18B20.requestTemperatures();
    tempC = DS18B20.getTempCByIndex(0);
    tempF = DS18B20.getTempFByIndex(0);
    Serial.println(tempC);
    Serial.println(tempF);
    //printValues();
    Serial.println(rtc.formatTime());
    Serial.println(rtc.formatDate());
    Serial.println(rtc.getTimerValue());
    Serial.println(rtc.getVoltLow()); // 1 is POWER_LOST 0-good
    Serial.println(F(__DATE__));
    */
  }
  //Serial.println(bool(rtc.lostPower()));
  //rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));

  //Serial.println(digitalRead(D3));
  //clok();
  //digitalWrite(D3, HIGH);
  //delay(2000);
  //digitalWrite(D3, LOW);
  //PWM Value varries from 0 to 1023

  //Continuous Fading

  //Serial.println("Fadding Started");
  /*
  while (1)
  {
    // set the brightness of pin 9:
    analogWrite(D3, brightness);

    // change the brightness for next time through the loop:
    brightness = brightness + fadeAmount;
    //Serial.println(brightness);
    // reverse the direction of the fading at the ends of the fade:
    if (brightness <= 300 || brightness >= 700)
    {
      fadeAmount = -fadeAmount;
    }
    // wait for 30 milliseconds to see the dimming effect
    delay(30);
  }

*/
  if (millis() - currentMillis > 10000 && ws_enable)
  {
    currentMillis = millis();
    //Serial.println("WFS");
    //now = false; правильно в самом конце
    json = "{\"wifi\":{";
    int n = WiFi.scanComplete();
    if (n == -2)
    {
      WiFi.scanNetworks(true);
    }
    else if (n)
    {
      for (int i = 0; i < n; ++i)
      {
        if (i)
          json += ",";
        json += "\"" + WiFi.SSID(i) + "\":{";
        json += "\"rssi\":" + String(WiFi.RSSI(i));
        //json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
        json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
        json += ",\"channel\":" + String(WiFi.channel(i));
        json += ",\"secure\":" + String(WiFi.encryptionType(i) == 7 ? "\"open\"" : "\"close\"");
        json += ",\"hide\":" + String(WiFi.isHidden(i) ? "true" : "false");
        json += "}";
      }
      WiFi.scanDelete();
      if (WiFi.scanComplete() == -2)
      {
        WiFi.scanNetworks(true);
      }
      json += "}}";
      ws.printfAll(json.c_str());
      //Serial.println(json);
      //Serial.println(json);
    }
    //json += "}}]";
    //ws.printfAll(json.c_str());
    //Serial.println(json);
    ////////////////
    float tempC;
    float tempF;
    DS18B20.requestTemperatures();
    tempC = DS18B20.getTempCByIndex(0);
    tempF = DS18B20.getTempFByIndex(0);
    ////////////////
    json = "{\"connect\": \"true\"}";
    ws.printfAll(json.c_str());
    json = "{\"notif\": {\"text\":\"Test <a href='ya.ru'>NEW</a> message\",\"duration\": 15000,\"type\":\"show\"}}";
    ws.printfAll(json.c_str());
    /////////////////////////////
    json = "{\"metric\": {\"name\":\"id\",\"value\":\"" + String(ESP.getChipId(), HEX) + "\",\"icon\": \"temp\",\"postfix\":\"\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"heap\",\"value\":\"" + String(ESP.getFreeHeap() / 1000) + "\",\"icon\": \"temp\",\"postfix\":\"kb\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"time1\",\"value\":\"" + String(rtc.formatTime()) + "\",\"icon\": \"temp\",\"postfix\":\"\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"ds18b20\",\"value\":\"" + String(tempC) + "\",\"icon\": \"temp\",\"postfix\":\"C°\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"bme_t\",\"value\":\"" + String(bme.readTemperature()) + "\",\"icon\": \"temp\",\"postfix\":\"C°\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"bme_h\",\"value\":\"" + String(bme.readHumidity()) + "\",\"icon\": \"humi\",\"postfix\":\"%\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"bme_p\",\"value\":\"" + String(bme.readPressure() / 100.0F) + "\",\"icon\": \"humi\",\"postfix\":\"hPa\"}}";
    ws.printfAll(json.c_str());
    json = "{\"metric\": {\"name\":\"bme_alt\",\"value\":\"" + String(bme.readAltitude(SEALEVELPRESSURE_HPA)) + "\",\"icon\": \"humi\",\"postfix\":\"m. sea level\"}}";
    ws.printfAll(json.c_str());
    /*Serial.print("Temperature = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");

  Serial.print("Pressure = ");

  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");

  Serial.print("Approx. Altitude = ");
  Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
  Serial.println(" m");

  Serial.print("Humidity = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");

    json = "{\"notif\": {\"text\":\"Обычный длиииииннннныыыыыыйййййййй очень длинннныыыйййй текст\",\"duration\": 15000,\"type\":\"show\"}}";
    ws.printfAll(json.c_str());rtc.formatTime()
    json = "{\"notif\": {\"text\":\"Информация\",\"duration\": 4000,\"type\":\"info\"}}";
    ws.printfAll(json.c_str());
    json = "{\"notif\": {\"text\":\"Предупреждение\",\"duration\": 23000,\"type\":\"warning\"}}";
    ws.printfAll(json.c_str());
    json = "{\"notif\": {\"text\":\"Выполнено удачно\",\"duration\": 32000,\"type\":\"success\"}}";
    ws.printfAll(json.c_str());
    json = "{\"notif\": {\"text\":\"Провал, ОШИБКА!!!\",\"duration\": 10000,\"type\":\"error\"}}";
    ws.printfAll(json.c_str());*/
  }

  if (now)
  {
    Serial.println("Start STA");
    //WiFi.begin("Redmi", "12345678");
  }

  if (millis() - sta_rec > 60000 && on_rec)
  {
    on_rec = false;
    retry = 0;
    Serial.println("Start wait 10sec");
    //WiFi.setAutoReconnect(true);
    wifi_station_set_reconnect_policy(true);
    WiFi.reconnect();
    //WiFi.setAutoConnect(true);
  }

  if (millis() - ap_sec > 2000 && off_ap)
  {
    off_ap = false;
    Serial.println("softAP OFF");
    WiFi.persistent(false);
    WiFi.softAPdisconnect(true);
    WiFi.persistent(true);
  }

  if (millis() - one_sec > 10000)
  {
    one_sec = millis();
    Serial.print("Wifi is: ");
    switch (WiFi.status())
    {
    case 0:
      Serial.println("0 в процессе");
      break;
    case 1:
      Serial.println("1 настроенный SSID не может быть достигнут");
      break;
    case 3:
      Serial.println("3 успешно подключен");
      break;
    case 4:
      Serial.println("4 пароль неверен");
      break;
    case 6:
      Serial.println("6 модуль не настроен в режиме станции");
      break;
    default:
      // выполняется, если не выбрана ни одна альтернатива
      Serial.println("Error case");
      // default необязателен
    }
    Serial.println("is connected " + String(WiFi.isConnected()));
    Serial.println("read D5 " + String(analogRead(D6)));
    /*
    Serial.print("WiFI mode: ");
    switch (WiFi.getMode())
    {
    case 3:
      Serial.println("WIFI_AP_STA");
      break;
    case 2:
      Serial.println("WIFI_AP");
      break;
    case 1:
      Serial.println("WIFI_STA");
      break;
    case 0:
      Serial.println("WIFI_OFF");
      break;
    }*/

    //Serial.println(ESP.getChipId(), HEX);
    //Serial.println(String(ESP.getFreeHeap() / 1000) + " kb");
    //Serial.println("Channel " + String(WiFi.channel()));
    //Serial.println(WiFi.SSID());
    //Serial.println(WiFi.psk());
    //Serial.println(WiFi.waitForConnectResult());
    //Serial.println(WiFi.isConnected());
    //Serial.println(WiFi.status());
    //Serial.println(ESP.getCpuFreqMHz());
    /*
    Serial.println("#############");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.softAPIP());
    Serial.println("#############");
    Serial.println("ws_enable " + String(ws_enable));
    Serial.println("#############");
    Serial.println(WiFi.getListenInterval());
    */
    //Serial.println(String());
    //WiFi.reconnect();
    //Serial.println(WiFi.getMode());
    //Serial.println("#############");
    /*
    WL_CONNECTED – если соединение с WiFi-сетью успешно установлено
WL_NO_SHIELD – если не подключен WiFi-модуль
WL_IDLE_STATUS – временный статус. Он возвращается, когда функция WiFi.begin() вызвана и остается активной. Если количество попыток подключения будет исчерпано, этот статус меняется на WL_CONNECT_FAILED, а если соединение будет успешно установлено, то на WL_CONNECTED
WL_NO_SSID_AVAIL – нет доступных SSID
WL_SCAN_COMPLETED – когда завершено сканирование сетей
WL_CONNECT_FAILED – когда все попытки подключения заканчиваются неуспешно
WL_CONNECTION_LOST – если соединение прервано
WL_DISCONNECTED – при отключении от сети
    */
  }

  if (shouldReboot)
  {
    Serial.println("Rebooting...");
    json = "{\"notif\": {\"text\":\"Reboot...\",\"duration\": 3000,\"type\":\"info\"}}";
    ws.printfAll(json.c_str());
    delay(100);
    ESP.restart();
  }
  //ArduinoOTA.handle();
  //if (ws.getClients() > 15)
  //{
  // }
  if (WiFi.isConnected())
  {
    if (!client.connected())
    {
      if (millis() - lastReconnectAttempt > 5000)
      {
        lastReconnectAttempt = millis();
        // Attempt to reconnect
        Serial.println("Atempt connect rec");
        if (reconnect())
        {
          lastReconnectAttempt = 0;
        }
      }
    }
    else
    {
      // Client connected

      client.loop();
    }
  }
  blink(beep, n_beep);
  ws.cleanupClients();
  dnsServer.processNextRequest();
  now = false;
}
