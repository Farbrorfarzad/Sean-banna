#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <iostream>
#include "Icons/Weather_icons.h"
#include <Preferences.h>

// ------------------------------------------------------
// Wi-Fi credentials
// ------------------------------------------------------
static const char* WIFI_SSID     = "BTH_Guest";
static const char* WIFI_PASSWORD = "plommon86silver";


LilyGo_Class amoled;

lv_obj_t *screen1;            // loading screen
lv_obj_t *screen2;            // current weather
lv_obj_t *screen3;            // 7-day forecast
lv_obj_t *screen4_history;   // history graph screen
lv_obj_t *screen5_settings;  // settings screen

// --- Preferences (NVS) ---
Preferences prefs;
static const char* PREF_NAMESPACE   = "weather";
static const char* PREF_KEY_CITY    = "city_idx";
static const char* PREF_KEY_PARAM   = "param_idx";
// Info-label for "Saved!" on settings
static lv_obj_t* save_default_info_label = nullptr;
// "user default"
static int  default_city_index   = 1;   
static int  default_param_index  = 0;  

// --- City selection  ---
static const int NUM_CITIES = 5;
static const char* CITY_NAMES[NUM_CITIES] = {
  "Karlskrona",
  "Stockholm",
  "Goteborg",
  "Malmo",
  "Kiruna"
};

static const int CITY_IDS[NUM_CITIES] = {
  65090,   // Karlskrona
  97400,   // Stockholm
  72420,   // Goteborg
  53300,   // Malmo
  180940   // Kiruna
};

static const float CITY_LATS[NUM_CITIES] = {
  56.16156f,  // Karlskrona
  59.33459f,  // Stockholm
  57.70887f,  // Goteborg
  55.60587f,  // Malmo
  67.85572f   // Kiruna
};

static const float CITY_LONS[NUM_CITIES] = {
  15.58661f,  // Karlskrona
  18.06324f,  // Stockholm
  11.97456f,  // Goteborg
  13.00073f,  // Malmo
  20.22513f   // Kiruna
};

// --- Parameter selection ---
static const char* PARAM_NAMES[] = {
  "Temperature",
  "Humidity",
  "Wind speed",
  "Air pressure"
};

static const int PARAM_IDS[] = {
  1,  // Temperature
  6,  // Humidity
  4,  // Wind speed
  9   // Air pressure
};

static const char* PARAM_UNITS[] = {
  "°C",
  "%",
  "m/s",
  "hPa"
};

// indices (selected by user)
static int selected_city_index  = 1;   
static int selected_param_index = 0;   

// The currently active city 
static const char* SELECTED_CITY      = CITY_NAMES[1];
static int         SELECTED_CITY_ID   = CITY_IDS[1];
static float       SELECTED_CITY_LAT  = CITY_LATS[1];
static float       SELECTED_CITY_LON  = CITY_LONS[1];
static int         selected_param_id  = PARAM_IDS[0];

// --- History data ---
static lv_obj_t* history_chart;
static lv_obj_t* history_slider;
static lv_obj_t* history_info_label;
static lv_chart_series_t* history_series;
static lv_obj_t* history_title_label;   


static const uint16_t HISTORY_MAX_POINTS = 600;   // latest months of hourly data
static uint16_t history_count = 0;
static int16_t  history_values[HISTORY_MAX_POINTS];
static String history_timestamps[HISTORY_MAX_POINTS];   // ISO timestamps per point

// --- Settings widgets ---
static lv_obj_t* city_dropdown;
static lv_obj_t* param_dropdown;
static lv_obj_t* city_current_label;
static lv_obj_t* param_current_label;


lv_obj_t *labelWeather;       // current weather label on screen2
lv_obj_t *forecast_labels[7]; // 7 rows on screen3
lv_obj_t *currentIcon;        // icon for current weather (screen2)
lv_obj_t *forecast_icons[7];  // icons for forecast rows
lv_obj_t *labelCity; // city name on screen2
lv_obj_t *forecast_rows[7];
lv_obj_t *forecast_date_labels[7];
lv_obj_t *forecast_temp_labels[7];
String g_todayDate = "";

int g_nowSymbolCode = -1;
bool g_nowSymbolValid = false;

// fade timing / state
bool intro_started      = false;
bool transition_started = false;
unsigned long intro_start_ms = 0;

// ------------------------------------------------------
// Map Wsymb2 code -> LVGL image
// ------------------------------------------------------
const lv_img_dsc_t* wsymb2_to_icon(int code) {
    switch (code) {
        case 1:  
            return &ClearSky;
        case 2:
            return &NearlyClearSky;
        case 3:
            return &VariableCloudiness;
        case 4:  // Halfclear sky (test icon)
            return &HalfClearSky;
        case 5:  // Halfclear sky (test icon)
            return &CloudySky;
        case 6:  // Halfclear sky (test icon)
            return &Overcast;
        case 7:  // Halfclear sky (test icon)
            return &Fog;
        case 8:  // Halfclear sky (test icon)
            return &LightRainShower;
        case 9:  // Halfclear sky (test icon)
            return &ModerateRainShowers;
        case 10:  // Halfclear sky (test icon)
            return &HeavyRainShowers;
        case 11:  // Halfclear sky (test icon)
            return &Thunderstorm;
        case 12:  // Halfclear sky (test icon)
            return &LightSleetShower;
        case 13:  // Halfclear sky (test icon)
            return &ModerateSleetShowers;
        case 14:  // Halfclear sky (test icon)
            return &HeavySleetShowers;
        case 15:  // Halfclear sky (test icon)
            return &LightSnowShowers;
        case 16:  // Halfclear sky (test icon)
            return &ModerateRainShowers;
        case 17:  // Halfclear sky (test icon)
            return &HeavySnowShowers;
        case 18:  // Halfclear sky (test icon)
            return &LightRain;
        case 19:  // Halfclear sky (test icon)
            return &ModerateRain;
        case 20:  // Halfclear sky (test icon)
            return &HeavyRain;
        case 21:  // Halfclear sky (test icon)
            return &Thunder;
        case 22:  // Halfclear sky (test icon)
            return &LightSleet;
        case 23:  // Halfclear sky (test icon)
            return &ModerateSleet;
        case 24:  // Halfclear sky (test icon)
            return &HeavySleet;
        case 25:  // Halfclear sky (test icon)
            return &LightSnowfall;
        case 26:  
            return &ModerateSnowfall;
        case 27:  
            return &HeavySnowfall;
       
        default:
            return nullptr;
    }
}

void GetWeather();
void GetForecast7Days();
void gesture_event_cb(lv_event_t * e);

void create_ui() {
    // ---------- Screen 1: loading ----------
    screen1 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen1, lv_color_white(), 0);

    lv_obj_t *loading_label = lv_label_create(screen1);
    lv_label_set_text(loading_label, "Version: Espressif32@6.7.0\nGroup: 12");
    lv_obj_center(loading_label);

    // ---------- Screen 2: current weather ----------
    screen2 = lv_obj_create(NULL);
    lv_obj_set_size(screen2, LV_HOR_RES, LV_VER_RES);

    // Horizontal flex: [City] [Icon] [Temp]
    lv_obj_set_flex_flow(screen2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(screen2,
                          LV_FLEX_ALIGN_CENTER,   
                          LV_FLEX_ALIGN_CENTER,   
                          LV_FLEX_ALIGN_CENTER);  
    lv_obj_set_style_pad_all(screen2, 10, 0);
    lv_obj_set_style_pad_column(screen2, 10, 0);

    // City label on the left
    labelCity = lv_label_create(screen2);          
    lv_label_set_text(labelCity, SELECTED_CITY);

    static lv_style_t style_city;
    lv_style_init(&style_city);
    lv_style_set_text_font(&style_city, &lv_font_montserrat_30);
    lv_obj_add_style(labelCity, &style_city, LV_PART_MAIN);


    // Icon in the middle
    currentIcon = lv_img_create(screen2);
    

    // Temperature label
    labelWeather = lv_label_create(screen2);
    lv_label_set_text(labelWeather, "Loading...");
    static lv_style_t style_temp;
    lv_style_init(&style_temp);
    lv_style_set_text_font(&style_temp, &lv_font_montserrat_32);  // Text size
    lv_obj_add_style(labelWeather, &style_temp, LV_PART_MAIN);


    // ---------- Screen 3: 7-day forecast ----------
screen3 = lv_obj_create(NULL);
lv_obj_set_size(screen3, LV_HOR_RES, LV_VER_RES);

// Make screen3 a vertical flex container
lv_obj_set_flex_flow(screen3, LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(screen3,
                      LV_FLEX_ALIGN_START,  
                      LV_FLEX_ALIGN_CENTER,  
                      LV_FLEX_ALIGN_CENTER); 
lv_obj_set_style_pad_all(screen3, 10, 0);
lv_obj_set_style_pad_row(screen3, 8, 0);  // spacing between rows
lv_obj_set_scroll_dir(screen3, LV_DIR_VER); // allow vertical scrolling if needed

// Title at the top
lv_obj_t *title = lv_label_create(screen3);
lv_label_set_text(title, "7-day forecast (12:00)");
lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

// --- styles for screen 3 fonts ---
static lv_style_t style_fc_date;
static lv_style_t style_fc_temp;

lv_style_init(&style_fc_date);
lv_style_set_text_font(&style_fc_date, &lv_font_montserrat_18);  // date font size

lv_style_init(&style_fc_temp);
lv_style_set_text_font(&style_fc_temp, &lv_font_montserrat_20);  // temp font size 

// --- rows ---
for (int i = 0; i < 7; i++) {
    // Row container
    forecast_rows[i] = lv_obj_create(screen3);
    lv_obj_set_width(forecast_rows[i], LV_PCT(100));
    lv_obj_set_height(forecast_rows[i], LV_SIZE_CONTENT);

    
    lv_obj_clear_flag(forecast_rows[i], LV_OBJ_FLAG_SCROLLABLE);

    
    lv_obj_set_flex_flow(forecast_rows[i], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(forecast_rows[i],
                          LV_FLEX_ALIGN_SPACE_BETWEEN, // spread across width
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(forecast_rows[i], 6, 0);
    lv_obj_set_style_pad_column(forecast_rows[i], 10, 0);

    // Date label 
    forecast_date_labels[i] = lv_label_create(forecast_rows[i]);
    lv_label_set_text(forecast_date_labels[i], "--");
    lv_obj_add_style(forecast_date_labels[i], &style_fc_date, LV_PART_MAIN);

    // Icon 
    forecast_icons[i] = lv_img_create(forecast_rows[i]);

    // Temp label 
    forecast_temp_labels[i] = lv_label_create(forecast_rows[i]);
    lv_label_set_text(forecast_temp_labels[i], "-- °C");
    lv_obj_add_style(forecast_temp_labels[i], &style_fc_temp, LV_PART_MAIN);
}



    // ---------- Gestures ----------
    lv_obj_add_event_cb(screen1, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen2, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen3, gesture_event_cb, LV_EVENT_GESTURE, NULL);
}

void create_history_screen()
{
    screen4_history = lv_obj_create(NULL);
    lv_obj_set_size(screen4_history, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(screen4_history, 10, 0);

    // Title
    history_title_label = lv_label_create(screen4_history);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s - History", SELECTED_CITY);
    lv_label_set_text(history_title_label, buf);
    lv_obj_set_style_text_font(history_title_label, &lv_font_montserrat_28, 0);
    lv_obj_align(history_title_label, LV_ALIGN_TOP_MID, 0, 5);


    // Chart
    history_chart = lv_chart_create(screen4_history);
    lv_obj_set_size(history_chart, 480, 260);
    lv_obj_align(history_chart, LV_ALIGN_TOP_MID, 0, 40);
    lv_chart_set_type(history_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(history_chart, LV_CHART_UPDATE_MODE_SHIFT);

    history_series = lv_chart_add_series(
            history_chart,
            lv_palette_main(LV_PALETTE_BLUE),
            LV_CHART_AXIS_PRIMARY_Y);

    // Info label
    history_info_label = lv_label_create(screen4_history);
    lv_label_set_text(history_info_label, "No history loaded yet");
    lv_obj_align(history_info_label, LV_ALIGN_BOTTOM_MID, 0, -40);

    // Slider
    history_slider = lv_slider_create(screen4_history);
    lv_obj_set_width(history_slider, 460);
    lv_obj_align(history_slider, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_slider_set_range(history_slider, 0, 0);
    lv_obj_add_event_cb(history_slider, history_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_clear_flag(history_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(screen4_history, gesture_event_cb, LV_EVENT_GESTURE, NULL);
}

void refresh_history_chart()
{
  if (!history_chart || !history_series) return;
  if (history_count == 0) return;


  lv_chart_set_point_count(history_chart, history_count);
  int16_t minv = history_values[0];
  int16_t maxv = history_values[0];

  for (uint16_t i = 0; i < history_count; ++i) {
    int16_t v = history_values[i];
    if (v < minv) minv = v;
    if (v > maxv) maxv = v;
    lv_chart_set_value_by_id(history_chart, history_series, i, v);
  }

  
  int16_t pad = 2;
  lv_chart_set_range(history_chart,
                     LV_CHART_AXIS_PRIMARY_Y,
                     minv - pad,
                     maxv + pad);

  
  lv_slider_set_range(history_slider, 0, (history_count > 0) ? (history_count - 1) : 0);
  lv_slider_set_value(history_slider, history_count > 0 ? history_count - 1 : 0, LV_ANIM_OFF);

  
  history_slider_event_cb(NULL);
}


void create_settings_screen()
{
    screen5_settings = lv_obj_create(NULL);
    lv_obj_set_size(screen5_settings, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(screen5_settings, 10, 0);

    // Title
    lv_obj_t* title = lv_label_create(screen5_settings);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // City label
    lv_obj_t* lbl_city = lv_label_create(screen5_settings);
    lv_label_set_text(lbl_city, "City:");
    lv_obj_align(lbl_city, LV_ALIGN_TOP_LEFT, 10, 70);

    // City dropdown
    city_dropdown = lv_dropdown_create(screen5_settings);
    lv_dropdown_set_options(city_dropdown,
        "Karlskrona (65090)\n"
        "Stockholm (97400)\n"
        "Goteborg (72420)\n"
        "Malmo (53300)\n"
        "Kiruna (180940)");
    lv_obj_set_width(city_dropdown, 220);
    lv_obj_align(city_dropdown, LV_ALIGN_TOP_LEFT, 120, 65);
    lv_dropdown_set_selected(city_dropdown, selected_city_index);
    lv_obj_add_event_cb(city_dropdown, city_dropdown_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Current city label
    city_current_label = lv_label_create(screen5_settings);
    lv_obj_align(city_current_label, LV_ALIGN_TOP_LEFT, 10, 105);
    char buf[64];
    snprintf(buf, sizeof(buf), "Current city: %s", SELECTED_CITY);
    lv_label_set_text(city_current_label, buf);

    // Parameter label
    lv_obj_t* lbl_param = lv_label_create(screen5_settings);
    lv_label_set_text(lbl_param, "Parameter:");
    lv_obj_align(lbl_param, LV_ALIGN_TOP_LEFT, 10, 150);

    // Parameter dropdown
    param_dropdown = lv_dropdown_create(screen5_settings);
    lv_dropdown_set_options(param_dropdown,
        "Temperature (1)\n"
        "Humidity (6)\n"
        "Wind speed (4)\n"
        "Air pressure (9)");
    lv_obj_set_width(param_dropdown, 220);
    lv_obj_align(param_dropdown, LV_ALIGN_TOP_LEFT, 120, 145);
    lv_dropdown_set_selected(param_dropdown, selected_param_index);
    lv_obj_add_event_cb(param_dropdown, param_dropdown_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Current parameter label
    param_current_label = lv_label_create(screen5_settings);
    lv_obj_align(param_current_label, LV_ALIGN_TOP_LEFT, 10, 185);
    snprintf(buf, sizeof(buf), "Current parameter: %s", PARAM_NAMES[selected_param_index]);
    lv_label_set_text(param_current_label, buf);

    // Button: Save as default
    lv_obj_t* btn_save = lv_btn_create(screen5_settings);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t* btn_save_lbl = lv_label_create(btn_save);
    lv_label_set_text(btn_save_lbl, "Save as default");

    // button event
    lv_obj_add_event_cb(btn_save, [](lv_event_t* e){
        default_city_index  = selected_city_index;
        default_param_index = selected_param_index;
        save_defaults_to_nvs();

        if (save_default_info_label) {
            lv_label_set_text(save_default_info_label, "Saved!");
        }
    }, LV_EVENT_CLICKED, NULL);

    // Info label 
    save_default_info_label = lv_label_create(screen5_settings);
    lv_obj_align(save_default_info_label, LV_ALIGN_BOTTOM_LEFT, 150, -10);
    lv_label_set_text(save_default_info_label, "");
    lv_obj_add_event_cb(screen5_settings, gesture_event_cb, LV_EVENT_GESTURE, NULL);
}

void city_dropdown_event_cb(lv_event_t* e)
{
    lv_obj_t* dd = lv_event_get_target(e);
    selected_city_index = lv_dropdown_get_selected(dd);

    SELECTED_CITY     = CITY_NAMES[selected_city_index];
    SELECTED_CITY_ID  = CITY_IDS[selected_city_index];
    SELECTED_CITY_LAT = CITY_LATS[selected_city_index];
    SELECTED_CITY_LON = CITY_LONS[selected_city_index];

    char buf[64];
    snprintf(buf, sizeof(buf), "Current city: %s", SELECTED_CITY);
    lv_label_set_text(city_current_label, buf);

    
    if (labelCity) {
        lv_label_set_text(labelCity, SELECTED_CITY);
    }
    
    if (history_title_label) {
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "%s - History", SELECTED_CITY);
        lv_label_set_text(history_title_label, titleBuf);
    }


    
    if (WiFi.status() == WL_CONNECTED) {
        GetWeather();
        GetForecast7Days();
        load_history_data();
        refresh_history_chart();
  }
}

void param_dropdown_event_cb(lv_event_t* e)
{
    lv_obj_t* dd = lv_event_get_target(e);
    selected_param_index = lv_dropdown_get_selected(dd);
    selected_param_id    = PARAM_IDS[selected_param_index];

    char buf[64];
    snprintf(buf, sizeof(buf), "Current parameter: %s", PARAM_NAMES[selected_param_index]);
    lv_label_set_text(param_current_label, buf);

    
    if (WiFi.status() == WL_CONNECTED) {
        load_history_data();
        refresh_history_chart();
    }
    }

    void gesture_event_cb(lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        lv_obj_t * current = lv_event_get_target(e);

        
        if (dir == LV_DIR_LEFT) {
            if (current == screen1) {
                lv_scr_load_anim(screen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
            } else if (current == screen2) {
                lv_scr_load_anim(screen3, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
            } else if (current == screen3) {
                lv_scr_load_anim(screen4_history, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
            } else if (current == screen4_history) {
                lv_scr_load_anim(screen5_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
            }
        }

        
        else if (dir == LV_DIR_RIGHT) {
            if (current == screen5_settings) {
                lv_scr_load_anim(screen4_history, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
            } else if (current == screen4_history) {
                lv_scr_load_anim(screen3, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
            } else if (current == screen3) {
                lv_scr_load_anim(screen2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
            } else if (current == screen2) {
                lv_scr_load_anim(screen1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
            }
        }
    }


    // ------------------------------------------------------
    // WiFi connect
    // ------------------------------------------------------
    static void connect_wifi() {
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
            GetWeather();
            GetForecast7Days();
        } else {
            Serial.println("WiFi could not connect (timeout).");
            if (labelWeather) lv_label_set_text(labelWeather, "WiFi failed :(");
            for (int i = 0; i < 7; i++) {
                if (forecast_date_labels[i]) lv_label_set_text(forecast_date_labels[i], "--");
                if (forecast_temp_labels[i]) lv_label_set_text(forecast_temp_labels[i], "WiFi failed :(");
            }
        }

}

void load_defaults_from_nvs() {
  if (!prefs.begin(PREF_NAMESPACE, true)) { 
    Serial.println("NVS: namespace 'weather' not found, using defaults");
    default_city_index  = 1;
    default_param_index = 0;
  } else {
    default_city_index  = prefs.getInt(PREF_KEY_CITY,  1);
    default_param_index = prefs.getInt(PREF_KEY_PARAM, 0);
    prefs.end();
  }

  
  if (default_city_index  < 0 || default_city_index  >= NUM_CITIES) default_city_index  = 1;
  if (default_param_index < 0 || default_param_index >= 4)          default_param_index = 0;

  selected_city_index  = default_city_index;
  selected_param_index = default_param_index;

  SELECTED_CITY     = CITY_NAMES[selected_city_index];
  SELECTED_CITY_ID  = CITY_IDS[selected_city_index];
  SELECTED_CITY_LAT = CITY_LATS[selected_city_index];
  SELECTED_CITY_LON = CITY_LONS[selected_city_index];
  selected_param_id = PARAM_IDS[selected_param_index];
}


void save_defaults_to_nvs() {
  prefs.begin(PREF_NAMESPACE, false); 
  prefs.putInt(PREF_KEY_CITY,  default_city_index);
  prefs.putInt(PREF_KEY_PARAM, default_param_index);
  prefs.end();
}

// Helper: convert ISO "YYYY-MM-DDTHH:MM:SS.000Z" -> "YYYY-MM-DD HH:MM"
static String format_smhi_iso(const char* iso)
{
  if (!iso) return "";

  String s(iso);
  int tPos = s.indexOf('T');
  if (tPos < 0) {
    
    return s;
  }

  String datePart = s.substring(0, tPos);   

  
  
  if (tPos + 6 <= (int)s.length()) {
    String timePart = s.substring(tPos + 1, tPos + 6);  
    return datePart + " " + timePart;                  
  } else {
    return s; 
  }
}


bool load_history_data_from_smhi()
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("History: WiFi not connected");
    return false;
  }

  
  char url[256];
  snprintf(url, sizeof(url),
           "https://opendata-download-metobs.smhi.se/api/version/1.0/"
           "parameter/%d/station/%d/period/latest-months/data.json",
           selected_param_id,
           SELECTED_CITY_ID);

  Serial.print("History URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("History: HTTP error ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  
  DynamicJsonDocument doc(65536);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("History: JSON error: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray values = doc["value"].as<JsonArray>();
  if (values.isNull()) {
    Serial.println("History: no 'value' array");
    return false;
  }

  uint32_t total = values.size();
  Serial.print("History: total entries in latest-months = ");
  Serial.println(total);
  if (total == 0) return false;

  // ---------------------------
  // Downsample so the chart is not squished
  // ---------------------------
  uint32_t step = 1;
  if (total > HISTORY_MAX_POINTS) {
    // ceil(total / HISTORY_MAX_POINTS)
    step = (total + HISTORY_MAX_POINTS - 1) / HISTORY_MAX_POINTS;
  }

  
  uint32_t startIndex = 0;
  if (total > step * HISTORY_MAX_POINTS) {
    startIndex = total - step * HISTORY_MAX_POINTS;
  }

  history_count = 0;

  for (uint32_t i = startIndex; i < total && history_count < HISTORY_MAX_POINTS; i += step) {
    JsonObject item = values[i];
    if (item["value"].isNull()) continue;

    
    float v = item["value"];
    history_values[history_count] = (int16_t)roundf(v);

    
    String ts = "";

    JsonVariant dateVar = item["date"];
    if (!dateVar.isNull()) {
      
      if (dateVar.is<int64_t>() || dateVar.is<long>() || dateVar.is<long long>() ||
          dateVar.is<unsigned long>() || dateVar.is<unsigned long long>() ||
          dateVar.is<int>()) {

        int64_t ms  = dateVar.as<int64_t>();
        time_t sec  = (time_t)(ms / 1000);    

        struct tm *tm_info = gmtime(&sec);    
        if (tm_info) {
          char tsBuf[20];                    
          if (strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M", tm_info)) {
            ts = String(tsBuf);
          }
        }
      }
      
      else if (dateVar.is<const char*>()) {
        const char* iso = dateVar.as<const char*>();
        ts = format_smhi_iso(iso);
      }
    }

    history_timestamps[history_count] = ts;

    Serial.print("History point ");
    Serial.print(history_count);
    Serial.print("  value = ");
    Serial.print(history_values[history_count]);
    Serial.print("  date = ");
    Serial.println(history_timestamps[history_count]);

    history_count++;
  }

  Serial.print("History: points kept (downsampled) = ");
  Serial.println(history_count);

  return (history_count > 0);
}





void load_history_data()
{
  if (load_history_data_from_smhi()) {
    return;
  }

  Serial.println("History: using fallback mock data");
  history_count = HISTORY_MAX_POINTS;

  for (uint16_t i = 0; i < history_count; ++i) {
    switch (selected_param_id) {
      case 1: history_values[i] = (int16_t)(-5 + (i % 15));   break; // temp
      case 6: history_values[i] = (int16_t)(40 + (i % 60));   break; // humidity
      case 4: history_values[i] = (int16_t)(i % 15);          break; // wind
      case 9: history_values[i] = (int16_t)(980 + (i % 61));  break; // pressure
      default: history_values[i] = 0;                         break;
    }
    history_timestamps[i] = "";   
  }
}

void history_slider_event_cb(lv_event_t* e)
{
  lv_obj_t* slider = history_slider;
  if (e != NULL) {
    slider = lv_event_get_target(e);
  }

  if (history_count == 0) return;
  if (!history_chart || !history_series || !slider) return;

  uint16_t index = lv_slider_get_value(slider);
  if (index >= history_count) index = history_count - 1;

  
  lv_chart_set_x_start_point(history_chart, history_series, index);
  lv_chart_refresh(history_chart);

  if (history_info_label) {
    const char* unit = PARAM_UNITS[selected_param_index];
    const char* name = PARAM_NAMES[selected_param_index];

    String ts = history_timestamps[index];   

    char buf[160];

    
    const char* timeText = ts.length() > 0 ? ts.c_str() : "no time";

    snprintf(buf, sizeof(buf),
             "%s  |  %s: %d %s  (idx %u/%u)",
             timeText,
             name,
             history_values[index],
             unit,
             index,
             (history_count > 0) ? (history_count - 1) : 0);

    lv_label_set_text(history_info_label, buf);
  }
}


// ------------------------------------------------------
// Current weather 
// ------------------------------------------------------
void GetWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("GetWeather: no WiFi");
        if (labelWeather) lv_label_set_text(labelWeather, "No WiFi");
        return;
    }

    HTTPClient http;

    // Build URL based on selected station ID (SELECTED_CITY_ID)
    char url[256];
    snprintf(url, sizeof(url),
            "https://opendata-download-metobs.smhi.se/api/version/1.0/"
            "parameter/1/station/%d/period/latest-hour/data.json",
            SELECTED_CITY_ID);

    Serial.print("GetWeather URL: ");
    Serial.println(url);

    http.setReuse(false);
    http.setTimeout(15000);
    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept-Encoding", "identity");


    int httpCode = http.GET();
    Serial.print("GetWeather: HTTP code = ");
    Serial.println(httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("GetWeather: HTTP error %d\n", httpCode);
        if (labelWeather) lv_label_set_text(labelWeather, "HTTP error");
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    Serial.println("GetWeather: payload first 200 chars:");
    Serial.println(payload.substring(0, 200));

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("GetWeather: JSON error: %s\n", err.c_str());
        if (labelWeather) lv_label_set_text(labelWeather, "JSON error");
        return;
    }

    JsonArray values = doc["value"];
    if (values.isNull() || values.size() == 0) {
        Serial.println("GetWeather: no values");
        if (labelWeather) lv_label_set_text(labelWeather, "No data");
        return;
    }

    
    JsonObject last = values[values.size() - 1];

    JsonVariant v = last["value"];
    float temp = NAN;

    if (v.is<float>() || v.is<double>() || v.is<long>() || v.is<int>()) {
        temp = v.as<float>();
    } else if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        Serial.print("GetWeather: value as string = ");
        Serial.println(s ? s : "nullptr");
        if (s) temp = atof(s);
    } else {
        Serial.println("GetWeather: value is unexpected type");
    }

    Serial.print("GetWeather: parsed temp = ");
    Serial.println(temp);

    if (isnan(temp)) {
        Serial.println("GetWeather: temp is NaN after parsing");
        if (labelWeather) lv_label_set_text(labelWeather, "Temp missing");
        return;
    }

    
    char tempBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f °C", temp);
    Serial.print("GetWeather OK, temp = ");
    Serial.println(tempBuf);
    
    

    // City label 
    if (labelCity) {
        lv_label_set_text(labelCity, SELECTED_CITY);
    }

    // Temp label 
    if (labelWeather) {
        lv_label_set_text(labelWeather, tempBuf);
    }

    int current_sym_code = -1;
    if (g_nowSymbolValid) {
        current_sym_code = g_nowSymbolCode;   // forecast "now"
    } else {
        current_sym_code = 4; 
    }

    const lv_img_dsc_t *icon = wsymb2_to_icon(current_sym_code);

    if (currentIcon && icon) {
        lv_img_set_src(currentIcon, icon);
        lv_obj_align(currentIcon, LV_ALIGN_LEFT_MID, 10, 0);
    }
}

// ------------------------------------------------------
// Forecast 7 days
// ------------------------------------------------------
void GetForecast7Days() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("GetForecast7Days: no WiFi");
        for (int i = 0; i < 7; i++) {
            if (forecast_temp_labels[i]) lv_label_set_text(forecast_temp_labels[i], "No WiFi");
            if (forecast_date_labels[i]) lv_label_set_text(forecast_date_labels[i], "--");
            if (forecast_icons[i])       lv_obj_add_flag(forecast_icons[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Reset state for new forecast
    g_nowSymbolValid = false;
    g_nowSymbolCode  = -1;
    g_todayDate      = "";


    HTTPClient http;

    // Build URL from selected lat/lon
    char url[256];
    snprintf(url, sizeof(url),
            "https://opendata-download-metfcst.smhi.se/api/category/snow1g/"
            "version/1/geotype/point/lon/%.4f/lat/%.4f/data.json",
            SELECTED_CITY_LON, SELECTED_CITY_LAT);

    Serial.print("GetForecast7Days URL: ");
    Serial.println(url);

    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept-Encoding", "identity");


    int httpCode = http.GET();
    Serial.printf("GetForecast7Days: HTTP %d\n", httpCode);

    if (httpCode != 200) {
        for (int i = 0; i < 7; i++) {
            if (forecast_temp_labels[i]) lv_label_set_text(forecast_temp_labels[i], "HTTP error");
            if (forecast_date_labels[i]) lv_label_set_text(forecast_date_labels[i], "--");
            if (forecast_icons[i])       lv_obj_add_flag(forecast_icons[i], LV_OBJ_FLAG_HIDDEN);
        }
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    Serial.println("Forecast RAW first 200 chars:");
    Serial.println(payload.substring(0, 200));

    const int MAX_FOUND = 16;
    String dates[MAX_FOUND];
    float  temps[MAX_FOUND];
    int    symbols[MAX_FOUND];
    int    found = 0;

    auto dateAlreadyUsed = [&](const String &d) -> bool {
        for (int i = 0; i < found; i++) {
            if (dates[i] == d) return true;
        }
        return false;
    };

    int searchPos = 0;

    while (true) {
        int timePos = payload.indexOf("\"time\":\"", searchPos);
        if (timePos < 0) break;

        int dtStart = timePos + 8;
        int dtEnd   = payload.indexOf("\"", dtStart);
        if (dtEnd < 0) break;

        String dateTime = payload.substring(dtStart, dtEnd);

        if (dateTime.length() < 16) {
            searchPos = dtEnd + 1;
            continue;
        }

        String date    = dateTime.substring(0, 10);  // YYYY-MM-DD
        String hourStr = dateTime.substring(11, 13); // HH
        int hour       = hourStr.toInt();
        
        
        Serial.print("Found time entry: ");
        Serial.print(dateTime);
        Serial.print(" (date ");
        Serial.print(date);
        Serial.print(", hour ");
        Serial.print(hour);
        Serial.println(")");

        if (g_todayDate.length() == 0) {
            g_todayDate = date;
            Serial.print("Set g_todayDate = ");
            Serial.println(g_todayDate);
}

        searchPos = dtEnd + 1;

        
        int windowStart = timePos;
        int windowEnd   = payload.indexOf("\"time\":\"", dtEnd);
        if (windowEnd < 0) windowEnd = payload.length();

        String window = payload.substring(windowStart, windowEnd);

        // ---- Parse air_temperature ----
        int tNamePos = window.indexOf("\"air_temperature\"");
        if (tNamePos < 0) {
            Serial.println("No air_temperature in this block");
            continue;
        }

        int tColon = window.indexOf(":", tNamePos);
        if (tColon < 0) {
            Serial.println("No ':' after air_temperature");
            continue;
        }

        int tNumStart = tColon + 1;
        while (tNumStart < window.length() &&
               (window[tNumStart] == ' ' || window[tNumStart] == '\t')) {
            tNumStart++;
        }

        int tNumEnd = tNumStart;
        while (tNumEnd < window.length() &&
               window[tNumEnd] != ',' &&
               window[tNumEnd] != '}' &&
               window[tNumEnd] != '\n' &&
               window[tNumEnd] != '\r') {
            tNumEnd++;
        }

        String tempStr = window.substring(tNumStart, tNumEnd);
        tempStr.trim();
        float temp = tempStr.toFloat();

        Serial.print("Parsed temp: '");
        Serial.print(tempStr);
        Serial.print("' -> ");
        Serial.println(temp);

        if (isnan(temp)) {
            Serial.println("Temp is NaN, skipping");
            continue;
        }

        // ---- Parse symbol_code ----
        int symCode  = -1;
        int sNamePos = window.indexOf("\"symbol_code\"");
        if (sNamePos >= 0) {
            int sColon = window.indexOf(":", sNamePos);
            if (sColon > 0) {
                int sNumStart = sColon + 1;
                while (sNumStart < window.length() &&
                       (window[sNumStart] == ' ' || window[sNumStart] == '\t')) {
                    sNumStart++;
                }

                int sNumEnd = sNumStart;
                while (sNumEnd < window.length() &&
                       window[sNumEnd] != ',' &&
                       window[sNumEnd] != '}' &&
                       window[sNumEnd] != '\n' &&
                       window[sNumEnd] != '\r') {
                    sNumEnd++;
                }

                String symStr = window.substring(sNumStart, sNumEnd);
                symStr.trim();
                symCode = symStr.toInt();

                Serial.print("Parsed symbol_code: '");
                Serial.print(symStr);
                Serial.print("' -> ");
                Serial.println(symCode);
            }
        }

        // ---- First valid symbol = "now" symbol for screen 2 ----
        if (!g_nowSymbolValid && symCode >= 1 && symCode <= 27) {
            g_nowSymbolValid = true;
            g_nowSymbolCode  = symCode;
            Serial.print("Set g_nowSymbolCode = ");
            Serial.println(g_nowSymbolCode);
        }

        

      // ---- Only keep 12:00 entries for the 7-day forecast,
      if (hour != 12) continue;


      if (g_todayDate.length() > 0 && date == g_todayDate) {
          Serial.println("Skipping today's 12:00 entry");
          continue;
      }

      if (dateAlreadyUsed(date)) continue;

      if (found >= MAX_FOUND) {
          Serial.println("Reached MAX_FOUND 12:00 entries");
          break;
      }

      dates[found]   = date;
      temps[found]   = temp;
      symbols[found] = symCode;
      found++;


    }
    Serial.printf("GetForecast7Days: found %d distinct 12:00 days\n", found);

    if (found == 0) {
        for (int i = 0; i < 7; i++) {
            lv_label_set_text(forecast_date_labels[i], "--");
            lv_label_set_text(forecast_temp_labels[i], "No data");
            lv_obj_add_flag(forecast_icons[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // sort dates 
    int idx[16];
    for (int i = 0; i < found; i++) idx[i] = i;

    for (int i = 0; i < found - 1; i++) {
        for (int j = i + 1; j < found; j++) {
            if (dates[idx[j]].compareTo(dates[idx[i]]) < 0) {
                int tmp = idx[i];
                idx[i] = idx[j];
                idx[j] = tmp;
            }
        }
    }



    String firstDate = dates[idx[0]];
    Serial.print("First forecast date (12:00) = ");
    Serial.println(firstDate);

    
    int outRow = 0;

    for (int ii = 0; ii < found && outRow < 7; ii++) {
        int k = idx[ii];

        const lv_img_dsc_t *icon = wsymb2_to_icon(symbols[k]);

        // date text
        if (forecast_date_labels[outRow]) {
            lv_label_set_text(forecast_date_labels[outRow], dates[k].c_str());
        }

        // temp text
        char tempBuf[16];
        snprintf(tempBuf, sizeof(tempBuf), "%.1f °C", temps[k]);
        if (forecast_temp_labels[outRow]) {
            lv_label_set_text(forecast_temp_labels[outRow], tempBuf);
        }

        // icon 
        if (forecast_icons[outRow]) {
            if (icon) {
                lv_img_set_src(forecast_icons[outRow], icon);
                lv_obj_clear_flag(forecast_icons[outRow], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(forecast_icons[outRow], LV_OBJ_FLAG_HIDDEN);
            }
        }

        outRow++;
    
    }

    
    for (int i = outRow; i < 7; i++) {
        lv_label_set_text(forecast_date_labels[i], "--");
        lv_label_set_text(forecast_temp_labels[i], "No data");
        lv_obj_add_flag(forecast_icons[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!amoled.begin()) {
        Serial.println("Failed to init LilyGO AMOLED.");
        while (true) delay(1000);
    }

    beginLvglHelper(amoled);

    load_defaults_from_nvs();  
    create_history_screen();
    create_settings_screen();


    create_ui();
    connect_wifi();
    load_history_data();
    refresh_history_chart();


    lv_timer_create([](lv_timer_t * t){
        Serial.println("TIMER: Refreshing weather...");
        GetWeather();
    }, 5 * 60 * 1000, NULL);

    lv_timer_create([](lv_timer_t * t){
        GetForecast7Days();
    }, 5 * 60 * 1000, NULL);
}



void loop() {
    lv_timer_handler();

    if (!intro_started) {
        // Start from a pure black screen
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

        // Fade IN the white loading screen from black
        lv_scr_load_anim(
          screen1,
          LV_SCR_LOAD_ANIM_FADE_ON,
          2000,
          0,
          false
        );

        intro_start_ms = millis();
        intro_started = true;
    }

    delay(5);
}
