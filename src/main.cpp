#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID = "nayyar910";
const char* WIFI_PASS = "18067300";

const char* SYMBOLS[] = {
    "AMZN", "AAPL", "BMY", "MARA", "NCNO",
    "POET", "S", "SMCI", "PFE"
};
const int NUM_SYMBOLS = 9;

float prices[NUM_SYMBOLS];
float changes[NUM_SYMBOLS];
bool  valid[NUM_SYMBOLS];

int currentSymbol = 0;
unsigned long lastRefresh = 0;
unsigned long lastSwitch = 0;
const unsigned long REFRESH_MS = 5UL * 60UL * 1000UL;
const unsigned long SWITCH_MS = 6000UL;

void fetchStock(int i) {
    String url = "http://query1.finance.yahoo.com/v8/finance/chart/";
    url += SYMBOLS[i];
    url += "?interval=1d&range=1d";

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(url);
    http.addHeader("User-Agent", "Mozilla/5.0");

    int code = http.GET();
    if (code != 200) {
        valid[i] = false;
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    int pIdx = body.indexOf("\"regularMarketPrice\":");
    int cIdx = body.indexOf("\"chartPreviousClose\":");
    int pcIdx = body.indexOf("\"previousClose\":");

    if (pIdx < 0) {
        valid[i] = false;
        return;
    }

    float price = body.substring(pIdx + 21, pIdx + 35).toFloat();
    float prev = 0.0f;
    if (pcIdx >= 0) prev = body.substring(pcIdx + 17, pcIdx + 31).toFloat();
    if (prev == 0.0f && cIdx >= 0) prev = body.substring(cIdx + 21, cIdx + 35).toFloat();

    if (price > 0.0f && prev > 0.0f) {
        prices[i] = price;
        changes[i] = ((price - prev) / prev) * 100.0f;
        valid[i] = true;
    } else {
        valid[i] = false;
    }
}

void refreshAll() {
    for (int i = 0; i < NUM_SYMBOLS; i++) {
        fetchStock(i);
        delay(200);
    }
    lastRefresh = millis();
}

void drawStock(int i) {
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();

    StickCP2.Display.fillScreen(TFT_BLACK);

    // Header bar
    uint32_t theme = valid[i] ? (changes[i] >= 0 ? TFT_GREEN : TFT_RED) : TFT_ORANGE;
    StickCP2.Display.fillRect(0, 0, w, 16, theme);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(" M5 STOCKS", 4, 2);

    String rssi = (WiFi.status() == WL_CONNECTED) ? (String(WiFi.RSSI()) + "dBm") : "NO WIFI";
    StickCP2.Display.setTextDatum(top_right);
    StickCP2.Display.drawString(rssi.c_str(), w - 2, 2);

    // Card border
    StickCP2.Display.drawRoundRect(2, 18, w - 4, h - 20, 4, theme);

    if (!valid[i]) {
        StickCP2.Display.setTextFont(2);
        StickCP2.Display.setTextColor(TFT_RED);
        StickCP2.Display.setTextDatum(middle_center);
        StickCP2.Display.drawString(SYMBOLS[i], w / 2, 40);
        StickCP2.Display.setTextColor(TFT_WHITE);
        StickCP2.Display.drawString("Loading...", w / 2, 72);
        return;
    }

    // Symbol
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextFont(4);
    StickCP2.Display.setTextDatum(middle_center);
    StickCP2.Display.drawString(SYMBOLS[i], w / 2, 34);

    // Price
    char priceBuf[32];
    snprintf(priceBuf, sizeof(priceBuf), "%.2f", prices[i]);
    StickCP2.Display.drawString(priceBuf, w / 2, 72);

    // Change
    uint32_t chgColor = changes[i] >= 0 ? TFT_GREEN : TFT_RED;
    char chgBuf[32];
    snprintf(chgBuf, sizeof(chgBuf), "%+.2f%%", changes[i]);
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(chgColor);
    StickCP2.Display.drawString(chgBuf, w / 2, 105);

    // Page dots
    int dotSize = 4, spacing = 10;
    int startX = (w - (NUM_SYMBOLS * spacing - dotSize)) / 2;
    for (int j = 0; j < NUM_SYMBOLS; j++) {
        int cx = startX + j * spacing;
        if (j == i) StickCP2.Display.fillCircle(cx, h - 7, dotSize / 2 + 1, chgColor);
        else        StickCP2.Display.drawCircle(cx, h - 7, dotSize / 2, TFT_DARKGREY);
    }
}

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    StickCP2.Display.setRotation(1);
    StickCP2.Display.fillScreen(TFT_BLACK);

    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(middle_center);
    StickCP2.Display.drawString("Connecting WiFi...", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000UL) {
        delay(500);
    }

    if (WiFi.status() != WL_CONNECTED) {
        StickCP2.Display.drawString("WiFi failed", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2);
        delay(5000);
        ESP.restart();
    }

    refreshAll();
    drawStock(currentSymbol);
    lastSwitch = millis();
}

void loop() {
    StickCP2.update();

    // Button A = next stock
    if (StickCP2.BtnA.wasPressed()) {
        currentSymbol = (currentSymbol + 1) % NUM_SYMBOLS;
        drawStock(currentSymbol);
        lastSwitch = millis();
    }

    // Button B = previous stock
    if (StickCP2.BtnB.wasPressed()) {
        currentSymbol = (currentSymbol + NUM_SYMBOLS - 1) % NUM_SYMBOLS;
        drawStock(currentSymbol);
        lastSwitch = millis();
    }

    // Auto-rotate every 6 seconds
    if (millis() - lastSwitch >= SWITCH_MS) {
        currentSymbol = (currentSymbol + 1) % NUM_SYMBOLS;
        drawStock(currentSymbol);
        lastSwitch = millis();
    }

    // Refresh prices every 5 minutes
    if (millis() - lastRefresh >= REFRESH_MS) {
        refreshAll();
        drawStock(currentSymbol);
    }

    delay(50);
}
