#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

// ----- Device configuration portal -----
// When no Wi-Fi is configured, the M5 will broadcast a setup AP with these
// credentials. Connect to it from a phone/laptop, open 192.168.4.1, and pick
// the available network + enter its password. Once saved, the M5 restarts and
// uses the selected network for stock data.
const char* AP_SSID     = "nayyar910";
const char* AP_PASSWORD = "18067300";

// ----- Stock ticker settings -----
const char* SYMBOLS[] = {
    "AMZN", "AAPL", "BMY", "MARA", "NCNO",
    "POET", "S", "SMCI", "PFE"
};
const int SYMBOL_COUNT = sizeof(SYMBOLS) / sizeof(SYMBOLS[0]);

const unsigned long REFRESH_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 minutes
const unsigned long DISPLAY_INTERVAL_MS = 6000UL;              // 6 s per symbol
const unsigned long REQUEST_TIMEOUT_MS  = 10000UL;
const char*         YAHOO_USER_AGENT    = "Mozilla/5.0 (M5StickStockTicker/1.0)";

// ----- State -----
struct Stock {
    const char* symbol;
    float       price         = 0.0f;
    float       previousClose = 0.0f;
    float       change        = 0.0f;
    float       pct           = 0.0f;
    bool        valid         = false;
    String      error;
};
Stock stocks[SYMBOL_COUNT];

int           currentSymbol      = 0;
unsigned long lastRefresh        = 0;
unsigned long lastDisplaySwitch  = 0;
bool          forceRefresh       = false;
WiFiManager   wm;

// ----- Formatting helpers -----
String fmtPrice(float v, int decimals = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return String(buf);
}

String fmtSigned(float v, int decimals = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%+.*f", decimals, v);
    return String(buf);
}

// ----- Display primitives -----
void showMessage(const char* title, const char* line1,
                 const char* line2 = nullptr, const char* line3 = nullptr) {
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();
    StickCP2.Display.fillScreen(TFT_BLACK);
    StickCP2.Display.fillRect(0, 0, w, 18, TFT_BLUE);
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(title, 4, 2);

    StickCP2.Display.setTextColor(TFT_WHITE);
    int y = 24;
    if (line1) {
        StickCP2.Display.drawString(line1, 6, y);
        y += 18;
    }
    if (line2) {
        StickCP2.Display.drawString(line2, 6, y);
        y += 18;
    }
    if (line3) {
        StickCP2.Display.drawString(line3, 6, y);
    }
}

void drawConfigScreen() {
    int w = StickCP2.Display.width();
    StickCP2.Display.fillScreen(TFT_BLACK);
    StickCP2.Display.fillRect(0, 0, w, 18, TFT_ORANGE);
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(" WiFi Setup", 4, 2);

    String ssid = "1. Connect: " + String(AP_SSID);
    String pass = "2. Password: " + String(AP_PASSWORD);
    String url  = "3. Open 192.168.4.1";
    String hint = "Then pick a network";

    int y = 22;
    StickCP2.Display.setTextColor(TFT_YELLOW);
    StickCP2.Display.drawString(ssid.c_str(),  4, y); y += 18;
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.drawString(pass.c_str(),  4, y); y += 18;
    StickCP2.Display.setTextColor(TFT_CYAN);
    StickCP2.Display.drawString(url.c_str(),   4, y); y += 20;
    StickCP2.Display.setTextColor(TFT_LIGHTGREY);
    StickCP2.Display.drawString(hint.c_str(),  4, y);
}

void drawHeader(uint32_t statusColor) {
    int w = StickCP2.Display.width();
    StickCP2.Display.fillRect(0, 0, w, 16, statusColor);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(" M5 STOCKS", 2, 2);

    String rssi;
    if (WiFi.status() == WL_CONNECTED) {
        rssi = String(WiFi.RSSI()) + "dBm";
    } else {
        rssi = "NO WIFI";
    }
    StickCP2.Display.setTextDatum(top_right);
    StickCP2.Display.drawString(rssi.c_str(), w - 2, 2);
}

uint32_t changeColor(float c) {
    if (c > 0.005f) return TFT_GREEN;
    if (c < -0.005f) return TFT_RED;
    return TFT_WHITE;
}

void drawChangeBar(int x, int y, int w, int h, float pct) {
    StickCP2.Display.fillRect(x, y, w, h, TFT_DARKGREY);
    uint32_t c = changeColor(pct);

    float a = (pct >= 0.0f) ? pct : -pct;
    if (a > 5.0f) a = 5.0f;
    int fill = (int)((a / 5.0f) * w);
    if (fill < 1) fill = 1;

    if (pct >= 0.0f) {
        StickCP2.Display.fillRect(x, y, fill, h, c);
    } else {
        StickCP2.Display.fillRect(x + w - fill, y, fill, h, c);
    }
}

void drawPageDots(int y, int active, uint32_t color) {
    int total = SYMBOL_COUNT;
    int dotSize = 4;
    int spacing = 10;
    int startX = (StickCP2.Display.width() - (total * spacing - dotSize)) / 2;
    for (int i = 0; i < total; i++) {
        int cx = startX + i * spacing;
        if (i == active) {
            StickCP2.Display.fillCircle(cx, y, dotSize / 2 + 1, color);
        } else {
            StickCP2.Display.drawCircle(cx, y, dotSize / 2, TFT_DARKGREY);
        }
    }
}

void drawSymbolCard(int index) {
    Stock& s = stocks[index];
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();

    StickCP2.Display.fillScreen(TFT_BLACK);

    uint32_t theme = s.valid ? changeColor(s.change) : TFT_ORANGE;
    drawHeader(theme);
    StickCP2.Display.drawRoundRect(2, 18, w - 4, h - 20, 4, theme);

    if (!s.valid) {
        StickCP2.Display.setTextFont(2);
        StickCP2.Display.setTextColor(TFT_RED);
        StickCP2.Display.setTextDatum(middle_center);
        StickCP2.Display.drawString(s.symbol, w / 2, 40);
        StickCP2.Display.setTextColor(TFT_WHITE);
        StickCP2.Display.drawString(s.error.c_str(), w / 2, 72);
        drawPageDots(h - 6, index, TFT_WHITE);
        return;
    }

    // Symbol
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextFont(4);
    StickCP2.Display.setTextDatum(middle_center);
    StickCP2.Display.drawString(s.symbol, w / 2, 34);

    // Price
    String price = fmtPrice(s.price, 2);
    StickCP2.Display.drawString(price.c_str(), w / 2, 72);

    // Change / percent
    uint32_t chgColor = changeColor(s.change);
    String changeStr = fmtSigned(s.change, 2) + "  (" + fmtSigned(s.pct, 2) + "%)";
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(chgColor);
    StickCP2.Display.drawString(changeStr.c_str(), w / 2, 105);

    // Change bar
    int barW = w - 40;
    int barH = 6;
    int barX = 20;
    int barY = h - 20;
    drawChangeBar(barX, barY, barW, barH, s.pct);

    // Page dots
    drawPageDots(h - 7, index, chgColor);
}

// ----- Network / stock fetch -----
void fetchStock(int i) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://query1.finance.yahoo.com/v8/finance/chart/";
    url += SYMBOLS[i];
    url += "?interval=1d&range=1d&includeAdjustedClose=true&events=history";

    http.setTimeout(REQUEST_TIMEOUT_MS);
    http.useHTTP10(true);
    http.begin(client, url);
    http.addHeader("User-Agent", YAHOO_USER_AGENT);

    int code = http.GET();
    if (code == 200) {
        StaticJsonDocument<128> filter;
        filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
        filter["chart"]["result"][0]["meta"]["previousClose"]      = true;
        filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;

        DynamicJsonDocument doc(1024);
        auto err = deserializeJson(doc, http.getStream(),
                                   DeserializationOption::Filter(filter));
        if (!err) {
            JsonObject meta = doc["chart"]["result"][0]["meta"];
            float price = meta["regularMarketPrice"]    | 0.0f;
            float prev  = meta["previousClose"]         |
                          meta["chartPreviousClose"]    | 0.0f;

            stocks[i].price = price;
            stocks[i].previousClose = prev;
            if (prev > 0.0f) {
                stocks[i].change = price - prev;
                stocks[i].pct    = (stocks[i].change / prev) * 100.0f;
            } else {
                stocks[i].change = 0.0f;
                stocks[i].pct    = 0.0f;
            }
            stocks[i].valid = (price > 0.0f && prev > 0.0f);
            if (!stocks[i].valid) {
                stocks[i].error = "No price data";
            }
        } else {
            stocks[i].valid = false;
            stocks[i].error = "Parse error";
            Serial.print("JSON error: ");
            Serial.println(err.c_str());
        }
    } else {
        stocks[i].valid = false;
        stocks[i].error = "HTTP " + String(code);
        Serial.print("HTTP error ");
        Serial.print(code);
        Serial.print(" for ");
        Serial.println(SYMBOLS[i]);
    }
    http.end();
}

void refreshAll() {
    showMessage("Updating prices", "Please wait...");
    for (int i = 0; i < SYMBOL_COUNT; i++) {
        fetchStock(i);
        delay(250); // brief pause between requests
    }
    lastRefresh = millis();
    drawSymbolCard(currentSymbol);
    lastDisplaySwitch = millis();
}

// ----- WiFiManager callbacks -----
void configModeCallback(WiFiManager* myWM) {
    drawConfigScreen();
}

void saveConfigCallback() {
    showMessage("Saved", "Connecting to WiFi...");
}

// ----- setup / loop -----
void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    Serial.begin(115200);

    StickCP2.Display.setRotation(1);
    StickCP2.Display.setBrightness(80);
    StickCP2.Display.fillScreen(TFT_BLACK);

    for (int i = 0; i < SYMBOL_COUNT; i++) {
        stocks[i].symbol = SYMBOLS[i];
    }

    showMessage("WiFi", "Starting setup portal", "if not connected...");
    delay(800);

    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(180);
    wm.setAPCallback(configModeCallback);
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setDebugOutput(false);

    bool connected = wm.autoConnect(AP_SSID, AP_PASSWORD);
    if (!connected) {
        showMessage("WiFi failed", "Restarting in 3s...");
        delay(3000);
        ESP.restart();
    }

    showMessage("Connected", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    delay(1200);

    refreshAll();
    lastDisplaySwitch = millis();
}

void loop() {
    StickCP2.update();

    // Button A: next symbol
    if (StickCP2.BtnA.wasPressed()) {
        currentSymbol = (currentSymbol + 1) % SYMBOL_COUNT;
        lastDisplaySwitch = millis();
        drawSymbolCard(currentSymbol);
    }

    // Button B: force refresh
    if (StickCP2.BtnB.wasPressed()) {
        forceRefresh = true;
    }

    // Auto-rotate display
    if (millis() - lastDisplaySwitch >= DISPLAY_INTERVAL_MS) {
        currentSymbol = (currentSymbol + 1) % SYMBOL_COUNT;
        lastDisplaySwitch = millis();
        drawSymbolCard(currentSymbol);
    }

    // Auto-refresh prices every 5 minutes (or on demand)
    if (forceRefresh || (millis() - lastRefresh >= REFRESH_INTERVAL_MS)) {
        forceRefresh = false;
        refreshAll();
    }

    delay(20);
}
