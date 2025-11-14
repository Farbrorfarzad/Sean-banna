#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <lvgl.h>

// Wi-Fi credentials (Delete these before commiting to GitHub)
static const char* WIFI_SSID     = "SSID";
static const char* WIFI_PASSWORD = "PWD";

LilyGo_Class amoled;

static lv_obj_t* tileview;
static lv_obj_t* t1;
static lv_obj_t* t2;
static lv_obj_t* t1_label;
static lv_obj_t* t2_label;
static bool t2_dark = false;  // start tile #2 in light mode
static lv_obj_t* t3;             // settings tile
static lv_obj_t* forecast_cont;  // forecast container
// Historical data /// chatgpt
struct HistoryEntry {
  String date;
  float value;
};

static HistoryEntry historyData[30];   // store up to 30 days
static int historyCount = 0;
static lv_obj_t* history_tile;
static lv_obj_t* history_label;
static lv_obj_t* history_slider;
// Settings
static lv_obj_t* settings_tile;
static lv_obj_t* city_dropdown;
static lv_obj_t* param_dropdown;

// Currently selected options
static String selectedCity = "Karlskrona";
static String selectedParameter = "temperature";




/// chatgpt
static String http_get(const String &url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Not connected to WiFi");
    return "";
  }

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[HTTP] GET failed, code: %d\n", code);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();
  return payload;
}




// Function: Tile #2 Color change
static void apply_tile_colors(lv_obj_t* tile, lv_obj_t* label, bool dark)
{
  // Background
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tile, dark ? lv_color_black() : lv_color_white(), 0);

  // Text
  lv_obj_set_style_text_color(label, dark ? lv_color_white() : lv_color_black(), 0);
}

static void on_tile2_clicked(lv_event_t* e)
{
  LV_UNUSED(e);
  t2_dark = !t2_dark;
  apply_tile_colors(t2, t2_label, t2_dark);
}

// Function: Creates UI
// Function: Creates UI
static void create_ui()
{
  // Create main tileview
  tileview = lv_tileview_create(lv_scr_act());
  lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

  // Base tiles: 0 = start, 1 = forecast
  t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);

  // Tile 1: start / welcome screen
  {
    t1_label = lv_label_create(t1);
    lv_label_set_text(t1_label, "Vader app v1.0\nGroup 12");   // change to your real group
    lv_obj_center(t1_label);
    apply_tile_colors(t1, t1_label, false);
  }

  // Tile 2: forecast screen
  {
    // Title for forecast
    lv_obj_t* title = lv_label_create(t2);
    lv_label_set_text(title, "7-day forecast");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Container for forecast rows
    forecast_cont = lv_obj_create(t2);
    lv_obj_set_size(forecast_cont, lv_pct(100), lv_pct(80));
    lv_obj_align(forecast_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(forecast_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(forecast_cont, 8, 0);

    apply_tile_colors(t2, title, false);
  }

  // Tile 3: Historical data screen
  {
    history_tile = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);

    // Title
    lv_obj_t* title = lv_label_create(history_tile);
    lv_label_set_text(title, "Historical Data");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Data label
    history_label = lv_label_create(history_tile);
    lv_label_set_text(history_label, "Loading historical data...");
    lv_obj_align(history_label, LV_ALIGN_CENTER, 0, -20);

    // Slider to scroll through history
    history_slider = lv_slider_create(history_tile);
    lv_obj_set_width(history_slider, lv_pct(80));
    lv_obj_align(history_slider, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_slider_set_range(history_slider, 0, 29); // 0 = oldest, 29 = newest
    lv_obj_add_event_cb(history_slider, [](lv_event_t* e) {
        lv_obj_t* slider = lv_event_get_target(e);
        int index = lv_slider_get_value(slider);
        if (index < historyCount) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%s  %.1f°C",
                   historyData[index].date.c_str(),
                   historyData[index].value);
          lv_label_set_text(history_label, buf);
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    apply_tile_colors(history_tile, title, false);
  }

  // Tile 4: Settings screen
  {
    settings_tile = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);

    // Title
    lv_obj_t* title = lv_label_create(settings_tile);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // --- City dropdown ---
    lv_obj_t* city_label = lv_label_create(settings_tile);
    lv_label_set_text(city_label, "Select City:");
    lv_obj_align(city_label, LV_ALIGN_TOP_LEFT, 10, 50);

    city_dropdown = lv_dropdown_create(settings_tile);
    lv_dropdown_set_options(city_dropdown,
      "Karlskrona\n"
      "Stockholm\n"
      "Gothenburg");
    lv_obj_set_width(city_dropdown, 180);
    lv_obj_align(city_dropdown, LV_ALIGN_TOP_LEFT, 10, 80);

    // --- Parameter dropdown ---
    lv_obj_t* param_label = lv_label_create(settings_tile);
    lv_label_set_text(param_label, "Select Parameter:");
    lv_obj_align(param_label, LV_ALIGN_TOP_LEFT, 10, 140);

    param_dropdown = lv_dropdown_create(settings_tile);
    lv_dropdown_set_options(param_dropdown,
      "temperature\n"
      "humidity\n"
      "wind speed");
    lv_obj_set_width(param_dropdown, 180);
    lv_obj_align(param_dropdown, LV_ALIGN_TOP_LEFT, 10, 170);

    // Event: city change
    lv_obj_add_event_cb(city_dropdown, [](lv_event_t* e) {
      lv_obj_t* dd = lv_event_get_target(e);
      char buf[32];
      lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
      selectedCity = String(buf);
      Serial.printf("City changed to: %s\n", buf);
      refresh_weather_data();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Event: parameter change
    lv_obj_add_event_cb(param_dropdown, [](lv_event_t* e) {
      lv_obj_t* dd = lv_event_get_target(e);
      char buf[32];
      lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
      selectedParameter = String(buf);
      Serial.printf("Parameter changed to: %s\n", buf);
      refresh_weather_data();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    apply_tile_colors(settings_tile, title, false);
  }
}

// Refetch forecast + history after settings change /// chatgpt
static void refresh_weather_data() {
  if (WiFi.status() != WL_CONNECTED) {
    show_forecast_error("WiFi not connected.");
    show_history_error("WiFi not connected.");
    return;
  }

  // ---- Forecast ----
  String forecastUrl = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/";

  // Simple hard-coded city mapping (lon/lat)
  if (selectedCity == "Karlskrona")
    forecastUrl += "lon/15.5866/lat/56.1612/data.json";
  else if (selectedCity == "Stockholm")
    forecastUrl += "lon/18.0686/lat/59.3293/data.json";
  else if (selectedCity == "Gothenburg")
    forecastUrl += "lon/11.9746/lat/57.7089/data.json";

  String json = http_get(forecastUrl);
  if (json.length() > 0) {
    parse_forecast_json(json);
    update_forecast_ui();
  } else {
    show_forecast_error("Failed to fetch forecast data.");
  }

  // ---- Historical ----
  String histJson = fetch_historical_data(selectedCity, selectedParameter);
  if (histJson.length() > 0) {
    parse_historical_json(histJson);
  } else {
    show_history_error("Failed to fetch historical data.");
  }
}



/// chatgpt
static void show_forecast_error(const char *msg) {
  if (!forecast_cont) return;
  lv_obj_clean(forecast_cont);

  lv_obj_t* lbl = lv_label_create(forecast_cont);
  lv_label_set_text(lbl, msg);
}

static void show_history_error(const char *msg) {
  if (history_label) {
    lv_label_set_text(history_label, msg);
  }
}


// Function: Connects to WIFI
static void connect_wifi()
{
  Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(250);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected.");
  } else {
    Serial.println("WiFi could not connect (timeout).");
  }
}

// simple model for one forecast item
struct ForecastEntry { /// chatgpt
  String time;
  float temperature;
  String symbol;
};

// storage for 7 forecast points
static ForecastEntry forecast[7];


///chat gpt
static void parse_forecast_json(const String &json) {
  if (json.length() == 0) return;

  StaticJsonDocument<12288> doc;
  if (deserializeJson(doc, json)) return;

  JsonArray ts = doc["timeSeries"].as<JsonArray>();
  int count = 0;

  for (JsonObject obj : ts) {
    if (count >= 7) break;

    String t = obj["validTime"].as<String>();
    if (!t.endsWith("T12:00:00Z")) continue;   // only noon entries

    float temp = 0;
    int symbolCode = -1;

    for (JsonObject p : obj["parameters"].as<JsonArray>()) {
      String name = p["name"].as<String>();
      if (name == "t")        temp = p["values"][0];
      if (name == "Wsymb2")   symbolCode = p["values"][0];
    }

    forecast[count].time = t;
    forecast[count].temperature = temp;

    // stupid-simple mapping of Wsymb2 → icon
    switch (symbolCode) {
      case 1: forecast[count].symbol = "☀"; break; // clear
      case 3: forecast[count].symbol = "☁"; break; // cloudy
      case 5: forecast[count].symbol = "🌧"; break; // rain
      case 9: forecast[count].symbol = "❄"; break; // snow
      case 11: forecast[count].symbol = "⛈"; break; // thunder
      default: forecast[count].symbol = "·"; break;
    }

    count++;
  }
}


/// chatgpt
static void update_forecast_ui() {
  if (!forecast_cont) return;

  lv_obj_clean(forecast_cont);

  bool hasData = false;
  for (int i = 0; i < 7; i++) {
    extern ForecastEntry forecast[]; // or make sure it's in scope
    if (forecast[i].time.length() == 0) continue;
    hasData = true;

    lv_obj_t* row = lv_label_create(forecast_cont);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %.1f C",
             forecast[i].time.substring(11,16).c_str(),
             forecast[i].temperature);
    lv_label_set_text(row, buf);
  }

  if (!hasData) {
    lv_obj_t* lbl = lv_label_create(forecast_cont);
    lv_label_set_text(lbl, "No forecast entries.");
  }
}


/// chatgpt
// Fetch latest-months historical data for city/parameter
static String fetch_historical_data(String city, String parameter) {
  if (WiFi.status() != WL_CONNECTED) return "";
  /// Base API reference: https://opendata-download-metobs.smhi.se/api.json
  // Example endpoint used below: parameter/1 (temperature), station/52350 (Karlskrona)
  String url = "https://opendata-download-metobs.smhi.se/api/version/latest/";

  // Map parameter -> SMHI code
  if (parameter == "temperature")
    url += "parameter/1/";
  else if (parameter == "humidity")
    url += "parameter/6/";
  else if (parameter == "wind speed")
    url += "parameter/4/";

  // Map city -> station ID
  if (city == "Karlskrona")
    url += "station/52350/";
  else if (city == "Stockholm")
    url += "station/98210/";
  else if (city == "Gothenburg")
    url += "station/71420/";

  url += "period/latest-months/data.json";
  Serial.println("[HTTP] Historical data URL: " + url);
  return http_get(url);
}

// Parse SMHI historical data /// chatgpt
static void parse_historical_json(const String &json) {
  if (json.length() == 0) return;

  StaticJsonDocument<16384> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.println("JSON parse error (historical)");
    return;
  }

  JsonArray values = doc["value"].as<JsonArray>();
  historyCount = 0;

  for (JsonObject v : values) {
    if (historyCount >= 30) break;
    historyData[historyCount].date = v["date"].as<String>();
    historyData[historyCount].value = v["value"].as<float>();
    historyCount++;
  }

  // Show newest value initially
  if (historyCount > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %.1f°C",
             historyData[historyCount - 1].date.c_str(),
             historyData[historyCount - 1].value);
    lv_label_set_text(history_label, buf);
    lv_slider_set_range(history_slider, 0, historyCount - 1);
    lv_slider_set_value(history_slider, historyCount - 1, LV_ANIM_OFF);
  }
}





// Must have function: Setup is run once on startup /// chatgpt
void setup() {
  Serial.begin(115200);
  delay(200);

  // display
  if (!amoled.begin()) {
    Serial.println("Failed to init LilyGO AMOLED.");
    while (true) delay(1000);
  }

  beginLvglHelper(amoled);
  create_ui();

  // wifi
  connect_wifi();

  // ===== forecast =====
  if (WiFi.status() == WL_CONNECTED) {
    String forecastUrl = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/15.5866/lat/56.1612/data.json";
    String json = http_get(forecastUrl);

    if (json.length() > 0) {
      parse_forecast_json(json);
      update_forecast_ui();
    } else {
      Serial.println("Failed to fetch forecast data.");
      show_forecast_error("No forecast data.\nCheck network.");
    }
  } else {
    Serial.println("No Wi-Fi connection, skipping forecast fetch.");
    show_forecast_error("WiFi not connected.");
  }

  // ===== historical =====
  if (WiFi.status() == WL_CONNECTED) {
    String histJson = fetch_historical_data("Karlskrona", "temperature");
    if (histJson.length() > 0) {
      parse_historical_json(histJson);
    } else {
      Serial.println("Failed to fetch historical data.");
      show_history_error("No historical data.\nCheck network.");
    }
  } else {
    Serial.println("No Wi-Fi connection, skipping historical fetch.");
    show_history_error("WiFi not connected.");
  }
}

// Must have function: Loop runs continously on device after setup
static uint32_t lastFetch = 0;

void loop() { // chat gpt
  lv_timer_handler();
  delay(5);

  if (millis() - lastFetch > 600000) {  // every 10 min
    refresh_weather_data();  // already does both
    lastFetch = millis();
  }
}
