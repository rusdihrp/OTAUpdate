#include <Arduino.h>
#include <ESP32httpUpdate.h>
#include <Preferences.h>
#include "mqtt_client.h"

#define VERSION "v0.0.1"
#define FW_VERSION_ADDRESS "savedVersionFW"
#define OTA_FLAG_ADDRESS "savedUpdateFlag"
#define OTA_SIZE_ADDRESS "savedSizeFW"

// STATES
#define STATE_MQTT_SEND 10
#define STATE_IDLE_TASK 20

// STATE RELATED DECLARATIONS
uint8_t mainState;

// length firmware
uint8_t SIZE_FW_LENGTH = 7;

bool mqttConnected = false;

char topicPubVersion[39] = "";
char topicSubSetID[37] = "";
char topicSubUpdate[38] = "";

// cert yg didapat dari devops
const char ca_cert_pem[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
//certificate
-----END CERTIFICATE-----
)EOF";

Preferences preferences;

esp_mqtt_client_config_t mqttCfg;
esp_mqtt_client_handle_t mqttClient;

/*!
    @brief  sebagai fungsi untuk updateOTA
    @param  url sebagai alamat untuk download firmware bin
    @param  size sebagai pengecekan size yang didapat dari server
    @return true or false ota sukses atau gagal di-update
*/
bool updateOTA(String url, uint32_t size)
{
  WiFiClientSecure *getClientOTA = new WiFiClientSecure;
  /**
   * @brief koneksi ke https
   *
   *
   */
  if (getClientOTA)
  {
    t_httpUpdate_return ret = ESPhttpUpdate.updates(url, size);
    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      return false;
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      return true;
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      return true;
      break;
    }
  }
  else
  {
    Serial.print("   Connection to ");
    Serial.print(url);
    Serial.println(" failed. Please check your setup");
  }
  return false;
}

/*!
    @brief  sebagai fungsi untuk configure topic mqtt
    @param  topicMqtt sebagai prefix
    @param  formatMqtt sebagai suffix
*/
void configureDeviceTopics(char *topicMqtt, const char *formatMqtt)
{
  const char DEVICE_TOPIC_STEM[11] = "device/";
  strcat(topicMqtt, DEVICE_TOPIC_STEM);
  strcat(topicMqtt, formatMqtt);
  strcat(topicMqtt, WiFi.macAddress().c_str());
  Serial.println(topicMqtt);
}

/*!
    @brief  sebagai fungsi untuk reboot esp
    @param  reason alasan untuk reboot
*/
void rebootEspWithReason(String reason)
{
  Serial.println(reason);
  delay(1000);
  ESP.restart();
}

/*!
    @brief  sebagai fungsi untuk callback mqtt ketika connect/disconnect, subscribe
    @param  event callback realtime untuk proses mqtt
    @return typedef esp32 yaitu ok(0) atau fail(-1)
*/
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  if (event->event_id == MQTT_EVENT_CONNECTED)
  {
    mqttConnected = true;
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_CONNECTED", event->msg_id, event->event_id);
    /**
     * @brief led blink connected
     *
     */
    esp_mqtt_client_subscribe(mqttClient, topicSubUpdate, 0);
  }
  else if (event->event_id == MQTT_EVENT_DISCONNECTED)
  {
    ESP_LOGI("TEST", "MQTT event: %d. MQTT_EVENT_DISCONNECTED", event->event_id);
    mqttConnected = false;
    /**
     * @brief led blink disconnected
     *
     */
  }
  else if (event->event_id == MQTT_EVENT_SUBSCRIBED)
  {
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_SUBSCRIBED", event->msg_id, event->event_id);
  }
  else if (event->event_id == MQTT_EVENT_UNSUBSCRIBED)
  {
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_UNSUBSCRIBED", event->msg_id, event->event_id);
  }
  else if (event->event_id == MQTT_EVENT_PUBLISHED)
  {
    ESP_LOGI("TEST", "MQTT event: %d. MQTT_EVENT_PUBLISHED", event->event_id);
  }
  else if (event->event_id == MQTT_EVENT_DATA)
  {
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_DATA", event->msg_id, event->event_id);
    ESP_LOGI("TEST", "Topic length %d. Data length %d", event->topic_len, event->data_len);
    ESP_LOGI("TEST", "Incoming data: %.*s %.*s\n", event->topic_len, event->topic, event->data_len, event->data);

    preferences.begin("device", false);

    if (!strncmp(event->topic, topicSubSetID, event->topic_len))
    {
      if (!strncmp(event->data, "l", 1))
      {
        // Check Version
        esp_mqtt_client_publish(mqttClient, topicPubVersion, VERSION, 7, 0, false);
      }
    }
    else if (!strncmp(event->topic, topicSubUpdate, event->topic_len))
    {
      // OTA
      if (!strncmp(event->data, "v", 1))
      {
        const char *find = strchr(event->data, ',');
        char cmpVersion[12] = "";
        uint8_t sizeIndex = find - event->data + 1;
        memcpy(cmpVersion, event->data, sizeIndex - 1);
        Serial.println(cmpVersion);
        /**
         * [compare versi ke atasnya agar tidak bisa downgrade, dan juga perbedaan versi
         * harus berjarak 1 versi dari yang existing, agar tidak bisa lompat ke versi
         * dari 1.x.x ke 3.x.x ]
         */

        if (!strcmp(VERSION, cmpVersion))
        {
          // logPrint("      No More Update");
        }
        else
        {
          char tempSizeFw[7] = "";
          memcpy(tempSizeFw, event->data + sizeIndex, SIZE_FW_LENGTH);
          uint32_t uintResult = atoi(tempSizeFw);
          preferences.putBytes(FW_VERSION_ADDRESS, cmpVersion, 10);
          preferences.putBool(OTA_FLAG_ADDRESS, true);
          preferences.putULong(OTA_SIZE_ADDRESS, uintResult);
          rebootEspWithReason("Update Flag configured. Restarting...");
        }
      }
    }

    preferences.end();
  }
  else if (event->event_id == MQTT_EVENT_BEFORE_CONNECT)
  {
    ESP_LOGI("TEST", "MQTT event: %d. MQTT_EVENT_BEFORE_CONNECT", event->event_id);
  }
  return ESP_OK;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("***INITIALIZATION STARTING***");
  Serial.print("***FIRMWARE VERSION:");
  Serial.print(VERSION);
  Serial.println("***");

  Serial.println("---PREFERENCES NAMES INITIALIZATION---");
  configureDeviceTopics(topicPubVersion, "version/");
  configureDeviceTopics(topicSubSetID, "setid/");
  configureDeviceTopics(topicSubUpdate, "update/");

  /**
   * @brief asumsi init wifi
   */

  Serial.println("---CHECK IF SHOULD UPDATE FIRMWARE FROM OTA---");
  preferences.begin("device", false);
  if (!preferences.getBool(OTA_FLAG_ADDRESS, ""))
  {
    Serial.println("   Firmware is latest version");
  }
  else
  {
    preferences.putBool(OTA_FLAG_ADDRESS, false);
    delay(500);
    uint32_t sizeLength = preferences.getULong(OTA_SIZE_ADDRESS);
    delay(500);
    Serial.println("   Update OTA");

    char newVersion[10] = "";
    preferences.getBytes(FW_VERSION_ADDRESS, newVersion, 10);

    char fwUrl[54] = "";
    snprintf(fwUrl, 54, "https://fw.device.id/download/envboard-%s.bin", newVersion); // PERLU DISESUIAKAN FWURL

    Serial.println("   Size of firmware: " + String(sizeLength));
    Serial.println("   FWurl : " + String(fwUrl));
    if (!updateOTA(fwUrl, sizeLength))
    {
      preferences.putULong(OTA_SIZE_ADDRESS, 0);
      Serial.println("   Firmware couldn't update");
    }
  }

  preferences.end();

  Serial.println("---MQTT INITIALIZATION---");
  mqttCfg.host = "hostnamemqtt.com";
  mqttCfg.username = "user";
  mqttCfg.password = "pass";
  mqttCfg.port = 8883;
  mqttCfg.transport = MQTT_TRANSPORT_OVER_SSL;
  mqttCfg.cert_pem = ca_cert_pem;
  mqttCfg.keepalive = 60;
  mqttCfg.event_handle = mqtt_event_handler;
  mqttCfg.lwt_msg_len = 1;
  mqttCfg.lwt_retain = 1;

  Serial.println("   Connecting to MQTT Server....");
  mqttClient = esp_mqtt_client_init(&mqttCfg);
  esp_err_t err = esp_mqtt_client_start(mqttClient);
  ESP_LOGI("TEST", "Client connect. Error = %d %s", err, esp_err_to_name(err));
  if (err != ESP_OK)
  {
    Serial.println("[MQTT] Failed to connect to MQTT server at setup");
  }
  Serial.println("---END MQTT INITIALIZATION---");

  mainState = STATE_MQTT_SEND;
}

void loop()
{
  switch (mainState)
  {
  case (STATE_MQTT_SEND):
    mainState = STATE_IDLE_TASK;
    /**
     * @brief panggil fungsi periodic kirim data/status
     */
    break;

  case (STATE_IDLE_TASK):
    mainState = STATE_MQTT_SEND;
    /**
     * @brief panggil fungsi reconnect wifi atau modem
     */
    break;

  default:
    break;
  }
}