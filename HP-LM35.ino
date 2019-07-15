
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>

#ifndef STASSID
#define STASSID "█████████████████"
#define STAPSK  "█████████"
#define MQTT_CLASS "HP-LM35"
#define VERSION "1.3"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
const char* mqtt_server = "MQTT-SRV.bilibili.com";//服务器的地址 
const int port = 1883;//服务器端口号
char mqtt_cls[sizeof(MQTT_CLASS) + 13];
char msg_buf[64];

WiFiClient espClient;
PubSubClient client(espClient);

char last_state = -1;
int set_state = 0;
long last_rssi = -1;
unsigned long last_send_rssi;
unsigned long last_state_hold;
bool otaMode = true;                             //OTA mode flag
bool hasPacket = false;

void setup() {
  pinMode(A0, INPUT);
  
  Serial.begin(115200);
  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(mqtt_cls, MQTT_CLASS"-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  WiFi.mode(WIFI_STA);
//  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  WiFi.begin(ssid, password);

  EEPROM.begin(256);
  for (int i = 0; i < sizeof(MQTT_CLASS) - 1; i++)
  {
    if (EEPROM.read(i) != mqtt_cls[i])
    {
      initEEPROM();
      break;
    }
  }

  Serial.println();
  Serial.printf("Flash: %d\n", ESP.getFlashChipRealSize());
  Serial.printf("Version: %s\n", VERSION);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  
  Serial.print("Connected to wifi. My address:");
  IPAddress myAddress = WiFi.localIP();
  Serial.println(myAddress);  
  
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 1000);
  client.setServer(mqtt_server, port);//端口号
  client.setCallback(callback); //用于接收服务器接收的数据

}


void initEEPROM()
{
  for (int i = 0; i < sizeof(MQTT_CLASS) - 1; i++)
  {
    EEPROM.write(i, mqtt_cls[i]);
  }
  EEPROM.write(15, 10);
  EEPROM.write(16, 0);
  EEPROM.write(64, 0);
  EEPROM.commit();
}

char *loadEEPName(char *buffer)
{
    uint8_t len = EEPROM.read(16);
    for (uint8_t i = 0; i < len; i++)
    {
        buffer[i] = EEPROM.read(i + 17);
    }
    return buffer + len;
}

char *loadEEPData(char *buffer)
{
    uint8_t len = EEPROM.read(64);
    for (uint8_t i = 0; i < len; i++)
    {
        buffer[i] = EEPROM.read(i + 65);
    }
    return buffer + len;
}

void callback(char* topic, byte* payload, unsigned int length) {//用于接收数据
  int l=0;
  int p=1;
  hasPacket = true;
  if (strcmp(topic, "ota") == 0)
  {
    WiFiClient ota_client;

    char bufferByte = payload[length];
    payload[length] = 0;
    Serial.print("Start OTA from URL: ");
    Serial.println((char *)payload);
    t_httpUpdate_return ret = ESPhttpUpdate.update(ota_client, (char *)payload);
    // Or:
    //t_httpUpdate_return ret = ESPhttpUpdate.update(client, "server", 80, "file.bin");

    payload[length] = bufferByte;

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        sprintf(msg_buf, "{\"ota\":\"%s\"}", ESPhttpUpdate.getLastErrorString().c_str());
        client.publish("status", msg_buf);
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        sprintf(msg_buf, "{\"ota\":\"no updates\"}");
        client.publish("status", msg_buf);
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        sprintf(msg_buf, "{\"ota\":\"success\"}");
        client.publish("status", msg_buf);
        Serial.println("HTTP_UPDATE_OK");
        break;
    }
  } else if (strcmp(topic, "setName") == 0)
  {
    if (length > 64 - 21 - EEPROM.read(64))
    {
      length = 64 - 21 - EEPROM.read(64);
    }
    EEPROM.write(16, length);
    for (int i = 0; i < length;i++)
    {
      EEPROM.write(17+i, payload[i]);
    }
    EEPROM.commit();
    sendMeta();
  } else if (strcmp(topic, "setData") == 0)
  {
    if (length > 64 - 21 - EEPROM.read(16))
    {
      length = 64 - 21 - EEPROM.read(16);
    }
    EEPROM.write(64, length);
    for (int i = 0; i < length;i++)
    {
      EEPROM.write(65+i, payload[i]);
    }
    EEPROM.commit();
    sendMeta();
  }
//  Serial.print("payload topic: ");
//  Serial.println(topic);
//  Serial.print("payload length: ");
//  Serial.println(length);
//  Serial.println((char *)payload);
}

void sendMeta()
{
  // max length = 64 - 21
    char *p = msg_buf + sprintf(msg_buf, "{\"name\":\"");
    p = loadEEPName(p);
    p = p + sprintf(p, "\",\"data\":\"");
    p = loadEEPData(p);
    p = p + sprintf(p, "\"}");    
    client.publish("dev", msg_buf);
}

void reconnect() {//等待，直到连接上服务器
  while (!client.connected()) {//如果没有连接上
    if (client.connect(mqtt_cls)) {//接入时的用户名，尽量取一个很不常用的用户名
      Serial.println("Connect to MQTT server Success!");
      sendMeta();
      client.subscribe("ota");//接收外来的数据时的intopic
      client.subscribe("setName");
      client.subscribe("setData");
      last_rssi = -1;
      last_state = -1;
    } else {
      Serial.print("failed, rc=");//连接失败
      Serial.print(client.state());//重新连接
      Serial.println(" try again in 5 seconds");//延时5秒后重新连接
      delay(5000);
    }
  }
}

int Tc;
int lastTc = 0;

void loop() {
  Tc = analogRead(A0);
  
   hasPacket = false;
   reconnect();//确保连上服务器，否则一直等待。
   client.loop();//MUC接收数据的主循环函数。

   long rssi = WiFi.RSSI();
   if ((abs(lastTc - Tc) >= 5 && millis() - last_send_rssi >= 1000) || (abs(rssi - last_rssi) >= 3 && millis() - last_send_rssi >= 5000))
   {
      last_send_rssi = millis();
      lastTc = Tc;
      last_rssi = rssi;
      sprintf(msg_buf, "{\"temp\":%d,\"rssi\":%ld,\"version\":\"%s\",\"ota\":\"unset\"}", Tc, rssi, VERSION);
      client.publish("status", msg_buf);
   }

   if (!hasPacket)
   {
      delay(100);
   }
}
