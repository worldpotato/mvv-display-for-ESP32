//Quellen: 
// https://github.com/Bodmer/TFT_eSPI/blob/master/examples/320%20x%20240/Free_Font_Demo/Free_Font_Demo.ino
// https://github.com/Xinyuan-LilyGO/TTGO-T-Display
#include <WiFi.h>
#include <HTTPClient.h>
//mvg api timestamp in ms needs long long
#define ARDUINOJSON_USE_LONG_LONG 1
//geops api timestamp in ms needs double
#define ARDUINOJSON_USE_DOUBLE 1
//geops api uses unicode characters
#define ARDUINOJSON_DECODE_UNICODE 1
#include <ArduinoJson.h>
#include <Arduino.h>
#include "Wire.h"
#include "time.h"
#include <ArduinoWebsockets.h>
#include <list>
#include <iterator>
using namespace std;
#include <TFT_eSPI.h>
#include <SPI.h>
#include "WiFi.h"
#include <Wire.h>
#include "Free_Fonts.h" // Include the header file attached to this sketchtft.setFreeFont(FSB9);

#define MAX_INCLUDE_TYPE 10
#define MAX_INCLUDE_LINE 10
#define MAX_EXCLUDE_DESTINATION 10
#define MAX_JSON_DOCUMENT 20000
#define MAX_DEPARTURE_LIST_LENGTH 100

#ifndef TFT_DISPOFF
#define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
#define TFT_SLPIN   0x10
#endif

#define TFT_MOSI            19
#define TFT_SCLK            18
#define TFT_CS              5
#define TFT_DC              16
#define TFT_RST             23

#define TFT_BL          4  // Display backlight control pin
#define ADC_EN          14
#define ADC_PIN         34
#define BUTTON_1        35
#define BUTTON_2        0

enum types {
  mvg_api,
  geops_api
};


typedef struct {
  const char *wifi_name;
  const char *wifi_pass;
  const enum types type;
  const char *bahnhof;
  const char *include_type[MAX_INCLUDE_TYPE];
  const char *include_line[MAX_INCLUDE_LINE];
  const char *exclude_destinations[MAX_EXCLUDE_DESTINATION];
} Config;
#include "config.h";

typedef struct {
  unsigned long long aimed_time;
  unsigned long long estimated_time;
  String line;
  String destination;
  int platform;
  int wagon;
} Departure;

list <Departure> departure_list;

websockets::WebsocketsClient client;

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library

StaticJsonDocument<MAX_JSON_DOCUMENT> doc;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const int   number_of_configs = sizeof(configs) / sizeof(*configs);
int config_number = -1;

int connect_wifi();
void handle_mvg_api(Config config);
void init_geops_api(Config config);
void handle_geops_api(Config config);

//! Long time delay, it is recommended to use shallow sleep, which can effectively reduce the current consumption
void espDelay(int ms)
{   
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
    esp_light_sleep_start();
}

void drawTopLine()
{ 
    tft.fillRect(0, 0, 240, 8,TFT_YELLOW);
    tft.setTextFont(GLCD);
    tft.setTextColor(0x005, TFT_YELLOW);
    tft.drawString("Linie", 1, 0);
    tft.drawString("Ziel", 33, 0);
    tft.drawString("Gleis", 128, 0);
    tft.drawString("A", 169, 0);
    tft.drawString("B", 184, 0);
    tft.drawString("C", 200, 0);
    tft.drawString("Min", 220, 0);
}

void setup_display()
{   tft.init();
    tft.setRotation(3);
    //tft.fillScreen(TFT_BLACK);
    tft.fillScreen(0x005);
    if (TFT_BL > 0) { // TFT_BL has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
         pinMode(TFT_BL, OUTPUT); // Set backlight pin to output mode
         digitalWrite(TFT_BL, TFT_BACKLIGHT_ON); // Turn backlight on. TFT_BACKLIGHT_ON has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
    }
}

void drawDeparture(int display_line, String line, String destination, int track, int wagon, int minutes)
{
  char str_buffer[10];
  int line_height = 15;
  int offset = 9;
  int y_display = display_line*line_height+offset;
    
  tft.setFreeFont(FF17);
  tft.setTextDatum(TL_DATUM);

  //line
  tft.fillRoundRect(0, y_display, 28, 15, 7,TFT_MAGENTA);
  tft.setTextColor(0x005);
  tft.drawString(line, 3, y_display); 

  //destination
  tft.setTextColor(TFT_WHITE);
  tft.drawString(destination.substring(0, 11), 32, y_display);

  //track
  sprintf(str_buffer, "%u", track);
  tft.drawString(str_buffer, 150, y_display);

  //wagon
  tft.drawString("_ _ _", 167, y_display);
  switch(wagon)
  {
    case 3:
      tft.fillRoundRect(195, y_display+4, 14, 11, 3,TFT_WHITE); //rechts
    case 2:
      tft.fillRoundRect(165, y_display+4, 14, 11, 3,TFT_WHITE); //links
    case 1:
      tft.fillRoundRect(180, y_display+4, 14, 11, 3,TFT_WHITE); //mitte
      default:
        break;
  }

  //minutes
  tft.setTextDatum(TR_DATUM);
  sprintf(str_buffer, "%u", minutes);
  tft.drawString(str_buffer, 240,y_display);
  
}

void setup()
{
  Serial.begin(115200);

  setup_display();

  delay(4000);   //Delay needed before calling the WiFi.begin

  while (config_number == -1) {
    config_number = connect_wifi();
  }
  Config loaded_config = configs[config_number];

  switch (loaded_config.type) {
    case mvg_api:
      break;
    case geops_api:
      init_geops_api(loaded_config);
      break;
    default:
      Serial.println("Unkown config type");
      break;
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


}


void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Config loaded_config = configs[config_number];
    switch (loaded_config.type)
    {
      case mvg_api:
        handle_mvg_api(loaded_config);
        break;
      case geops_api:
        handle_geops_api(loaded_config);
        break;
      default:
        Serial.println("Unkown config type");
    }
  }
  else
  {
    Serial.println("Error in WiFi connection");
  }
}

int connect_wifi()
{
  int number_of_networks = WiFi.scanNetworks();
  if (number_of_networks == -1 ) {
    Serial.println("No networks available");
  }
  for (int i = 0; i < number_of_networks; ++i) {
    String ssid = WiFi.SSID(i);
    // is this network in config...?
    for (int j = 0; j < number_of_configs; ++j) {
      if (strcmp(ssid.c_str(), configs[j].wifi_name) == 0) {
        // ... yes it is
        WiFi.begin(configs[j].wifi_name, configs[j].wifi_pass);
        while (WiFi.status() != WL_CONNECTED) {
          delay(1000);
          Serial.println("Connecting to WiFi...");
        }
        Serial.printf("Connected to the WiFi network: %s\n", ssid.c_str());
        return j;
      }
    }
  }
  return -1;
}


void handle_mvg_api(Config config)
{
  HTTPClient http;
  String url = "https://www.mvg.de/api/fahrinfo/departure/" + String(config.bahnhof) + "?footway=0";
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("New data");
    Serial.println(httpResponseCode);
    Serial.println(response);

    Serial.println("Parsing JSON...");
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
    } else {
      Serial.println("No errors");
      String servings = doc["departures"][0];
      Serial.println(servings);

      time_t now;
      time(&now);
      Serial.println(now);

      tft.fillScreen(0x005);
      drawTopLine();
      unsigned int departures_length = doc["departures"].size();
      unsigned int i = 0;
      unsigned int cnt = 0;

      while (i < departures_length && cnt < 4) {
        // Extract what we are interested in from response
        // based on config
        bool interesting_type = false;
        bool interesting_line = false;
        bool interesting_destination = true;

        for (int j = 0; j < MAX_INCLUDE_TYPE; ++j) {
          Serial.println(config.include_type[j]);
          if (config.include_type[j] && strcmp(config.include_type[j], "*") == 0) {
            // We want to see all types
            interesting_type = true;
            break;
          }
          if (config.include_type[j] && strcmp(config.include_type[j], doc["departures"][i]["product"]) == 0) {
            // We want to see this type
            interesting_type = true;
            break;
          }
        }

        for (int j = 0; j < MAX_INCLUDE_LINE; ++j) {
          Serial.println(config.include_line[j]);
          if (config.include_line[j] && strcmp(config.include_line[j], "*") == 0) {
            // We want to see all lines
            interesting_line = true;
            break;
          }
          if (config.include_line[j] && strcmp(config.include_line[j], doc["departures"][i]["label"]) == 0) {
            // We want to see this line
            interesting_line = true;
            break;
          }
        }
        if (interesting_type && interesting_line) {
          for (int j = 0; j < MAX_EXCLUDE_DESTINATION; ++j) {
            if (config.exclude_destinations[j] && strcmp(config.exclude_destinations[j], doc["departures"][i]["destination"]) == 0) {
              interesting_destination = false;
              break;
            }
          }
        }
        if (interesting_type && interesting_line && interesting_destination) {
          //Calc minutes until departure
          unsigned long departure = doc["departures"][i]["departureTime"].as<long long>() / 1000; //ms to seconds
          Serial.println(departure);

          unsigned long minutes = 0;
          if ( departure > now) {
            unsigned long wait = departure - now;
            Serial.println(wait);
            minutes = wait / 60;
            //if (wait % 60 > 30) ++minutes;
            minutes +=  doc["departures"][i]["delay"].as<int>();
          }
          Serial.println(minutes);

          drawDeparture(cnt, doc["departures"][i]["label"].as<String>(), doc["departures"][i]["destination"].as<String>(), 0, 0, minutes);

          ++cnt;
        }
        ++i;
      }
    }
  } else {
    Serial.print("Error: Couldn't send GET: ");
    Serial.println(httpResponseCode);
  }
  http.end();
  delay(30000);  //Send a request every 30 seconds
}

void init_geops_api(Config config)
{
  bool connected = client.connect("wss://tralis.sbahnm.geops.de:443/ws");
  if (connected) {
    Serial.println("Connecetd!");
    client.send("GET timetable_" + String(config.bahnhof));
    client.send("SUB timetable_" + String(config.bahnhof)); //Subscribe for Departures at Hauptbahnhof
  } else {
    Serial.println("Not Connected!");
  }

  // run callback when messages are received
  client.onMessage([&](websockets::WebsocketsMessage message) {
    Serial.println(message.data());
    Serial.println("Parsing JSON...");
    DeserializationError error = deserializeJson(doc, message.data());
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
    }
    else
    {
      Serial.println("No errors");
      if (doc["source"] == ("timetable_" + String(config.bahnhof)))
      {
        Departure received_departure;
        received_departure.aimed_time= doc["content"]["ris_aimed_time"].as<double>(); 
        received_departure.estimated_time =  doc["content"]["time"].as<double>();
        received_departure.line = doc["content"]["line"]["name"].as<String>();
        received_departure.destination = doc["content"]["to"][0].as<String>();
        received_departure.platform = doc["content"]["platform"].as<int>();
        received_departure.wagon = doc["content"]["train_type"].as<int>();

         /*
        Serial.println((unsigned long)(received_departure.aimed_time/1000.));
        Serial.println((unsigned long)(received_departure.estimated_time/1000.));
        Serial.println(received_departure.line);
        Serial.println(received_departure.destination);
        Serial.println(received_departure.platform);
        Serial.println(received_departure.wagon);
        */
        if (departure_list.empty())
        {
          Serial.println("EMPTY");
          departure_list.push_back(received_departure);
        }
        else
        {
          Serial.println("Not empty");
          list<Departure>::iterator it1;
          for (it1 = departure_list.begin(); it1 != departure_list.end(); ++it1)
          { 
            Serial.println("For loop");

            if (it1->aimed_time == received_departure.aimed_time && it1->line == received_departure.line && it1->destination == received_departure.destination) //Departure schon vorhanden => Update
            {
              Serial.println("Update");
              *it1 = received_departure;
              //Sorting to be sure we are sill in correct order
              departure_list.sort([](const Departure & a, const Departure & b) { return a.estimated_time < b.estimated_time; });
              break;
            }
            if ( next(it1, 1) == departure_list.end() && departure_list.size() < MAX_DEPARTURE_LIST_LENGTH) //Departure nicht vorhanden
            {
              Serial.println("Departure nicht vorhanden");
              list<Departure>::iterator it2;
              for (it2 = departure_list.begin(); it2 != departure_list.end(); ++it2)
              {
                if (it2->estimated_time > received_departure.estimated_time) //Element richtig einsortieren
                {
                  Serial.println("Departure insert");
                  departure_list.insert(it2, received_departure);
                  break;
                }
                if (next(it2,1) == departure_list.end()) //Element ganz hinten einfügen da größter Wert
                {
                  Serial.println("Departure push_back");
                  departure_list.push_back(received_departure);
                  break; //needed cause otherwise it2 != departure_list.end() will never be true
                }
              }
            }
          }  
        }  
      }
    }
  });
}

void handle_geops_api(Config config)
{
  if (client.available()) {
    client.poll();
  }

  time_t now;
  time(&now);

  Serial.println("New List");
  tft.fillScreen(0x005);
  drawTopLine();
  list <Departure> :: iterator it;
  it = departure_list.begin();
  int cnt = 0;
  while ( !departure_list.empty() && it != departure_list.end() && cnt < 8)
  {
    unsigned long estimated_time_s = it->estimated_time/1000;
    unsigned long minutes = 0;

    if (estimated_time_s > now) 
    {
      unsigned long wait = estimated_time_s - now;
      minutes = wait / 60;
      //if (wait % 60 > 30) ++minutes;
    }
    else //abfahrt in der vergangenheit => aus der liste entfernen
    {
      departure_list.erase (it);
    }
    
    if (it != departure_list.end())
    {
      // Serial.print(it->line);
      // Serial.print("\t");
      // Serial.print(it->destination);
      // Serial.print("\t");
      // Serial.println(minutes);
      drawDeparture(cnt, it->line, it->destination, it->platform, it->wagon, minutes);
    }
    ++cnt;
    ++it;
  }
}
