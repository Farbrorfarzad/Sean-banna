#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <Preferences.h>   // for storing defaults across restart

// ---------- Wi-Fi (DO NOT COMMIT REAL CREDENTIALS) ----------
static const char* WIFI_SSID     = "SSID";
static const char* WIFI_PASSWORD = "PWD";

// ---------- Forward declarations ----------
static void refresh_weather_data();
static void parse_forecast_json(const String &json);
static void update_forecast_ui();
static String fetch_historical_data(String city, String parameter);
static void parse_historical_json(const String &json);
static void show_forecast_error(const char *msg);
static void show_history_error(const char *msg);
static void apply_settings_to_ui();
static void update_start_screen();

// ---------- Display / LVGL ----------
LilyGo_Class amoled;

static lv_obj_t* tileview;
static lv_obj_t* t1;
static lv_obj_t* t2;
static lv_obj_t* t1_label;
static lv_obj_t* t2_label;
static bool t2_dark = false;
static lv_obj_t* forecast_cont;

// Historical data
struct HistoryEntry {
  String date;
  float value;
};

static HistoryEntry historyData[30];
static int historyCount = 0;
static lv_obj_t* history_tile;
static lv_obj_t* history_label;
static lv_obj_t* history_slider;
static lv_obj_t* history_chart;
static lv_chart_series_t* history_series;

// Settings
static lv_obj_t* settings_tile;
static lv_obj_t* city_dropdown;
static lv_obj_t* param_dropdown;
static lv_obj_t* reset_btn;
static lv_obj_t* save_btn;

// Current selection
static String selectedCity      = "Karlskrona";
static String selectedParameter = "temperature";

// Persistence (defaults)
Preferences prefs;
static const char* NVS_NAMESPACE = "weather";
static const char* KEY_CITY      = "city";
static const char* KEY_PARAM     = "param";
static const char* DEFAULT_CITY  = "Karlskrona";
static const char* DEFAULT_PARAM = "temperature";

// Forecast model
struct ForecastEntry {
  String time;
  float  temperature;
  String symbol;
};

static ForecastEntry forecast[7];

// ---------- HTTP helper ----------
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

// ---------- Style helper ----------
static void apply_tile_colors(lv_obj_t* tile, lv_obj_t* label, bool dark)
{
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tile, dark ? lv_color_black() : lv_color_white(), 0);
  lv_obj_set_style_text_color(label, dark ? lv_color_white() : lv_color_black(), 0);
}

// ---------- UI creation ----------
static void create_ui()
{
  // Tileview root
  tileview = lv_tileview_create(lv_scr_act());
  lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

  // Tile 0: start screen
  t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  t1_label = lv_label_create(t1);
  lv_label_set_text(t1_label, "Weather app v1.0\nGroup 12");
  lv_obj_center(t1_label);
  apply_tile_colors(t1, t1_label, false);

  // Tile 1: forecast screen
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  {
    lv_obj_t* title = lv_label_create(t2);
    lv_label_set_text(title, "7-day forecast (12:00)");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    forecast_cont = lv_obj_create(t2);
    lv_obj_set_size(forecast_cont, lv_pct(100), lv_pct(80));
    lv_obj_align(forecast_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(forecast_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(forecast_cont, 8, 0);

    apply_tile_colors(t2, title, false);
  }

  // Tile 2: historical data
  history_tile = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  {
    lv_obj_t* title = lv_label_create(history_tile);
    lv_label_set_text(title, "Historical data");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Chart
    history_chart = lv_chart_create(history_tile);
    lv_obj_set_size(history_chart, lv_pct(90), lv_pct(50));
    lv_obj_align(history_chart, LV_ALIGN_CENTER, 0, 20);
    lv_chart_set_type(history_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(history_chart, 30);
    history_series = lv_chart_add_series(history_chart,
                                         lv_color_black(),
                                         LV_CHART_AXIS_PRIMARY_Y);

    // Label under / over chart
    history_label = lv_label_create(history_tile);
    lv_label_set_text(history_label, "Loading historical data...");
    lv_obj_align(history_label, LV_ALIGN_CENTER, 0, -40);

    // Slider
    history_slider = lv_slider_create(history_tile);
    lv_obj_set_width(history_slider, lv_pct(80));
    lv_obj_align(history_slider, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_slider_set_range(history_slider, 0, 29);
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

  // Tile 3: settings
  settings_tile = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
  {
    lv_obj_t* title = lv_label_create(settings_tile);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // City dropdown
    lv_obj_t* city_label = lv_label_create(settings_tile);
    lv_label_set_text(city_label, "Select city:");
    lv_obj_align(city_label, LV_ALIGN_TOP_LEFT, 10, 50);

    city_dropdown = lv_dropdown_create(settings_tile);
    lv_dropdown_set_options(city_dropdown,
      "Karlskrona\n"
      "Stockholm\n"
      "Gothenburg\n"
      "Malmo\n"
      "Kiruna");
    lv_obj_set_width(city_dropdown, 180);
    lv_obj_align(city_dropdown, LV_ALIGN_TOP_LEFT, 10, 70);

    // Parameter dropdown
    lv_obj_t* param_label = lv_label_create(settings_tile);
    lv_label_set_text(param_label, "Select parameter:");
    lv_obj_align(param_label, LV_ALIGN_TOP_LEFT, 10, 120);

    param_dropdown = lv_dropdown_create(settings_tile);
    lv_dropdown_set_options(param_dropdown,
      "temperature\n"
      "humidity\n"
      "wind speed\n"
      "air pressure");
    lv_obj_set_width(param_dropdown, 180);
    lv_obj_align(param_dropdown, LV_ALIGN_TOP_LEFT, 10, 140);

    // Reset button (US4.4)
    reset_btn = lv_btn_create(settings_tile);
    lv_obj_set_size(reset_btn, 130, 35);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_LEFT, 10, -15);
    lv_obj_t* reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset defaults");
    lv_obj_center(reset_label);
    lv_obj_add_event_cb(reset_btn, [](lv_event_t* e) {
      (void)e;
      selectedCity      = DEFAULT_CITY;
      selectedParameter = DEFAULT_PARAM;
      apply_settings_to_ui();
      refresh_weather_data();
    }, LV_EVENT_CLICKED, NULL);

    // Save button (US4.5 + US4.6)
    save_btn = lv_btn_create(settings_tile);
    lv_obj_set_size(save_btn, 130, 35);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -15);
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save as default");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
      (void)e;
      prefs.putString(KEY_CITY,  selectedCity);
      prefs.putString(KEY_PARAM, selectedParameter);
      Serial.println("Defaults saved to NVS");
    }, LV_EVENT_CLICKED, NULL);

    // Change events
    lv_obj_add_event_cb(city_dropdown, [](lv_event_t* e) {
      lv_obj_t* dd = lv_event_get_target(e);
      char buf[32];
      lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
      selectedCity = String(buf);
      Serial.printf("City changed to: %s\n", buf);
      refresh_weather_data();
    }, LV_EVENT_VALUE_CHANGED, NULL);

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

  // Make dropdown selection match loaded defaults
  apply_settings_to_ui();
}

// ---------- Settings → dropdown sync ----------
static void apply_settings_to_ui() {
  if (!city_dropdown || !param_dropdown) return;

  int cityIndex = 0;
  if (selectedCity == "Karlskrona") cityIndex = 0;
  else if (selectedCity == "Stockholm") cityIndex = 1;
  else if (selectedCity == "Gothenburg") cityIndex = 2;
  else if (selectedCity == "Malmo") cityIndex = 3;
  else if (selectedCity == "Kiruna") cityIndex = 4;
  lv_dropdown_set_selected(city_dropdown, cityIndex);

  int paramIndex = 0;
  if (selectedParameter == "temperature") paramIndex = 0;
  else if (selectedParameter == "humidity") paramIndex = 1;
  else if (selectedParameter == "wind speed") paramIndex = 2;
  else if (selectedParameter == "air pressure") paramIndex = 3;
  lv_dropdown_set_selected(param_dropdown, paramIndex);
}

// ---------- Error helpers ----------
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

// ---------- Wi-Fi ----------
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
    Serial.println("WiFi connected.");
  } else {
    Serial.println("WiFi could not connect (timeout).");
  }
}

// ---------- Forecast parsing ----------
static void parse_forecast_json(const String &json) {
  if (json.length() == 0) return;

  StaticJsonDocument<12288> doc;
  if (deserializeJson(doc, json)) return;

  JsonArray ts = doc["timeSeries"].as<JsonArray>();
  int count = 0;

  for (JsonObject obj : ts) {
    if (count >= 7) break;

    String t = obj["validTime"].as<String>();
    if (!t.endsWith("T12:00:00Z")) continue;  // only noon entries

    float temp = 0;
    int symbolCode = -1;

    for (JsonObject p : obj["parameters"].as<JsonArray>()) {
      String name = p["name"].as<String>();
      if (name == "t")      temp       = p["values"][0];
      if (name == "Wsymb2") symbolCode = p["values"][0];
    }

    forecast[count].time        = t;
    forecast[count].temperature = temp;

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

// ---------- Forecast UI ----------
static void update_start_screen() {
  if (!t1_label) return;

  String text = "Weather app v1.0\nGroup 12\n";
  text += selectedCity;

  if (forecast[0].time.length() > 0) {
    text += "\nToday 12: ";
    text += String(forecast[0].temperature, 1);
    text += " C ";
    text += forecast[0].symbol;
  }
  lv_label_set_text(t1_label, text.c_str());
}

static void update_forecast_ui() {
  if (!forecast_cont) return;

  lv_obj_clean(forecast_cont);

  bool hasData = false;
  for (int i = 0; i < 7; i++) {
    if (forecast[i].time.length() == 0) continue;
    hasData = true;

    lv_obj_t* row = lv_label_create(forecast_cont);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %.1f C  %s",
             forecast[i].time.substring(0, 10).c_str(),  // date
             forecast[i].temperature,
             forecast[i].symbol.c_str());
    lv_label_set_text(row, buf);
  }

  if (!hasData) {
    lv_obj_t* lbl = lv_label_create(forecast_cont);
    lv_label_set_text(lbl, "No forecast entries.");
  }

  // also update start screen
  update_start_screen();
}

// ---------- Historical fetching ----------
static String fetch_historical_data(String city, String parameter) {
  if (WiFi.status() != WL_CONNECTED) return "";

  String url = "https://opendata-download-metobs.smhi.se/api/version/latest/";

  // parameter -> SMHI code (US4.2B)
  if (parameter == "temperature")
    url += "parameter/1/";
  else if (parameter == "humidity")
    url += "parameter/6/";
  else if (parameter == "wind speed")
    url += "parameter/4/";
  else if (parameter == "air pressure")
    url += "parameter/9/";

  // city -> station ID (US4.3B)
  if (city == "Karlskrona")
    url += "station/65090/";
  else if (city == "Stockholm")
    url += "station/97400/";
  else if (city == "Gothenburg")
    url += "station/72420/";
  else if (city == "Malmo")
    url += "station/53300/";
  else if (city == "Kiruna")
    url += "station/180940/";

  url += "period/latest-months/data.json";

  Serial.println("[HTTP] Historical data URL: " + url);
  return http_get(url);
}

// ---------- Historical parsing ----------
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
    historyData[historyCount].date  = v["date"].as<String>();
    historyData[historyCount].value = v["value"].as<float>();
    historyCount++;
  }

  if (!history_chart || !history_series) return;

  if (historyCount == 0) {
    lv_label_set_text(history_label, "No historical data.");
    return;
  }

  lv_chart_set_point_count(history_chart, historyCount);
  for (int i = 0; i < historyCount; ++i) {
    lv_chart_set_value_by_id(history_chart, history_series, i, historyData[i].value);
  }

  // Show newest datapoint by default
  char buf[64];
  snprintf(buf, sizeof(buf), "%s  %.1f°C",
           historyData[historyCount - 1].date.c_str(),
           historyData[historyCount - 1].value);
  lv_label_set_text(history_label, buf);
  lv_slider_set_range(history_slider, 0, historyCount - 1);
  lv_slider_set_value(history_slider, historyCount - 1, LV_ANIM_OFF);
}

// ---------- Refresh after settings / timer ----------
static void refresh_weather_data() {
  if (WiFi.status() != WL_CONNECTED) {
    show_forecast_error("WiFi not connected.");
    show_history_error("WiFi not connected.");
    return;
  }

  // Forecast for selected city (lat/lon mapping)
  String forecastUrl = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/";

  if (selectedCity == "Karlskrona")
    forecastUrl += "lon/15.5866/lat/56.1612/data.json";
  else if (selectedCity == "Stockholm")
    forecastUrl += "lon/18.0686/lat/59.3293/data.json";
  else if (selectedCity == "Gothenburg")
    forecastUrl += "lon/11.9746/lat/57.7089/data.json";
  else if (selectedCity == "Malmo")
    forecastUrl += "lon/13.0038/lat/55.6050/data.json";
  else if (selectedCity == "Kiruna")
    forecastUrl += "lon/20.2251/lat/67.8558/data.json";

  String json = http_get(forecastUrl);
  if (json.length() > 0) {
    parse_forecast_json(json);
    update_forecast_ui();
  } else {
    show_forecast_error("Failed to fetch forecast data.");
  }

  // Historical data
  String histJson = fetch_historical_data(selectedCity, selectedParameter);
  if (histJson.length() > 0) {
    parse_historical_json(histJson);
  } else {
    show_history_error("Failed to fetch historical data.");
  }
}

// ---------- Arduino setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  if (!amoled.begin()) {
    Serial.println("Failed to init LilyGO AMOLED.");
    while (true) delay(1000);
  }

  prefs.begin(NVS_NAMESPACE, false);
  // load stored defaults if they exist
  selectedCity      = prefs.getString(KEY_CITY,  DEFAULT_CITY);
  selectedParameter = prefs.getString(KEY_PARAM, DEFAULT_PARAM);

  beginLvglHelper(amoled);
  create_ui();

  connect_wifi();

  if (WiFi.status() == WL_CONNECTED) {
    refresh_weather_data();
  } else {
    show_forecast_error("WiFi not connected.");
    show_history_error("WiFi not connected.");
  }
}

static uint32_t lastFetch = 0;

void loop() {
  lv_timer_handler();
  delay(5);

  // refresh every 10 minutes
  if (millis() - lastFetch > 600000UL) {
    refresh_weather_data();
    lastFetch = millis();
  }
}
