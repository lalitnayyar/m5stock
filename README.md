# M5StickC Plus 2 Stock Ticker

A lightweight, professional stock ticker and portfolio tracker for the **M5StickC Plus 2**.

---

## Author

**Lalit Nayyar**  
Email: [lalitnayyar@gmail.com](mailto:lalitnayyar@gmail.com)  
Phone: +971 50 832 0336 (UAE) / +91 95 9535 3336 (India)

---

## Disclaimer

This project is for **personal monitoring and informational purposes only**. It is **not financial advice**. Stock prices are fetched from third-party public endpoints and may be delayed or inaccurate. Always verify prices and portfolio calculations with your official broker before making any investment decisions. The author is not responsible for any financial losses, missed trades, or decisions made based on this device.

---

## Hardware

- **Device:** M5StickC Plus 2
- **MCU:** ESP32-PICO-V3-02
- **Display:** 135 × 240 TFT, rotation 1
- **Buttons:** BtnA (front), BtnB (side), Power
- **Port:** COM3 (default upload port)

---

## Supported Stocks

AMZN, AAPL, BMY, MARA, NCNO, POET, S, SMCI, PFE

These can be changed by editing `SYMBOLS[]` in `src/main.cpp` and reflashing.

---

## Features

### Live Price Display
- Fetches current prices from Yahoo Finance every 5 minutes
- Shows price and daily % change
- Color-coded: green for up, red for down

### Three View Modes
1. **Card View** – large single-stock card with symbol, price, and % change
2. **List View** – all 9 stocks on one scrollable page with price and % change
3. **Portfolio View** – shows per-stock holding, average buy price, current price, cost, value, and P&L

### Portfolio Tracking
- Enter holdings (quantity) and average buy price per stock
- Calculates real-time P&L and P&L %
- Export / import portfolio data as JSON through the web interface

### WiFi Setup
- Default WiFi: `nayyar910` / `18067300`
- Change WiFi through the on-device config portal
- Credentials and all settings saved to ESP32 Preferences (persist across reboots)

### Settings
- Change WiFi
- View mode (Card / List / Portfolio)
- Idle timeout (Off, 1m, 2m, 5m, 10m, 30m)
- All settings can also be edited from the web config portal

### Idle Timeout
- Screen dims to black after selected inactivity time
- Wakes automatically on any button press

### NTP Time
- Syncs time over WiFi
- Shows current HH:MM in the top header bar
- Uses Eastern / NYSE time zone with DST

### Button Controls

#### Normal mode
- **BtnA press** – next stock
- **BtnB press** – previous stock
- **Hold BtnA** – open Settings menu
- **Hold BtnB** – cycle Card / List / Portfolio view

#### Settings menu
- **BtnA press** – next menu item
- **BtnB press** – select / change
- **Hold BtnA** – exit settings

---

## User Guide

### Initial Setup

1. Power on the M5StickC Plus 2.
2. It will automatically connect to the saved/default WiFi.
3. Once connected, it fetches prices and shows the first stock.

### Change WiFi

1. Hold **BtnA** to open the **Settings** menu.
2. Press **BtnA** until **Change WiFi** is highlighted.
3. Press **BtnB**.
4. The device creates an access point:
   - **SSID:** `M5-StockTicker`
   - **Password:** `18067300`
5. Connect your phone/PC to that AP.
6. Open a browser and go to `http://192.168.4.1`
7. Enter your home WiFi **SSID** and **Password**, then tap **Save & Restart**.

### Add or Update Holdings

1. Open the WiFi config portal (steps above).
2. Scroll to the **Portfolio (per stock)** table.
3. Enter the **Holding** (quantity) and **Avg Buy** price for each stock.
4. Tap **Save & Restart**.

Or, use the **Import / Export** links:

**Example JSON format:**
```json
{
  "AMZN": {"qty": 12, "buy": 205.095},
  "AAPL": {"qty": 10, "buy": 303.00},
  "BMY":  {"qty": 50, "buy": 64.30},
  "MARA": {"qty": 11, "buy": 8.05},
  "NCNO": {"qty": 150, "buy": 37.45},
  "POET": {"qty": 130, "buy": 8.23},
  "S":    {"qty": 35, "buy": 25.64},
  "SMCI": {"qty": 25, "buy": 46.58},
  "PFE":  {"qty": 273, "buy": 36.32}
}
```

### Switch Between Views

- **Hold BtnB** to cycle through Card, List, and Portfolio views.

### View Portfolio P&L

1. Switch to **Portfolio View** (Hold BtnB).
2. Press **BtnA** or **BtnB** to scroll through your holdings.
3. The screen shows:
   - Holding and average buy
   - Current price
   - Total cost and current value
   - P&L amount and %
   - Header color changes green (profit) or red (loss)

### Adjust Idle Timeout

1. Hold **BtnA** to open the **Settings** menu.
2. Select **Idle Timeout** with **BtnA**.
3. Press **BtnB** to cycle through: Off, 1m, 2m, 5m, 10m, 30m.
4. Select **Back** and press **BtnB**.

---

## Building and Flashing

Requires [PlatformIO](https://platformio.org/) with Python 3.13.

```powershell
py -3.13 -m platformio run -t upload
```

Default upload port: **COM3**

### PlatformIO Settings

- Platform: `espressif32@6.7.0`
- Board: `m5stick-c`
- Framework: Arduino
- Upload speed: `1500000`
- Monitor speed: `115200`

---

## Repository

```
C:\projects\m5stock
```

Main files:
- `src/main.cpp` – all firmware logic
- `platformio.ini` – build configuration

---

## License

Personal use only. No warranty. See **Disclaimer** above.
