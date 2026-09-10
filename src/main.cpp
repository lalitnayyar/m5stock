#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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
int viewMode = 0;  // 0 = card, 1 = list
unsigned long lastRefresh = 0;
unsigned long lastSwitch = 0;
const unsigned long REFRESH_MS = 5UL * 60UL * 1000UL;
const unsigned long SWITCH_MS = 6000UL;

// Global SSL client with small buffers to avoid heap corruption
WiFiClientSecure gSecure;

void fetchStock(int i) {
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/";
    url += SYMBOLS[i];
    url += "?interval=1d&range=1d";

    gSecure.stop();
    gSecure.setInsecure();

    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(10000);
    http.setConnectTimeout(8000);
    http.begin(gSecure, url);
    http.addHeader("User-Agent", "Mozilla/5.0");

    int code = http.GET();
    Serial.printf("Fetch %s: HTTP %d\n", SYMBOLS[i], code);
    if (code != 200) {
        valid[i] = false;
        http.end();
        delay(100);
        return;
    }

    String body = http.getString();
    http.end();
    delay(100);

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
        Serial.printf("  %s: $%.2f (%.2f%%)\n", SYMBOLS[i], price, changes[i]);
    } else {
        valid[i] = false;
        Serial.println("  parse failed");
    }
}

void refreshAll() {
    for (int i = 0; i < NUM_SYMBOLS; i++) {
        fetchStock(i);
        delay(300);
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

void drawList() {
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Header
    StickCP2.Display.fillRect(0, 0, w, 16, TFT_BLUE);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(" M5 STOCKS", 4, 2);
    String rssi = (WiFi.status() == WL_CONNECTED) ? (String(WiFi.RSSI()) + "dBm") : "NO WIFI";
    StickCP2.Display.setTextDatum(top_right);
    StickCP2.Display.drawString(rssi.c_str(), w - 2, 2);

    // Column headers
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.setTextColor(TFT_ORANGE);
    StickCP2.Display.drawString("SYM", 4, 20);
    StickCP2.Display.drawString("PRICE", 80, 20);
    StickCP2.Display.drawString("CHG%", 160, 20);

    // Stock rows
    int y = 36;
    int rowH = 16;
    for (int i = 0; i < NUM_SYMBOLS; i++) {
        StickCP2.Display.setTextColor(i == currentSymbol ? TFT_CYAN : TFT_WHITE);
        StickCP2.Display.drawString(SYMBOLS[i], 4, y);

        if (valid[i]) {
            StickCP2.Display.setTextColor(TFT_WHITE);
            char priceBuf[16];
            snprintf(priceBuf, sizeof(priceBuf), "%.2f", prices[i]);
            StickCP2.Display.drawString(priceBuf, 80, y);

            uint32_t c = changes[i] >= 0 ? TFT_GREEN : TFT_RED;
            StickCP2.Display.setTextColor(c);
            char chgBuf[16];
            snprintf(chgBuf, sizeof(chgBuf), "%+.2f%%", changes[i]);
            StickCP2.Display.drawString(chgBuf, 160, y);
        } else {
            StickCP2.Display.setTextColor(TFT_RED);
            StickCP2.Display.drawString("--", 80, y);
        }
        y += rowH;
    }
}

void redraw() {
    if (viewMode == 0) drawStock(currentSymbol);
    else               drawList();
}

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    Serial.begin(115200);
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

    StickCP2.Display.drawString("Fetching prices...", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2);
    refreshAll();
    redraw();
    lastSwitch = millis();
}

void loop() {
    StickCP2.update();

    // Button A = next stock (or scroll in list)
    if (StickCP2.BtnA.wasPressed()) {
        currentSymbol = (currentSymbol + 1) % NUM_SYMBOLS;
        redraw();
        lastSwitch = millis();
    }

    // Button B = previous stock
    if (StickCP2.BtnB.wasPressed()) {
        currentSymbol = (currentSymbol + NUM_SYMBOLS - 1) % NUM_SYMBOLS;
        redraw();
        lastSwitch = millis();
    }

    // Hold Button B to toggle card/list view
    if (StickCP2.BtnB.wasHold()) {
        viewMode = (viewMode + 1) % 2;
        redraw();
        lastSwitch = millis();
        delay(300);
    }

    // Auto-rotate every 6 seconds (card view only)
    if (viewMode == 0 && millis() - lastSwitch >= SWITCH_MS) {
        currentSymbol = (currentSymbol + 1) % NUM_SYMBOLS;
        redraw();
        lastSwitch = millis();
    }

    // Refresh prices every 5 minutes
    if (millis() - lastRefresh >= REFRESH_MS) {
        refreshAll();
        redraw();
    }

    delay(50);
}
