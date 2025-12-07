#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <Preferences.h>   // US4.6 – store defaults across restart

// ====================================================================
// Wi-Fi credentials  (DO NOT COMMIT REAL PASSWORDS TO GITHUB)
// ====================================================================
// >>>> TODO: put your real SSID/PWD here while developing,
// >>>> then restore these dummy values before pushing to GitHub.
static const char* WIFI_SSID     = "SSID";
static const char* WIFI_PASSWORD = "PWD";

// ====================================================================
// Global display / LVGL objects
// ====================================================================
LilyGo_Class amoled;

static lv_obj_t* tileview;
static lv_obj_t* t1;
static lv_obj_t* t2;
static lv_obj_t* t1_label;
static lv_obj_t* t2_label;
static bool       t2_dark = false;

static lv_obj_t* history_tile;
static lv_obj_t* history_label;
static lv_obj_t* history_slider;

static lv_obj_t* settings_tile;
static lv_obj_t* city_dropdown;
static lv_obj_t* param_dropdown;
static lv_obj_t* btn_reset_defaults;
static lv_obj_t* btn_save_defaults;

static lv_obj_t* forecast_cont;   // container with forecast rows

// ====================================================================
// Persistent settings (US4.4 / US4.5 / US4.6)
// ====================================================================
Preferences prefs;
static const char* PREF_NAMESPACE  = "weather";
static const char* PREF_KEY_CITY   = "city";
static const char* PREF_KEY_PARAM  = "param";

// Built-in defaults (used if nothing is stored yet)
static const char* DEFAULT_CITY    = "Karlskrona";
static const char* DEFAULT_PARAM   = "temperature";

// Currently selected options in the app (US4.1 / US4.2B / US4.3B)
static String selectedCity       = DEFAULT_CITY;
static String selectedParameter  = DEFAULT_PARAM;

// ====================================================================
// Data models
// ====================================================================

// Historical data (US3.1 / US3.2D)
struct HistoryEntry {
  String date;
  float  value;
};

static HistoryEntry historyData[30];   // latest 30 points
static int historyCount = 0;

// Forecast data (US1.2C)
struct ForecastEntry {
  String time;        // ISO time
  float  temperature;
  String symbol;      // simple icon from Wsymb2
};

static ForecastEntry forecast[7];      // 7 days at 12:00

// ====================================================================
// Small helpers
// ====================================================================

// Generic HTTP GET (used by forecast + historical) – SMHI endpoints
// US1.2C + US3.1 + US3.2D
String http_get(const String& url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Not connected to WiFi");
    return "";
  }

  Serial.println("[HTTP] GET " + url);

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

// UI theming for tiles (original demo behavior)
void apply_tile_colors(lv_obj_t* tile, lv_obj_t* label, bool dark) {
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tile, dark ? lv_color_black() : lv_color_white(), 0);
  lv_obj_set_style_text_color(label, dark ? lv_color_white() : lv_color_black(), 0);
}

// Forecast / history error helpers – US1.3 / US3.1
void show_forecast_error(const char* msg) {
  if (!forecast_cont) return;
  lv_obj_clean(forecast_cont);
  lv_obj_t* lbl = lv_label_create(forecast_cont);
  lv_label_set_text(lbl, msg);
}

void show_history_error(const char* msg) {
  if (history_label) {
    lv_label_set_text(history_label, msg);
  }
}

// ====================================================================
// Persistent settings helpers  (US4.4 / US4.5 / US4.6)
// ====================================================================

// Map city name -> dropdown index (US4.3B)
int cityIndexFromName(const String& name) {
  if (name == "Karlskrona") return 0;
  if (name == "Stockholm")  return 1;
  if (name == "Göteborg")   return 2;
  if (name == "Malmö")      return 3;
  if (name == "Kiruna")     return 4;
  return 0; // fallback
}

// Map parameter name -> dropdown index (US4.2B)
int paramIndexFromName(const String& name) {
  if (name == "temperature") return 0;
  if (name == "humidity")    return 1;
  if (name == "wind speed")  return 2;
  if (name == "air pressure")return 3;
  return 0;
}

// Apply current selectedCity / selectedParameter to the dropdowns
// (called after create_ui and when Reset button is used)
void apply_settings_to_ui_dropdowns() {
  if (city_dropdown) {
    lv_dropdown_set_selected(city_dropdown, cityIndexFromName(selectedCity));
  }
  if (param_dropdown) {
    lv_dropdown_set_selected(param_dropdown, paramIndexFromName(selectedParameter));
  }
}

// Load defaults from NVS (US4.6)
void load_settings_from_nvs() {
  String city  = prefs.getString(PREF_KEY_CITY, DEFAULT_CITY);
  String param = prefs.getString(PREF_KEY_PARAM, DEFAULT_PARAM);

  selectedCity      = city;
  selectedParameter = param;

  Serial.printf("[SETTINGS] Loaded defaults: city=%s, param=%s\n",
                selectedCity.c_str(), selectedParameter.c_str());
}

// Save current selection as defaults in NVS (US4.5)
void save_settings_to_nvs() {
  prefs.putString(PREF_KEY_CITY,  selectedCity);
  prefs.putString(PREF_KEY_PARAM, selectedParameter);
  Serial.printf("[SETTINGS] Saved defaults: city=%s, param=%s\n",
                selectedCity.c_str(), selectedParameter.c_str());
}

// ====================================================================
// Forward declarations of functions used in event callbacks
// ====================================================================
void refresh_weather_data();

// ====================================================================
// UI creation – tiles & controls
//   US1.1C  – start screen
//   US1.3   – forecast screen
//   US3.1   – historical data screen
//   US4.1   – settings screen (city + parameter)
//   US3.2D  – interactive history slider
//   US4.2B / US4.3B – dropdowns for parameters/cities
//   US4.4 / US4.5   – reset/save buttons
// ====================================================================
void create_ui() {
  // Main tileview
  tileview = lv_tileview_create(lv_scr_act());
  lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

  // Tile 0 – start, 1 – forecast, 2 – history, 3 – settings
  t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  history_tile  = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  settings_tile = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);

  // --- US1.1C: Start screen with version + group ---
  {
    t1_label = lv_label_create(t1);
    lv_label_set_text(t1_label, "Vader app v1.0\nGroup 12");   // adjust group number if needed
    lv_obj_center(t1_label);
    apply_tile_colors(t1, t1_label, false);
  }

  // --- US1.3 + US1.2C: Forecast screen (list of 7 days) ---
  {
    lv_obj_t* title = lv_label_create(t2);
    lv_label_set_text(title, "7-day forecast");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    forecast_cont = lv_obj_create(t2);
    lv_obj_set_size(forecast_cont, lv_pct(100), lv_pct(80));
    lv_obj_align(forecast_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(forecast_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(forecast_cont, 8, 0);

    apply_tile_colors(t2, title, false);
  }

  // --- US3.1 + US3.2D: Historical data screen + slider ---
  {
    lv_obj_t* title = lv_label_create(history_tile);
    lv_label_set_text(title, "Historical Data");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    history_label = lv_label_create(history_tile);
    lv_label_set_text(history_label, "Loading historical data...");
    lv_obj_align(history_label, LV_ALIGN_CENTER, 0, -20);

    history_slider = lv_slider_create(history_tile);
    lv_obj_set_width(history_slider, lv_pct(80));
    lv_obj_align(history_slider, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_slider_set_range(history_slider, 0, 29); // 0..N-1

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

  // --- US4.1 / US4.2B / US4.3B / US4.4 / US4.5: Settings screen ---
  {
    lv_obj_t* title = lv_label_create(settings_tile);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // City dropdown – US4.3B (5 cities)
    lv_obj_t* city_label = lv_label_create(settings_tile);
    lv_label_set_text(city_label, "Select City:");
    lv_obj_align(city_label, LV_ALIGN_TOP_LEFT, 10, 50);

    city_dropdown = lv_dropdown_create(settings_tile);
    // NOTE: these names must match the ones used in cityIndexFromName()
    lv_dropdown_set_options(city_dropdown,
                            "Karlskrona\n"
                            "Stockholm\n"
                            "Göteborg\n"
                            "Malmö\n"
                            "Kiruna");
    lv_obj_set_width(city_dropdown, 180);
    lv_obj_align(city_dropdown, LV_ALIGN_TOP_LEFT, 10, 80);

    // Parameter dropdown – US4.2B (4 parameters: 1,6,4,9)
    lv_obj_t* param_label = lv_label_create(settings_tile);
    lv_label_set_text(param_label, "Select Parameter:");
    lv_obj_align(param_label, LV_ALIGN_TOP_LEFT, 10, 140);

    param_dropdown = lv_dropdown_create(settings_tile);
    lv_dropdown_set_options(param_dropdown,
                            "temperature\n"   // 1
                            "humidity\n"      // 6
                            "wind speed\n"    // 4
                            "air pressure");  // 9
    lv_obj_set_width(param_dropdown, 180);
    lv_obj_align(param_dropdown, LV_ALIGN_TOP_LEFT, 10, 170);

    // Make dropdowns reflect current selectedCity/Parameter (from NVS or defaults)
    apply_settings_to_ui_dropdowns();

    // Handle city change
    lv_obj_add_event_cb(city_dropdown, [](lv_event_t* e) {
      lv_obj_t* dd = lv_event_get_target(e);
      char buf[32];
      lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
      selectedCity = String(buf);
      Serial.printf("[UI] City changed to: %s\n", buf);
      refresh_weather_data();    // US2.1 + US1.2C + US3.1 respond to change
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Handle parameter change
    lv_obj_add_event_cb(param_dropdown, [](lv_event_t* e) {
      lv_obj_t* dd = lv_event_get_target(e);
      char buf[32];
      lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
      selectedParameter = String(buf);
      Serial.printf("[UI] Parameter changed to: %s\n", buf);
      refresh_weather_data();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // US4.4: Reset city/parameter to stored defaults
    btn_reset_defaults = lv_btn_create(settings_tile);
    lv_obj_set_size(btn_reset_defaults, 110, 35);
    lv_obj_align(btn_reset_defaults, LV_ALIGN_BOTTOM_LEFT, 10, -15);
    lv_obj_t* lbl_reset = lv_label_create(btn_reset_defaults);
    lv_label_set_text(lbl_reset, "Reset");
    lv_obj_center(lbl_reset);

    lv_obj_add_event_cb(btn_reset_defaults, [](lv_event_t* e) {
      (void)e;
      // Reload defaults from NVS, apply to UI, refresh data
      load_settings_from_nvs();
      apply_settings_to_ui_dropdowns();
      refresh_weather_data();
    }, LV_EVENT_CLICKED, NULL);

    // US4.5: Save current selection as new defaults
    btn_save_defaults = lv_btn_create(settings_tile);
    lv_obj_set_size(btn_save_defaults, 140, 35);
    lv_obj_align(btn_save_defaults, LV_ALIGN_BOTTOM_LEFT, 140, -15);
    lv_obj_t* lbl_save = lv_label_create(btn_save_defaults);
    lv_label_set_text(lbl_save, "Save as default");
    lv_obj_center(lbl_save);

    lv_obj_add_event_cb(btn_save_defaults, [](lv_event_t* e) {
      (void)e;
      save_settings_to_nvs();
    }, LV_EVENT_CLICKED, NULL);

    apply_tile_colors(settings_tile, title, false);
  }
}

// ====================================================================
// Wi-Fi connection helper
// ====================================================================
void connect_wifi() {
  Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected.");
  } else {
    Serial.println("WiFi could not connect (timeout).");
  }
}

// ====================================================================
// Forecast parsing & UI  (US1.2C / US1.3)
// ====================================================================

void parse_forecast_json(const String& json) {
  if (json.length() == 0) return;

  StaticJsonDocument<12288> doc;   // WARNING: deprecated type, but works
  if (deserializeJson(doc, json)) {
    Serial.println("[JSON] Forecast parse failed");
    return;
  }

  JsonArray ts = doc["timeSeries"].as<JsonArray>();
  int count = 0;

  for (JsonObject obj : ts) {
    if (count >= 7) break;

    String t = obj["validTime"].as<String>();
    if (!t.endsWith("T12:00:00Z")) continue;   // only 12:00 entries

    float temp = 0;
    int   symbolCode = -1;

    for (JsonObject p : obj["parameters"].as<JsonArray>()) {
      String name = p["name"].as<String>();
      if (name == "t")       temp       = p["values"][0];
      if (name == "Wsymb2")  symbolCode = p["values"][0];
    }

    forecast[count].time        = t;
    forecast[count].temperature = temp;

    // Tiny mapping from Wsymb2 to icons
    switch (symbolCode) {
      case 1:  forecast[count].symbol = "☀"; break; // clear
      case 3:  forecast[count].symbol = "☁"; break; // cloudy
      case 5:  forecast[count].symbol = "🌧"; break; // rain
      case 9:  forecast[count].symbol = "❄"; break; // snow
      case 11: forecast[count].symbol = "⛈"; break; // thunder
      default: forecast[count].symbol = "·"; break;
    }

    count++;
  }
}

void update_forecast_ui() {
  if (!forecast_cont) return;

  lv_obj_clean(forecast_cont);

  bool hasData = false;
  for (int i = 0; i < 7; i++) {
    if (forecast[i].time.length() == 0) continue;
    hasData = true;

    lv_obj_t* row = lv_label_create(forecast_cont);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %.1f C %s",
             forecast[i].time.substring(0, 10).c_str(),     // date
             forecast[i].temperature,
             forecast[i].symbol.c_str());
    lv_label_set_text(row, buf);
  }

  if (!hasData) {
    lv_obj_t* lbl = lv_label_create(forecast_cont);
    lv_label_set_text(lbl, "No forecast entries.");
  }
}

// ====================================================================
// Historical data (US3.1 / US3.2D / US4.2B / US4.3B)
// ====================================================================

// Fetch latest-months historical data from SMHI Metobs API.
// Base API description: https://opendata-download-metobs.smhi.se/api.json
String fetch_historical_data(const String& city, const String& parameter) {
  if (WiFi.status() != WL_CONNECTED) return "";

  String url = "https://opendata-download-metobs.smhi.se/api/version/latest/";

  // Parameter -> SMHI code (US4.2B)
  if (parameter == "temperature")
    url += "parameter/1/";   // temperature
  else if (parameter == "humidity")
    url += "parameter/6/";   // relative humidity
  else if (parameter == "wind speed")
    url += "parameter/4/";   // wind speed
  else if (parameter == "air pressure")
    url += "parameter/9/";   // air pressure
  else
    url += "parameter/1/";   // fallback

  // City -> station ID (US4.3B)
  if (city == "Karlskrona")
    url += "station/65090/";
  else if (city == "Stockholm")
    url += "station/97400/";
  else if (city == "Göteborg")
    url += "station/72420/";
  else if (city == "Malmö")
    url += "station/53300/";
  else if (city == "Kiruna")
    url += "station/180940/";
  else
    url += "station/65090/"; // fallback Karlskrona

  url += "period/latest-months/data.json";

  return http_get(url);
}

void parse_historical_json(const String& json) {
  if (json.length() == 0) return;

  StaticJsonDocument<16384> doc;   // deprecated but works
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.println("[JSON] Historical parse error");
    return;
  }

  JsonArray values = doc["value"].as<JsonArray>();
  historyCount = 0;

  for (JsonObject v : values) {
    if (historyCount >= 30) break;
    historyData[historyCount].date  = v["date"].as<String>();
    historyData[historyCount].value = v["value"].as<float>();
    historyCount++;
  }

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

// ====================================================================
// Combined refresh for forecast + historical (US1.2C / US3.1 / US4.x)
// ====================================================================
void refresh_weather_data() {
  if (WiFi.status() != WL_CONNECTED) {
    show_forecast_error("WiFi not connected.");
    show_history_error("WiFi not connected.");
    return;
  }

  // ---- Forecast ----  (SMHI forecast API – URL pattern)
  String forecastUrl = "https://opendata-download-metfcst.smhi.se/api/"
                       "category/pmp3g/version/2/geotype/point/";

  // City -> lon/lat for forecast (rough values)
  if (selectedCity == "Karlskrona")
    forecastUrl += "lon/15.5866/lat/56.1612/data.json";
  else if (selectedCity == "Stockholm")
    forecastUrl += "lon/18.0686/lat/59.3293/data.json";
  else if (selectedCity == "Göteborg")
    forecastUrl += "lon/11.9746/lat/57.7089/data.json";
  else if (selectedCity == "Malmö")
    forecastUrl += "lon/13.0038/lat/55.6050/data.json";
  else if (selectedCity == "Kiruna")
    forecastUrl += "lon/20.2253/lat/67.8558/data.json";
  else
    forecastUrl += "lon/15.5866/lat/56.1612/data.json"; // fallback Karlskrona

  String forecastJson = http_get(forecastUrl);
  if (forecastJson.length() > 0) {
    parse_forecast_json(forecastJson);
    update_forecast_ui();
  } else {
    show_forecast_error("Failed to fetch forecast data.");
  }

  // ---- Historical ---- (US3.1 / US3.2D / US4.2B / US4.3B)
  String histJson = fetch_historical_data(selectedCity, selectedParameter);
  if (histJson.length() > 0) {
    parse_historical_json(histJson);
  } else {
    show_history_error("Failed to fetch historical data.");
  }
}

// ====================================================================
// Setup & loop
// ====================================================================
static uint32_t lastFetch = 0;   // for periodic refresh

void setup() {
  Serial.begin(115200);
  delay(200);

  // Open NVS for persistent settings – US4.6
  prefs.begin(PREF_NAMESPACE, false);
  load_settings_from_nvs();   // fills selectedCity / selectedParameter

  // Init display + LVGL
  if (!amoled.begin()) {
    Serial.println("Failed to init LilyGO AMOLED.");
    while (true) delay(1000);
  }
  beginLvglHelper(amoled);
  create_ui();                       // builds tiles + dropdowns
  apply_settings_to_ui_dropdowns();  // sync UI with loaded settings

  // Wi-Fi & initial data fetch
  connect_wifi();
  refresh_weather_data();            // uses selectedCity/Parameter
}

void loop() {
  lv_timer_handler();
  delay(5);

  // Periodic refresh every 10 minutes – US1.2C / US3.1 keep data fresh
  if (millis() - lastFetch > 600000) {
    refresh_weather_data();
    lastFetch = millis();
  }
}
