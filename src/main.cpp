#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"

// Global Variables
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSentRTCM_ms = 0;          // Time of last data pushed to socket
int maxTimeBeforeHangup_ms = 9000; // If we fail to get a complete RTCM frame after 10s, then disconnect from caster

int serverReconections = 0; // Just a running total
long lastReport_ms = 0;

#define LED_BUILTIN 2
const int BUFFER_SIZE = 5000;
const int number_of_messages = 6;
int message_types[number_of_messages] = {1005,
                                         1074,
                                         1084,
                                         1094,
                                         1124,
                                         1230};

WiFiClient ntripCaster;
IPAddress myIP;
bool wifiConnected;
bool connectionSuccess;

// //=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void connectToNtrip()
{
  digitalWrite(LED_BUILTIN, LOW);
  delay(10); // Wait for any serial to arrive
  serverReconections++;

  // Connect if we are not already
  if (ntripCaster.connected() == false)
  {
    if (ntripCaster.connect(casterHost, casterPort) == true) // Attempt connection
    {
      const char formatStr[] = "SOURCE %s /%s\r\nSource-Agent: %s\r\n\r\n";
      const int SERVER_BUFFER_SIZE = strlen(sourceAgent) + strlen(mountPointPW) + strlen(mountPoint) + sizeof(formatStr);

      char serverRequest[SERVER_BUFFER_SIZE];

      snprintf(serverRequest,
               SERVER_BUFFER_SIZE,
               formatStr,
               mountPointPW, mountPoint, sourceAgent);

      ntripCaster.write(serverRequest, strlen(serverRequest));
    }
    // Wait for response
    unsigned long timeout = millis();
    while (ntripCaster.available() == 0)
    {
      if (millis() - timeout > 5000)
      {
        ntripCaster.stop();
        digitalWrite(LED_BUILTIN, LOW);
        delay(60000);
        return;
      }
      delay(10);
    }

    // Check reply
    connectionSuccess = false;
    char response[512];
    int responseSpot = 0;
    while (ntripCaster.available())
    {
      response[responseSpot++] = ntripCaster.read();
      if (strstr(response, "200") > 0) // Look for 'ICY 200 OK'
        connectionSuccess = true;
      if (responseSpot == 511)
        break;
    }
    response[responseSpot] = '\0';

    if (connectionSuccess == false)
    {

      ntripCaster.stop();
      delay(60000);
      return;
    }
  }
  else
  {
    serverReconections = 0;
    connectionSuccess = true;
    delay(1000);
    return;
  }
}

// //=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void beginServing()
{

  if (ntripCaster.connected() == true)
  {
    delay(10);
    while (Serial2.available() > 0)
    {
      Serial2.read();
    }
    lastReport_ms = millis();
    lastSentRTCM_ms = millis();

    while (1)
    {
      delay(1);

      int count = Serial2.available();
      if (count > 0)
      {

        digitalWrite(LED_BUILTIN, HIGH);
        // Serial.print("Received ");
        // Serial.println(count);
        int data = Serial2.read();
        while (data != 0xD3)
        {
          data = Serial2.read();
        }

        int data1 = Serial2.read();
        int data2 = Serial2.read();
        int data3 = Serial2.read();
        int data4 = Serial2.read();

        int messageLength = ((data1 & 0x03) << 8) | data2;
        int messageType =
            ((data3 & 0xFF) << 4) | ((data4 & 0xF0) >> 4);

        for (int i = 0; i < number_of_messages; ++i)
        {
          if (messageType == message_types[i])
          {
            while (Serial2.available() < messageLength)
            {

              delay(2);
              if (millis() - lastSentRTCM_ms > maxTimeBeforeHangup_ms)
              {
                ntripCaster.stop();
                delay(30000);
                return;
              }
            }
            uint8_t messagBuffer[1000];
            messagBuffer[0] = data;
            messagBuffer[1] = data1;
            messagBuffer[2] = data2;
            messagBuffer[3] = data3;
            messagBuffer[4] = data4;
            int j = 5;

            for (j; j < messageLength + 6; j++)
            {
              messagBuffer[j] = Serial2.read();
            }
            ntripCaster.write(messagBuffer, (messageLength + 6));
            // ntripCaster.write(0x11);

            // Serial.print("Wrote message ");
            // Serial.print(messageType);
            // Serial.print(" ");
            // Serial.println(messageLength);
            // Serial.print("i value was ");
            // Serial.println(i);
            // Serial.println("");
          }
        }
      }
      digitalWrite(LED_BUILTIN, LOW);

      if (millis() - lastSentRTCM_ms > maxTimeBeforeHangup_ms)
      {
        ntripCaster.stop();
        delay(30000);
        return;
      }
      if (!wifiConnected)
      {
        ntripCaster.stop();
        delay(30000);
        return;
      }
      if (ntripCaster.connected() == false)
      {
        ntripCaster.stop();
        delay(30000);
        return;
      }

      lastSentRTCM_ms = millis();
    }
  }
  else
  {
    return;
  }
}

// ------------------------------------------------------------------------------------------------

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  wifiConnected = false;
  delay(10000);
}
// ------------------------------------------------------------------------------------------------

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  delay(200);
  myIP = WiFi.localIP();
  delay(2000);
  wifiConnected = true;
}
// ------------------------------------------------------------------------------------------------

void connect_to_wifi()
{
  delay(100);
  WiFi.begin(ssid, password);
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
  }
}

// ------------------------------------------------------------------------------------------------

void connect_to_base()
{
  Serial2.setRxBufferSize(3000);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  while (!Serial2)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(50);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
  }
  delay(5);
  digitalWrite(LED_BUILTIN, HIGH);
}
// ------------------------------------------------------------------------------------------------

void setup()
{
  // Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  connect_to_wifi();
  delay(30000);
  connect_to_base();
}
// ------------------------------------------------------------------------------------------------

void loop()
{
  if (wifiConnected)
  {
    if (Serial2.available() > 0)
    {
      if (serverReconections < 60)
      {
        connectToNtrip();
        beginServing();
      }
      else if (serverReconections < 121)
      {
        delay(300000);
        connectToNtrip();
        beginServing();
      }
      else

      {
        while (true)
        {
          digitalWrite(LED_BUILTIN, LOW);
          delay(80);
          digitalWrite(LED_BUILTIN, HIGH);
          delay(80);
        }
      }
    }
  }
  else
  {
    connect_to_wifi();
  }
}
