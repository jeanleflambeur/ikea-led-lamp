#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>   
#include <credentials.h>

#include "ESPRotary.h"

String s_client_id;

WiFiClient s_wifi_client;
uint32_t s_last_wifi_connect_attempt_tp = 0;

PubSubClient s_mqtt_client(s_wifi_client);
uint32_t s_last_mqtt_connect_attempt_tp = 0;

#define NODE_LOCATION "home"
#define NODE_TYPE "ikea_led"
#define NODE_NAME "bedroom_light"

const char* s_status_topic = "/"NODE_LOCATION"/"NODE_NAME"/status";
const char* s_switch_topic = "/"NODE_LOCATION"/"NODE_NAME"/switch";

const char* s_brightness_topic = "/"NODE_LOCATION"/"NODE_NAME"/brightness";
const char* s_brightness_set_topic = "/"NODE_LOCATION"/"NODE_NAME"/brightness/set";

ESPRotary s_encoder = ESPRotary(D3, D4);

int k_led_pin = D6;
int k_button_pin = D2;

bool s_status = true;
int32_t s_button_level = 0;
int32_t s_last_button_level_change_tp = 0;

int32_t s_old_position  = 0;
int32_t s_last_position_change_tp = 0;

int32_t s_last_interpolation_tp = 0;


float s_brightness = 0.5f;
float s_target_brightness = 0.5f;
float s_saved_brightness = 0.5f;

void mqtt_publish(const char* topic, float t)
{
  char buffer[128] = {0};
  sprintf(buffer, "%f", t);
  s_mqtt_client.publish(topic, buffer, strlen(buffer));
}
void mqtt_publish(const char* topic, bool t)
{
  char buffer[128] = {0};
  sprintf(buffer, "%s", t ? "true" : "false");
  s_mqtt_client.publish(topic, buffer, strlen(buffer));
}


void set_brightness(float b)
{
  if (b > 1.f)
  {
    b = 1.f;
  }
  if (b < 0.002f)
  {
    b = 0.f;
  }
  s_brightness = b;
  analogWrite(k_led_pin, static_cast<int>(pow((1.f - b), 0.25f) * 1023.f));
}

void set_target_brightness(float brightness)
{
  if (brightness < 0.f)
  {
    brightness = 0.f;
  }
  if (brightness > 1.f)
  {
    brightness = 1.f;
  }
  s_target_brightness = brightness;
  mqtt_publish(s_brightness_topic, s_target_brightness * 255.f);
}

void save_target_brightness()
{
  s_saved_brightness = s_target_brightness;
}

void load_target_brightness()
{
  float brightness = s_saved_brightness;
  if (isnan(brightness) || !(brightness >= 0.f && brightness <= 1.f))
  {
    brightness = 0.5f;
  }
  set_target_brightness(brightness);
}

void set_status(bool status)
{
  if (status == s_status)
  {
    return;
  }

  if (status == false)
  {
    s_status = status;
    mqtt_publish(s_status_topic, s_status);
    
    save_target_brightness();
    set_target_brightness(0);
    Serial.println("Turning off");
  }
  else
  {
    s_status = status;
    mqtt_publish(s_status_topic, s_status);
    
    load_target_brightness();
  
    //too low? make sure it tirns on with a decent brightness
    if (s_target_brightness < 0.05f)
    {
      set_target_brightness(0.2f);
    }
    Serial.println("Turning on");
  }  
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) 
{
  char buffer[128] = { 0, };
  length = min(length, 126u);
  memcpy(buffer, payload, length);
  buffer[length] = 0;

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.print(length);
  Serial.print(" bytes '");
  Serial.print(buffer);
  Serial.print("'");
  Serial.println();  
  
  if (strcmp(topic, s_switch_topic) == 0)
  {
    set_status(strcmp(buffer, "true") == 0);
  }
  if (strcmp(topic, s_brightness_set_topic) == 0)
  {
    float brightness = atoi(buffer) / 255.f;
    if (isnan(brightness))
    {
      brightness = 0.5f;
    }
    set_target_brightness(brightness);
  }
}

void reconnect_mqtt() 
{
  // Loop until we're reconnected
  if (!s_mqtt_client.connected() && (s_last_mqtt_connect_attempt_tp + 30000 < millis()) || s_last_mqtt_connect_attempt_tp == 0) 
  {
    s_last_mqtt_connect_attempt_tp = millis();
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (s_mqtt_client.connect(s_client_id.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) 
    {
      Serial.println("connected");
      s_last_mqtt_connect_attempt_tp = 0;

      // Once connected, publish an announcement...
      mqtt_publish(s_status_topic, s_status);
      mqtt_publish(s_brightness_topic, s_target_brightness);
      // ... and resubscribe
      s_mqtt_client.subscribe(s_switch_topic);
      s_mqtt_client.subscribe(s_brightness_set_topic);
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(s_mqtt_client.state());
      Serial.println(" try again");
    }
  }
}

void setup_wifi(bool wait) 
{
  if (WiFi.status() != WL_CONNECTED && (s_last_wifi_connect_attempt_tp + 30000 < millis() || s_last_wifi_connect_attempt_tp == 0))
  {
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(WLAN_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WLAN_SSID, WLAN_PASSWORD);
    s_last_wifi_connect_attempt_tp = millis();
  }

  if (wait)
  {
    int start = millis();
    while (WiFi.status() != WL_CONNECTED && start + 5000 > millis())
    {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    s_last_wifi_connect_attempt_tp = 0;
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("WiFi NOT connected, postponed");
  }
}  

void setup() 
{
  Serial.begin(115200);

  s_client_id = NODE_LOCATION"/"NODE_NAME"/"NODE_TYPE"/";
  s_client_id += String(random(0xffff), HEX);

  Serial.print("Node location: "); Serial.println(NODE_LOCATION);
  Serial.print("Node type: "); Serial.println(NODE_TYPE);
  Serial.print("Node name: "); Serial.println(NODE_NAME);
  Serial.print("Client ID: "); Serial.println(s_client_id.c_str());
  Serial.print("MQTT status topic: "); Serial.println(s_status_topic);
  Serial.print("MQTT switch topic: "); Serial.println(s_switch_topic);
  Serial.print("MQTT brightness topic: "); Serial.println(s_brightness_topic);
  Serial.print("MQTT brightness set topic: "); Serial.println(s_brightness_set_topic);

  // put your setup code here, to run once:
  pinMode(k_led_pin, OUTPUT);
  analogWriteFreq(128);

  pinMode(k_button_pin, INPUT_PULLUP);

  setup_wifi(true);
  s_mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);
  s_mqtt_client.setCallback(mqtt_callback);

  //OTA SETUP
  ArduinoOTA.setPort(OTA_PORT);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(s_client_id.c_str());

  // No authentication by default
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() 
  {
    Serial.println("Starting");
  });
  ArduinoOTA.onEnd([]() 
  {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) 
  {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) 
  {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  
  s_brightness = 0.f;
  load_target_brightness();
}

void process_encoder()
{
  s_encoder.loop();

  bool value_changed = false;
  float delta = 0.f;
  int32_t new_position = s_encoder.getPosition() / 4;
  if (new_position != s_old_position)
  {
    int32_t tp = millis();
    if (tp < s_last_position_change_tp)
    {
      s_last_position_change_tp = tp;
    }
    if (tp != s_last_position_change_tp)
    {
      float dt = static_cast<float>(tp - s_last_position_change_tp) / 1000.f;
      if (dt > 1000.f)
      {
        dt = 1000.f;
      }

      value_changed = true;
      delta = static_cast<float>(new_position - s_old_position) / dt;
  
      s_last_position_change_tp = tp;
      
      s_old_position = new_position;
    }
  }

  if (value_changed && s_status == true)
  {
    float sign = delta < 0.f ? -1.f : 1.f;
    if (abs(delta) > 10.f)
    {
      delta = sign * 10.f;
    }

    float brightness = s_target_brightness + delta / 100.f;
    set_target_brightness(brightness);
  }  
}

void process_interpolation()
{
  int32_t tp = millis();
  if (tp < s_last_interpolation_tp)
  {
    s_last_interpolation_tp = tp;
  }

  if (tp - s_last_interpolation_tp < 10)
  {
    return;
  }
  s_last_interpolation_tp = tp;
  
  set_brightness(s_brightness + (s_target_brightness - s_brightness) * 0.05f);
}

void process_button()
{
  int level = digitalRead(k_button_pin) ? 1 : 0;
  if (level != s_button_level && millis() - s_last_button_level_change_tp > 50)
  {
    s_last_button_level_change_tp = millis();
    
    s_button_level = level;
    if (level == 0)
    {
      set_status(!s_status);
    }
  }
}

void process_mqtt()
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    delay(1);
    Serial.print("WIFI Disconnected. Attempting reconnection.");
    setup_wifi(false);
    return;
  }
  
  if (!s_mqtt_client.connected()) 
    reconnect_mqtt();
  else
    s_mqtt_client.loop();
}

 
void loop() 
{
  process_encoder();
  process_interpolation();
  process_button();
  process_mqtt();
  ArduinoOTA.handle(); 
}
