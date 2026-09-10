#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Preferences.h>

// ----- Defaults -----
const char* DEFAULT_STA_SSID = "nayyar910";
const char* DEFAULT_STA_PASSWORD = "18067300";
const char* AP_SSID = "M5-StockTicker";
const char* AP_PASSWORD = "18067300";
const char* YAHOO_USER_AGENT = "Mozilla/5.0 (M5StockTicker/1.0)";
const int MAX_SYMBOLS = 20;
const unsigned long REQUEST_TIMEOUT_MS = 10000UL;

// ----- State -----
struct Stock {
    String  symbol;
    float   price = 0.0f;
    float   previousClose = 0.0f;
    float   change = 0.0f;
    float   pct = 0.0f;
    bool    valid = false;
    String  error;
};

String  symbols[MAX_SYMBOLS];
Stock   stocks[MAX_SYMBOLS];
int     symbolCount = 0;

int     currentSymbol = 0;
int     listOffset = 0;

int     viewMode = 0;        // 0 card, 1 list
int     brightness = 80;
int     refreshMinutes = 5;
int     scrollSeconds = 6;
int     idleSeconds = 30;
bool    autoScroll = true;

unsigned long lastRefresh = 0;
unsigned long lastDisplaySwitch = 0;
bool          refreshActive = false;
int           refreshIndex = 0;
unsigned long refreshLast = 0;

bool    screenOff = false;
unsigned long lastActivity = 0;
bool    settingsMode = false;
int     settingsIndex = 0;

WiFiManager   wm;
WebServer     server(80);
Preferences   prefs;

// ----- Formatting -----
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

// ----- Display -----
void showMessage(const char* title, const char* line1,
                 const char* line2 = nullptr, const char* line3 = nullptr) {
    if (screenOff) return;
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();
    StickCP2.Display.fillScreen(TFT_BLACK);
    StickCP2.Display.fillRect(0, 0, w, 18, TFT_BLUE);
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(title, 4, 2);

    int y = 24;
    StickCP2.Display.setTextColor(TFT_WHITE);
    if (line1) { StickCP2.Display.drawString(line1, 6, y); y += 18; }
    if (line2) { StickCP2.Display.drawString(line2, 6, y); y += 18; }
    if (line3) { StickCP2.Display.drawString(line3, 6, y); }
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

void drawHeader(uint32_t color, const char* label) {
    int w = StickCP2.Display.width();
    StickCP2.Display.fillRect(0, 0, w, 16, color);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.drawString(label, 4, 2);

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

    float a = pct >= 0.0f ? pct : -pct;
    if (a > 5.0f) a = 5.0f;
    int fill = (int)((a / 5.0f) * w);
    if (fill < 1) fill = 1;

    if (pct >= 0.0f) StickCP2.Display.fillRect(x, y, fill, h, c);
    else             StickCP2.Display.fillRect(x + w - fill, y, fill, h, c);
}

void drawPageDots(int y, int active, uint32_t color) {
    int dotSize = 4;
    int spacing = 10;
    int startX = (StickCP2.Display.width() - (symbolCount * spacing - dotSize)) / 2;
    for (int i = 0; i < symbolCount; i++) {
        int cx = startX + i * spacing;
        if (i == active) StickCP2.Display.fillCircle(cx, y, dotSize / 2 + 1, color);
        else             StickCP2.Display.drawCircle(cx, y, dotSize / 2, TFT_DARKGREY);
    }
}

void drawCard(int index) {
    Stock& s = stocks[index];
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();

    StickCP2.Display.fillScreen(TFT_BLACK);
    uint32_t theme = s.valid ? changeColor(s.change) : TFT_ORANGE;
    drawHeader(theme, " M5 STOCKS");
    StickCP2.Display.drawRoundRect(2, 18, w - 4, h - 20, 4, theme);

    if (!s.valid) {
        StickCP2.Display.setTextFont(2);
        StickCP2.Display.setTextColor(TFT_RED);
        StickCP2.Display.setTextDatum(middle_center);
        StickCP2.Display.drawString(s.symbol.c_str(), w / 2, 40);
        StickCP2.Display.setTextColor(TFT_WHITE);
        StickCP2.Display.drawString(s.error.c_str(), w / 2, 72);
        drawPageDots(h - 6, index, TFT_WHITE);
        return;
    }

    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextFont(4);
    StickCP2.Display.setTextDatum(middle_center);
    StickCP2.Display.drawString(s.symbol.c_str(), w / 2, 34);

    String price = fmtPrice(s.price, 2);
    StickCP2.Display.drawString(price.c_str(), w / 2, 72);

    uint32_t chgColor = changeColor(s.change);
    String changeStr = fmtSigned(s.change, 2) + "  (" + fmtSigned(s.pct, 2) + "%)";
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextColor(chgColor);
    StickCP2.Display.drawString(changeStr.c_str(), w / 2, 105);

    int barW = w - 40;
    drawChangeBar(20, h - 20, barW, 6, s.pct);
    drawPageDots(h - 7, index, chgColor);
}

void drawList() {
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();
    StickCP2.Display.fillScreen(TFT_BLACK);
    drawHeader(TFT_BLUE, " M5 LIST");

    if (listOffset < 0) listOffset = 0;
    int rows = (h - 22) / 16;
    int maxOffset = symbolCount > rows ? symbolCount - rows : 0;
    if (listOffset > maxOffset) listOffset = maxOffset;

    StickCP2.Display.setTextFont(1);
    StickCP2.Display.setTextDatum(top_left);
    StickCP2.Display.setTextColor(TFT_ORANGE);
    StickCP2.Display.drawString("SYM", 4, 20);
    StickCP2.Display.drawString("PRICE", 80, 20);
    StickCP2.Display.drawString("CHG%", 160, 20);

    int y = 36;
    for (int i = listOffset; i < listOffset + rows && i < symbolCount; i++) {
        Stock& s = stocks[i];
        StickCP2.Display.setTextColor(i == currentSymbol ? TFT_CYAN : TFT_WHITE);
        StickCP2.Display.drawString(s.symbol.c_str(), 4, y);

        if (s.valid) {
            StickCP2.Display.setTextColor(TFT_WHITE);
            StickCP2.Display.drawString(fmtPrice(s.price, 2).c_str(), 80, y);
            uint32_t c = changeColor(s.pct);
            String chg = fmtSigned(s.pct, 2) + "%";
            StickCP2.Display.setTextColor(c);
            StickCP2.Display.drawString(chg.c_str(), 160, y);
        } else {
            StickCP2.Display.setTextColor(TFT_RED);
            StickCP2.Display.drawString(s.error.c_str(), 80, y);
        }
        y += 16;
    }
}

void redraw() {
    if (screenOff) return;
    if (viewMode == 0) drawCard(currentSymbol);
    else               drawList();
}

// ----- Network -----
void fetchStock(int i) {
    if (i < 0 || i >= symbolCount) return;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://query1.finance.yahoo.com/v8/finance/chart/";
    url += symbols[i];
    url += "?interval=1d&range=1d&includeAdjustedClose=true&events=history";

    http.setTimeout(REQUEST_TIMEOUT_MS);
    http.useHTTP10(true);
    http.begin(client, url);
    http.addHeader("User-Agent", YAHOO_USER_AGENT);

    int code = http.GET();
    if (code == 200) {
        StaticJsonDocument<128> filter;
        filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
        filter["chart"]["result"][0]["meta"]["previousClose"] = true;
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
            if (!stocks[i].valid) stocks[i].error = "No price";
        } else {
            stocks[i].valid = false;
            stocks[i].error = "Parse";
        }
    } else {
        stocks[i].valid = false;
        stocks[i].error = "HTTP " + String(code);
    }
    http.end();
}

void startRefresh() {
    refreshActive = true;
    refreshIndex = 0;
    refreshLast = 0;
    showMessage("Updating prices", "Please wait...");
}

void stepRefresh() {
    if (!refreshActive) return;
    if (millis() - refreshLast < 250) return;
    fetchStock(refreshIndex);
    refreshIndex++;
    refreshLast = millis();
    if (refreshIndex >= symbolCount) {
        refreshActive = false;
        lastRefresh = millis();
        redraw();
    }
}

// ----- WiFi callbacks -----
void configModeCallback(WiFiManager* myWM) {
    drawConfigScreen();
}

void saveConfigCallback() {
    showMessage("Saved", "Connecting to WiFi...");
}

// ----- Preferences -----
void saveSymbols() {
    String list;
    for (int i = 0; i < symbolCount; i++) {
        if (i > 0) list += ",";
        list += symbols[i];
    }
    prefs.putString("symbols", list);
}

void loadSymbols() {
    String list = prefs.getString("symbols", "");
    symbolCount = 0;
    if (list.length() == 0) {
        // defaults
        const char* defaults[] = {"AMZN","AAPL","BMY","MARA","NCNO","POET","S","SMCI","PFE"};
        for (int i = 0; i < 9 && i < MAX_SYMBOLS; i++) {
            symbols[i] = defaults[i];
            stocks[i].symbol = symbols[i];
            symbolCount++;
        }
        saveSymbols();
    } else {
        int start = 0;
        for (int i = 0; i < list.length() && symbolCount < MAX_SYMBOLS; i++) {
            if (list.charAt(i) == ',') {
                symbols[symbolCount] = list.substring(start, i);
                symbols[symbolCount].toUpperCase();
                stocks[symbolCount].symbol = symbols[symbolCount];
                symbolCount++;
                start = i + 1;
            }
        }
        if (start < list.length() && symbolCount < MAX_SYMBOLS) {
            symbols[symbolCount] = list.substring(start);
            symbols[symbolCount].toUpperCase();
            stocks[symbolCount].symbol = symbols[symbolCount];
            symbolCount++;
        }
    }
}

void saveSettings() {
    prefs.putInt("view", viewMode);
    prefs.putInt("bright", brightness);
    prefs.putInt("refresh", refreshMinutes);
    prefs.putInt("scroll", scrollSeconds);
    prefs.putInt("idle", idleSeconds);
    prefs.putBool("auto", autoScroll);
}

void loadSettings() {
    viewMode = prefs.getInt("view", 0);
    brightness = prefs.getInt("bright", 80);
    refreshMinutes = prefs.getInt("refresh", 5);
    scrollSeconds = prefs.getInt("scroll", 6);
    idleSeconds = prefs.getInt("idle", 30);
    autoScroll = prefs.getBool("auto", true);
    // persist defaults on first boot
    saveSettings();
    StickCP2.Display.setBrightness(brightness);
}

// ----- Web UI -----
String htmlEscape(const String& s) {
    String r;
    for (int i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '&') r += "&amp;";
        else if (c == '"') r += "&quot;";
        else r += c;
    }
    return r;
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;margin:12px;background:#111;color:#eee;}";
    html += "input,select,button{font-size:16px;margin:4px 0;}h1,h2{color:#0f0;}";
    html += "table{border-collapse:collapse;width:100%;}th,td{padding:6px;border-bottom:1px solid #555;text-align:left;}";
    html += "a{color:#0ff;}</style></head><body>";
    html += "<h1>M5 Stock Ticker</h1>";

    html += "<h2>Status</h2>";
    if (WiFi.status() == WL_CONNECTED) {
        html += "<p>WiFi: " + htmlEscape(WiFi.SSID()) + "<br>IP: " + WiFi.localIP().toString() + "</p>";
    } else {
        html += "<p>WiFi not connected</p>";
    }

    html += "<h2>Stocks</h2><table><tr><th>Symbol</th><th>Price</th><th>Change%</th><th></th></tr>";
    for (int i = 0; i < symbolCount; i++) {
        html += "<tr><td>" + symbols[i] + "</td><td>";
        if (stocks[i].valid) {
            html += fmtPrice(stocks[i].price, 2) + "</td><td>" + fmtSigned(stocks[i].pct, 2);
        } else {
            html += "--</td><td>" + stocks[i].error;
        }
        html += "%</td><td><a href='/api/remove?symbol=" + symbols[i] + "'>remove</a></td></tr>";
    }
    html += "</table>";

    html += "<h2>Add stock</h2>";
    html += "<form action='/api/add' method='GET'>";
    html += "<input type='text' name='symbol' placeholder='SYMBOL' required>";
    html += "<button type='submit'>Add</button></form>";

    html += "<h2>Wi-Fi</h2>";
    html += "<form action='/api/wifi' method='POST'>";
    html += "<input type='text' name='ssid' placeholder='SSID' required><br>";
    html += "<input type='password' name='pass' placeholder='Password' required><br>";
    html += "<button type='submit'>Save & restart</button></form>";

    html += "<h2>Device settings</h2>";
    html += "<form action='/api/settings' method='POST'>";
    html += "View: <select name='view'><option value='0'" + String(viewMode == 0 ? " selected" : "") + ">Card</option>";
    html += "<option value='1'" + String(viewMode == 1 ? " selected" : "") + ">List</option></select><br>";
    html += "Brightness: <input type='number' name='bright' min='10' max='100' value='" + String(brightness) + "'><br>";
    html += "Refresh (min): <input type='number' name='refresh' min='1' max='60' value='" + String(refreshMinutes) + "'><br>";
    html += "Scroll (sec): <input type='number' name='scroll' min='0' max='60' value='" + String(scrollSeconds) + "'><br>";
    html += "Idle (sec, 0=off): <input type='number' name='idle' min='0' max='3600' value='" + String(idleSeconds) + "'><br>";
    html += "Auto scroll: <input type='checkbox' name='auto' value='1'" + String(autoScroll ? " checked" : "") + "><br>";
    html += "<button type='submit'>Save</button></form>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleAdd() {
    if (symbolCount >= MAX_SYMBOLS) { server.send(200, "text/html", "Max stocks reached. <a href='/'>Back</a>"); return; }
    String sym = server.arg("symbol");
    sym.toUpperCase();
    sym.trim();
    if (sym.length() == 0 || sym.indexOf(',') >= 0) { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); return; }
    for (int i = 0; i < symbolCount; i++) {
        if (symbols[i] == sym) { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); return; }
    }
    symbols[symbolCount] = sym;
    stocks[symbolCount].symbol = sym;
    stocks[symbolCount].valid = false;
    symbolCount++;
    saveSymbols();
    startRefresh();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void handleRemove() {
    String sym = server.arg("symbol");
    sym.toUpperCase();
    int idx = -1;
    for (int i = 0; i < symbolCount; i++) if (symbols[i] == sym) { idx = i; break; }
    if (idx >= 0) {
        for (int i = idx; i < symbolCount - 1; i++) {
            symbols[i] = symbols[i + 1];
            stocks[i] = stocks[i + 1];
        }
        symbolCount--;
        if (currentSymbol >= symbolCount) currentSymbol = 0;
        saveSymbols();
        redraw();
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void handleWiFi() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    prefs.putString("wifissid", ssid);
    prefs.putString("wifipass", pass);
    server.send(200, "text/html", "Saved. Restarting...");
    delay(500);
    ESP.restart();
}

void handleSettings() {
    if (server.hasArg("view")) viewMode = server.arg("view").toInt();
    if (server.hasArg("bright")) brightness = constrain(server.arg("bright").toInt(), 10, 100);
    if (server.hasArg("refresh")) refreshMinutes = constrain(server.arg("refresh").toInt(), 1, 60);
    if (server.hasArg("scroll")) scrollSeconds = constrain(server.arg("scroll").toInt(), 0, 60);
    if (server.hasArg("idle")) idleSeconds = constrain(server.arg("idle").toInt(), 0, 3600);
    autoScroll = server.hasArg("auto");
    saveSettings();
    StickCP2.Display.setBrightness(brightness);
    redraw();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void setupWeb() {
    server.on("/", handleRoot);
    server.on("/api/add",   handleAdd);
    server.on("/api/remove", handleRemove);
    server.on("/api/wifi",  handleWiFi);
    server.on("/api/settings", handleSettings);
    server.begin();
}

// ----- On-device settings menu -----
const char* menuLabels[] = {"View", "Bright", "Refresh", "Scroll", "Auto", "Idle", "Power Off", "Back"};
const int MENU_ITEMS = 8;

int findIndex(int value, const int* arr, int n) {
    for (int i = 0; i < n; i++) if (arr[i] == value) return i;
    return 0;
}

void drawSettings() {
    int w = StickCP2.Display.width();
    int h = StickCP2.Display.height();
    StickCP2.Display.fillScreen(TFT_BLACK);
    drawHeader(TFT_ORANGE, " SETTINGS");

    int y = 22;
    StickCP2.Display.setTextFont(2);
    StickCP2.Display.setTextDatum(top_left);
    for (int i = 0; i < MENU_ITEMS; i++) {
        StickCP2.Display.setTextColor(i == settingsIndex ? TFT_BLACK : TFT_WHITE);
        if (i == settingsIndex) StickCP2.Display.fillRect(0, y, w, 18, TFT_CYAN);

        String val;
        switch (i) {
            case 0: val = viewMode == 0 ? "Card" : "List"; break;
            case 1: val = String(brightness); break;
            case 2: val = String(refreshMinutes) + "m"; break;
            case 3: val = String(scrollSeconds) + "s"; break;
            case 4: val = autoScroll ? "On" : "Off"; break;
            case 5: val = idleSeconds == 0 ? "Off" : (String(idleSeconds) + "s"); break;
            case 6: val = "press B"; break;
            case 7: val = ""; break;
        }
        String line = String(menuLabels[i]) + (val.length() ? (": " + val) : "");
        StickCP2.Display.drawString(line.c_str(), 4, y);
        y += 18;
    }
    StickCP2.Display.setTextColor(TFT_LIGHTGREY);
    StickCP2.Display.setTextFont(1);
    StickCP2.Display.drawString("A=next  B=change  A-hold=exit", 4, h - 12);
}

void settingsNext() {
    settingsIndex = (settingsIndex + 1) % MENU_ITEMS;
    drawSettings();
}

void settingsChange() {
    if (settingsIndex == 7) {
        settingsMode = false;
        saveSettings();
        StickCP2.Display.setBrightness(brightness);
        redraw();
        return;
    }
    if (settingsIndex == 6) {
        showMessage("Power off", "Press power to wake");
        delay(1000);
        M5.Power.powerOff();
        return;
    }
    switch (settingsIndex) {
        case 0: {
            viewMode = (viewMode + 1) % 2;
            break;
        }
        case 1: {
            static const int b[] = {20, 40, 60, 80, 100};
            brightness = b[(findIndex(brightness, b, 5) + 1) % 5];
            StickCP2.Display.setBrightness(brightness);
            break;
        }
        case 2: {
            static const int r[] = {1, 5, 10, 15, 30};
            refreshMinutes = r[(findIndex(refreshMinutes, r, 5) + 1) % 5];
            break;
        }
        case 3: {
            static const int s[] = {0, 2, 4, 6, 10};
            scrollSeconds = s[(findIndex(scrollSeconds, s, 5) + 1) % 5];
            break;
        }
        case 4: {
            autoScroll = !autoScroll;
            break;
        }
        case 5: {
            static const int idles[] = {0, 10, 30, 60, 120, 300};
            idleSeconds = idles[(findIndex(idleSeconds, idles, 6) + 1) % 6];
            lastActivity = millis();
            break;
        }
    }
    drawSettings();
}

// ----- setup / loop -----
void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    Serial.begin(115200);

    StickCP2.Display.setRotation(1);
    prefs.begin("m5stock", false);
    loadSettings();
    loadSymbols();
    StickCP2.Display.fillScreen(TFT_BLACK);

    showMessage("WiFi", "Trying nayyar910...");
    delay(500);

    String ssid = prefs.getString("wifissid", DEFAULT_STA_SSID);
    String pass = prefs.getString("wifipass", DEFAULT_STA_PASSWORD);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

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

    setupWeb();
    showMessage("Updating", "Fetching stocks...");
    startRefresh();
    lastDisplaySwitch = millis();
    lastActivity = millis();
}

void loop() {
    StickCP2.update();

    // Wake from idle on any button
    if (StickCP2.BtnA.isPressed() || StickCP2.BtnB.isPressed()) {
        lastActivity = millis();
        if (screenOff) {
            screenOff = false;
            StickCP2.Display.setBrightness(brightness);
            redraw();
        }
    }

    server.handleClient();
    stepRefresh();

    if (settingsMode) {
        if (StickCP2.BtnA.wasPressed()) settingsNext();
        if (StickCP2.BtnB.wasPressed()) settingsChange();
        if (StickCP2.BtnA.wasHold()) {
            settingsMode = false;
            saveSettings();
            StickCP2.Display.setBrightness(brightness);
            redraw();
        }
    } else {
        if (StickCP2.BtnA.wasPressed()) {
            if (viewMode == 0) {
                currentSymbol = (currentSymbol + 1) % symbolCount;
            } else {
                currentSymbol = (currentSymbol + 1) % symbolCount;
                int rows = (StickCP2.Display.height() - 22) / 16;
                if (currentSymbol >= listOffset + rows) listOffset = currentSymbol - rows + 1;
                if (currentSymbol < listOffset) listOffset = currentSymbol;
            }
            lastDisplaySwitch = millis();
            redraw();
        }

        if (StickCP2.BtnB.wasPressed()) {
            if (viewMode == 0) {
                currentSymbol = (currentSymbol + symbolCount - 1) % symbolCount;
            } else {
                currentSymbol = (currentSymbol + symbolCount - 1) % symbolCount;
                int rows = (StickCP2.Display.height() - 22) / 16;
                if (currentSymbol >= listOffset + rows) listOffset = currentSymbol - rows + 1;
                if (currentSymbol < listOffset) listOffset = currentSymbol;
            }
            lastDisplaySwitch = millis();
            redraw();
        }

        if (StickCP2.BtnA.wasHold()) {
            settingsMode = true;
            settingsIndex = 0;
            drawSettings();
        }
    }

    // Auto-rotate in card view
    if (!settingsMode && !refreshActive && viewMode == 0 && autoScroll &&
        (millis() - lastDisplaySwitch >= (unsigned long)scrollSeconds * 1000UL)) {
        currentSymbol = (currentSymbol + 1) % symbolCount;
        lastDisplaySwitch = millis();
        redraw();
    }

    // Auto-refresh
    if (!refreshActive && (millis() - lastRefresh >= (unsigned long)refreshMinutes * 60000UL)) {
        startRefresh();
    }

    // Idle screen timeout
    if (!screenOff && idleSeconds > 0 &&
        (millis() - lastActivity >= (unsigned long)idleSeconds * 1000UL)) {
        screenOff = true;
        StickCP2.Display.setBrightness(0);
    }

    delay(20);
}
