#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>

String WIFI_SSID = "nayyar910";
String WIFI_PASS = "18067300";

const char* SYMBOLS[] = {
    "AMZN", "AAPL", "BMY", "MARA", "NCNO",
    "POET", "S", "SMCI", "PFE"
};
const int NUM_SYMBOLS = 9;

float prices[NUM_SYMBOLS];
float changes[NUM_SYMBOLS];
bool  valid[NUM_SYMBOLS];

int currentSymbol = 0;
int listOffset = 0;  // scroll offset for list view
int viewMode = 0;  // 0 = card, 1 = list
bool settingsMode = false;
int settingsIndex = 0;
int idleSeconds = 300;  // idle timeout (5 min default), 0 = disabled
bool screenOff = false;
unsigned long lastActivity = 0;
const char* menuLabels[] = {"Change WiFi", "View: Card/List", "Idle Timeout", "Back"};
const int MENU_ITEMS = 4;
const int IDLE_OPTIONS[] = {0, 60, 120, 300, 600, 1800};
const int NUM_IDLE_OPTIONS = 6;
unsigned long lastRefresh = 0;
unsigned long lastSwitch = 0;
const unsigned long REFRESH_MS = 5UL * 60UL * 1000UL;
const unsigned long SWITCH_MS = 6000UL;

Preferences prefs;

// ----- Time -----
String getTimeStr() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 1000)) return "--:--";
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
    return String(buf);
}

void drawHeaderTime() {
    StickCP2.Display.setTextDatum(top_center);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.drawString(getTimeStr().c_str(), StickCP2.Display.width() / 2, 2);
}

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
    drawHeaderTime();

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
    drawHeaderTime();

    // Column headers
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.setTextColor(TFT_ORANGE);
    StickCP2.Display.drawString("SYM", 4, 20);
    StickCP2.Display.drawString("PRICE", 80, 20);
    StickCP2.Display.drawString("CHG%", 160, 20);

    // Stock rows - only show what fits on screen, with scroll
    int y = 36;
    int rowH = 16;
    int availH = h - 36;
    int visibleRows = availH / rowH;

    // Adjust scroll offset to keep currentSymbol visible
    if (currentSymbol < listOffset) listOffset = currentSymbol;
    if (currentSymbol >= listOffset + visibleRows) listOffset = currentSymbol - visibleRows + 1;
    if (listOffset < 0) listOffset = 0;
    if (listOffset > NUM_SYMBOLS - visibleRows) listOffset = NUM_SYMBOLS - visibleRows;
    if (listOffset < 0) listOffset = 0;

    for (int idx = listOffset; idx < listOffset + visibleRows && idx < NUM_SYMBOLS; idx++) {
        StickCP2.Display.setTextColor(idx == currentSymbol ? TFT_CYAN : TFT_WHITE);
        StickCP2.Display.drawString(SYMBOLS[idx], 4, y);

        if (valid[idx]) {
            StickCP2.Display.setTextColor(TFT_WHITE);
            char priceBuf[16];
            snprintf(priceBuf, sizeof(priceBuf), "%.2f", prices[idx]);
            StickCP2.Display.drawString(priceBuf, 80, y);

            uint32_t c = changes[idx] >= 0 ? TFT_GREEN : TFT_RED;
            StickCP2.Display.setTextColor(c);
            char chgBuf[16];
            snprintf(chgBuf, sizeof(chgBuf), "%+.2f%%", changes[idx]);
            StickCP2.Display.drawString(chgBuf, 160, y);
        } else {
            StickCP2.Display.setTextColor(TFT_RED);
            StickCP2.Display.drawString("--", 80, y);
        }
        y += rowH;
    }

    // Scroll indicator
    if (NUM_SYMBOLS > visibleRows) {
        int barH = h - 36;
        int thumbH = barH * visibleRows / NUM_SYMBOLS;
        int thumbY = 36 + barH * listOffset / NUM_SYMBOLS;
        StickCP2.Display.fillRect(w - 3, 36, 2, barH, TFT_DARKGREY);
        StickCP2.Display.fillRect(w - 3, thumbY, 2, thumbH, TFT_CYAN);
    }
}

void drawSettings() {
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();
    StickCP2.Display.fillScreen(TFT_BLACK);
    StickCP2.Display.fillRect(0, 0, w, 16, TFT_ORANGE);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(" SETTINGS", 4, 2);

    int y = 24;
    StickCP2.Display.setTextFont(2);
    for (int i = 0; i < MENU_ITEMS; i++) {
        StickCP2.Display.setTextColor(i == settingsIndex ? TFT_BLACK : TFT_WHITE);
        if (i == settingsIndex) StickCP2.Display.fillRect(0, y, w, 20, TFT_CYAN);
        String line = menuLabels[i];
        if (i == 1) line += viewMode == 0 ? " (Card)" : " (List)";
        if (i == 2) {
            if (idleSeconds == 0) line += ": Off";
            else if (idleSeconds < 60) line += ": " + String(idleSeconds) + "s";
            else line += ": " + String(idleSeconds / 60) + "m";
        }
        StickCP2.Display.drawString(line.c_str(), 4, y);
        y += 22;
    }
    StickCP2.Display.setTextColor(TFT_LIGHTGREY);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.drawString("A=next B=select HoldA=back", 4, h - 12);
}

void redraw() {
    if (settingsMode) drawSettings();
    else if (viewMode == 0) drawStock(currentSymbol);
    else                    drawList();
}

// ----- WiFi change via simple AP + web form -----
#include <WebServer.h>
WebServer configServer(80);
bool configPortalActive = false;

void handleConfigRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>M5 Setup</title></head><body style='font-family:sans-serif;margin:20px'>";
    html += "<h2>M5 Stock Ticker - Setup</h2>";
    html += "<form action='/save' method='post'>";
    html += "<h3>WiFi</h3>";
    html += "<p>SSID: <input name='ssid' length='32' style='font-size:16px;padding:5px'></p>";
    html += "<p>Password: <input name='pass' length='64' type='password' style='font-size:16px;padding:5px'></p>";
    html += "<h3>Display</h3>";
    html += "<p>View: <select name='view' style='font-size:16px;padding:5px'>";
    html += "<option value='0'" + String(viewMode == 0 ? " selected" : "") + ">Card</option>";
    html += "<option value='1'" + String(viewMode == 1 ? " selected" : "") + ">List</option>";
    html += "</select></p>";
    html += "<p>Idle Timeout: <select name='idle' style='font-size:16px;padding:5px'>";
    int idleOpts[] = {0, 60, 120, 300, 600, 1800};
    const char* idleLabels[] = {"Off", "1m", "2m", "5m", "10m", "30m"};
    for (int j = 0; j < 6; j++) {
        html += "<option value='" + String(idleOpts[j]) + "'" +
                String(idleSeconds == idleOpts[j] ? " selected" : "") +
                ">" + idleLabels[j] + "</option>";
    }
    html += "</select></p>";
    html += "<p><button type='submit' style='font-size:16px;padding:8px 20px'>Save & Restart</button></p>";
    html += "</form></body></html>";
    configServer.send(200, "text/html", html);
}

void handleConfigSave() {
    String ssid = configServer.arg("ssid");
    String pass = configServer.arg("pass");
    String view = configServer.arg("view");
    String idle = configServer.arg("idle");

    if (ssid.length() > 0) {
        prefs.putString("wifissid", ssid);
        prefs.putString("wifipass", pass);
    }
    if (view.length() > 0) prefs.putInt("view", view.toInt());
    if (idle.length() > 0) prefs.putInt("idle", idle.toInt());

    configServer.send(200, "text/html", "<h2>Saved! Restarting...</h2>");
    delay(1500);
    ESP.restart();
}

void startConfigPortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5-StockTicker", "18067300");

    configServer.on("/", handleConfigRoot);
    configServer.on("/save", HTTP_POST, handleConfigSave);
    configServer.begin();

    StickCP2.Display.fillScreen(TFT_BLACK);
    StickCP2.Display.fillRect(0, 0, 240, 16, TFT_PURPLE);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(" WiFi Setup", 4, 2);
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString("1. Connect phone to:", 4, 24);
    StickCP2.Display.setTextColor(TFT_CYAN);
    StickCP2.Display.drawString("M5-StockTicker", 4, 44);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.drawString("Pass: 18067300", 4, 64);
    StickCP2.Display.drawString("2. Open browser:", 4, 84);
    StickCP2.Display.setTextColor(TFT_CYAN);
    StickCP2.Display.drawString("192.168.4.1", 4, 104);
    StickCP2.Display.setTextColor(TFT_LIGHTGREY);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.drawString("Hold A to cancel", 4, 130);

    configPortalActive = true;
    while (configPortalActive) {
        configServer.handleClient();
        StickCP2.update();
        if (StickCP2.BtnA.wasHold()) {
            configPortalActive = false;
            configServer.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            delay(500);
            ESP.restart();
        }
        delay(10);
    }
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

    prefs.begin("m5stock", false);
    WIFI_SSID = prefs.getString("wifissid", "nayyar910");
    WIFI_PASS = prefs.getString("wifipass", "18067300");
    idleSeconds = prefs.getInt("idle", 300);
    viewMode = prefs.getInt("view", 0);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000UL) {
        delay(500);
    }

    if (WiFi.status() != WL_CONNECTED) {
        StickCP2.Display.drawString("WiFi failed", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2);
        delay(5000);
        ESP.restart();
    }

    // Sync time (NYSE/Eastern)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();

    StickCP2.Display.drawString("Fetching prices...", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2);
    refreshAll();
    redraw();
    lastSwitch = millis();
    lastActivity = millis();
}

void loop() {
    StickCP2.update();

    if (settingsMode) {
        // Settings menu navigation
        if (StickCP2.BtnA.wasPressed()) {
            settingsIndex = (settingsIndex + 1) % MENU_ITEMS;
            drawSettings();
        }
        if (StickCP2.BtnB.wasPressed()) {
            // Action on selected item
            if (settingsIndex == 0) {
                // Change WiFi
                startConfigPortal();
            } else if (settingsIndex == 1) {
                // Toggle view
                viewMode = (viewMode + 1) % 2;
                prefs.putInt("view", viewMode);
                drawSettings();
            } else if (settingsIndex == 2) {
                // Cycle idle timeout
                int curIdx = 0;
                for (int j = 0; j < NUM_IDLE_OPTIONS; j++) {
                    if (IDLE_OPTIONS[j] == idleSeconds) { curIdx = j; break; }
                }
                idleSeconds = IDLE_OPTIONS[(curIdx + 1) % NUM_IDLE_OPTIONS];
                prefs.putInt("idle", idleSeconds);
                drawSettings();
            } else if (settingsIndex == 3) {
                // Back
                settingsMode = false;
                redraw();
            }
        }
        if (StickCP2.BtnA.wasHold()) {
            settingsMode = false;
            redraw();
            delay(300);
        }
        delay(50);
        return;
    }

    // Button A = next stock
    if (StickCP2.BtnA.wasPressed()) {
        lastActivity = millis();
        if (screenOff) {
            screenOff = false;
            StickCP2.Display.setBrightness(80);
            redraw();
        } else {
            currentSymbol = (currentSymbol + 1) % NUM_SYMBOLS;
            redraw();
            lastSwitch = millis();
        }
    }

    // Button B = previous stock
    if (StickCP2.BtnB.wasPressed()) {
        lastActivity = millis();
        if (screenOff) {
            screenOff = false;
            StickCP2.Display.setBrightness(80);
            redraw();
        } else {
            currentSymbol = (currentSymbol + NUM_SYMBOLS - 1) % NUM_SYMBOLS;
            redraw();
            lastSwitch = millis();
        }
    }

    // Hold Button A = settings menu
    if (StickCP2.BtnA.wasHold()) {
        lastActivity = millis();
        if (!screenOff) {
            settingsMode = true;
            settingsIndex = 0;
            drawSettings();
            delay(300);
        }
    }

    // Hold Button B to toggle card/list view
    if (StickCP2.BtnB.wasHold()) {
        lastActivity = millis();
        if (!screenOff) {
            viewMode = (viewMode + 1) % 2;
            prefs.putInt("view", viewMode);
            redraw();
            lastSwitch = millis();
            delay(300);
        }
    }

    // Auto-rotate every 6 seconds (card view only)
    if (!screenOff && viewMode == 0 && millis() - lastSwitch >= SWITCH_MS) {
        currentSymbol = (currentSymbol + 1) % NUM_SYMBOLS;
        redraw();
        lastSwitch = millis();
    }

    // Idle screen timeout
    if (!screenOff && idleSeconds > 0 &&
        (millis() - lastActivity >= (unsigned long)idleSeconds * 1000UL)) {
        screenOff = true;
        StickCP2.Display.setBrightness(0);
    }

    // Refresh prices every 5 minutes
    if (millis() - lastRefresh >= REFRESH_MS) {
        refreshAll();
        if (!screenOff) redraw();
    }

    delay(50);
}
