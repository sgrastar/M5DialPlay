#include <Arduino.h>
#include <M5Unified.h>
#include <M5Dial.h>
#include <Preferences.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

#include "wifiform.h"
#include "SPClient.h"

typedef enum
{
  StateUndefined = 0,
  StateAPQRcode = 1,
  StateAPFormQRcode = 2,
  StateAuthQRcode = 3,
  StateWaitAuth = 4,
  StatePlay = 5,
  StateDeviceList = 6
} ScreenState;

ScreenState screenState;

// WiFi variables
IPAddress myIP;
std::vector<String> wifiVector;

// Access point mode
const IPAddress myAPIP(192, 168, 0, 1);
const IPAddress subnet(255, 255, 255, 0);
String AP_ssid = "DialPlay";
String AP_pass = "DialPlay";
DNSServer dnsServer;
WebServer webServer(80);

// Station mode
String ST_ssid;
String ST_pass;

// URL and paths
String spotifyAuthURLString;
#define formPath "/formwifi"
#define authStartPath "/authstart"
#define authRedirectPath "/authredirected"

// Screen
M5GFX Display;
#define screenWidth 240
#define screenHeight 240
#define qrcodeWidth 160
#define baseColor 0xFB40

// Spotify variables
SPClient spClient;
int tempVolume = 0;
int tempDeviceIndex = 0;
long oldPosition;
long oldMillis;
long refreshMillis = 0;

// Preferences (Save refresh token)
Preferences preferences;

// Function declarations
void resetWiFiAndAuth();
void scanWiFi();
void startWiFiAP();
void showAPQRcode();
void showAPFormQRcode();
void startWiFiST();
void showAuthQRcode();

void showPlayScreen();
void redrawPlayScreen();
void showDeviceScreen();
void redrawDeviceScreen(int selectedLine);

void handleFormWiFi(void);
void handlePostWiFi(void);
void handleAuthStart(void);
void handleAuthRedirected(void);
void handleNotFound(void);

void showMessage(String message);

// Setup M5Dial
void setup()
{
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  Display.begin();
  M5Dial.update();

  oldPosition = M5Dial.Encoder.read();

  Display.setTextColor(baseColor);
  Display.setTextDatum(middle_center);
  Display.setFont(&fonts::lgfxJapanGothic_20);
  Display.setTextSize(1);

  // Reset if button is pressed when power-on
  if (M5Dial.BtnA.isPressed()) {
    resetWiFiAndAuth();
    return;
  }

  // Connect if WiFi saved
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    timeout++;
    if (timeout > 20) // Timeout. restart from scanWiFi
    {
      scanWiFi();
      startWiFiAP();
      showAPQRcode();
      return;
    }
  }

  // Preferences
  preferences.begin("DialPlay");
  spClient.refreshToken = preferences.getString("refreshToken");
  if (spClient.refreshToken.length())
  {
    if (spClient.refreshAccessToken() == 200)
    {
      preferences.putString("refreshToken", spClient.refreshToken);
      preferences.end();
      delay(100);
      showPlayScreen();
      if (spClient.trackName.isEmpty())
      {
        showDeviceScreen();
      }
      return;
    }
  }
  preferences.end();

  // Start web server
  webServer.onNotFound(handleNotFound);
  webServer.on("/formwifi", HTTP_ANY, handleFormWiFi);
  webServer.on("/postwifi", HTTP_POST, handlePostWiFi);
  webServer.on("/authstart", handleAuthStart);
  webServer.on("/authredirected", handleAuthRedirected);
  webServer.begin();

  myIP = WiFi.localIP();
  mdns_init();
  MDNS.begin("dialplayredirect");
  spotifyAuthURLString = spClient.authURLString();
  showAuthQRcode();
}

// Main loop M5Dial
void loop()
{
  M5Dial.update();
  if (screenState <= StateAPFormQRcode)
  {
    dnsServer.processNextRequest();
  }
  if (screenState <= StateWaitAuth)
  {
    webServer.handleClient();
  }

  switch (screenState)
  {
  case StatePlay:
  {
    // Refresh token if needed
    if (spClient.needsRefresh)
    {
      if (spClient.refreshAccessToken() == 200)
      {
        preferences.begin("DialPlay");
        preferences.putString("refreshToken", spClient.refreshToken);
        preferences.end();
      }
      if (spClient.accessToken.isEmpty())
      {
        showAuthQRcode();
      }
      return;
    }

    if (M5Dial.BtnA.wasReleased())
    {
      M5Dial.Speaker.tone(8000, 20);
      showDeviceScreen();
      return;
    }

    // Dial
    if (spClient.supportsVolume)
    {
      long newPosition = M5Dial.Encoder.read();

      // Position changed
      if (newPosition != oldPosition)
      {
        tempVolume += newPosition - oldPosition;
        if (tempVolume < 0)
          tempVolume = 0;
        else if (tempVolume > 100)
          tempVolume = 100;
        oldPosition = newPosition;
        oldMillis = millis();
        Display.fillArc(screenWidth / 2, screenHeight / 2, screenHeight / 2, screenHeight / 2 - 8, 270, (360 + 270), BLACK);
        Display.fillArc(screenWidth / 2, screenHeight / 2, screenHeight / 2, screenHeight / 2 - 8, 270, (360 * ((float)tempVolume / 100.0f) + 270), baseColor);
      }

      // Position not changed. Wait 1 second and request volume change
      else
      {
        if (millis() - oldMillis > 1000 && tempVolume != spClient.volume)
        {
          M5Dial.Speaker.tone(8000, 20);
          spClient.changeVolume(tempVolume);
          delay(100);
          showPlayScreen();
          return;
        }
      }
    }

    // Auto redraw
    if (refreshMillis != 0 && refreshMillis < millis())
    {
      refreshMillis = 0;
      showPlayScreen();
      return;
    }

    // Touch
    auto touchDetail = M5Dial.Touch.getDetail();
    if (touchDetail.state == m5::touch_state_t::touch_end)
    {
      // Play or Pause
      if (touchDetail.y > 75 && touchDetail.y < (75 + 60))
      {
        if (touchDetail.x > 95 && touchDetail.x < (screenWidth - 95))
        {
          if (spClient.isPlaying)
          {
            M5Dial.Speaker.tone(8000, 20);
            spClient.pausePlayback();
          }
          else
          {
            M5Dial.Speaker.tone(8000, 20);
            spClient.resumePlayback();
          }
          spClient.getPlaybackState();
          redrawPlayScreen();
        }
        else if (touchDetail.x < 75)
        {
          M5Dial.Speaker.tone(8000, 20);
          spClient.skipToPrev();
          delay(100);
          showPlayScreen();
        }
        else if (touchDetail.x > (75 + 60))
        {
          M5Dial.Speaker.tone(8000, 20);
          spClient.skipToNext();
          delay(100);
          showPlayScreen();
        }
      }
    }
    return;
  }
  case StateDeviceList:
  {
    // Toggle screen
    if (M5Dial.BtnA.wasReleased())
    {
      M5Dial.Speaker.tone(8000, 20);
      if (tempDeviceIndex >= 0 && tempDeviceIndex < spClient.deviceIDs.size())
      {
        String selectedDeviceID = spClient.deviceIDs[tempDeviceIndex];
        if (selectedDeviceID != spClient.deviceID)
        {
          spClient.selectDevice(selectedDeviceID);
          delay(100);
        }
      }
      showPlayScreen();
      return;
    }
    long newPosition = M5Dial.Encoder.read();

    // Select device
    if (newPosition != oldPosition && newPosition % 4 == 0)
    {
      tempDeviceIndex += (newPosition - oldPosition) / 4;
      if (tempDeviceIndex < 0)
        tempDeviceIndex = 0;
      if (tempDeviceIndex >= spClient.deviceIDs.size())
        tempDeviceIndex = spClient.deviceIDs.size() - 1;

      redrawDeviceScreen(tempDeviceIndex);
      oldPosition = newPosition;
    }
    return;
  }
  }
}

// Reset WiFi and Authorization
void resetWiFiAndAuth()
{
  // Reset
  WiFi.disconnect(false, true);
  preferences.begin("DialPlay");
  preferences.remove("refreshToken");
  preferences.end();

  scanWiFi();
  startWiFiAP();
  showAPQRcode();
}

// Scan WiFi access points
void scanWiFi()
{
  showMessage("Scanning WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int count = WiFi.scanNetworks();
  wifiVector.clear();
  for (int i = 0; i < count; i++)
  {
    String ssid = WiFi.SSID(i);
    wifiVector.push_back(ssid);
  }
  WiFi.scanDelete();
}

// Start access point mode, web server, and DNS server
void startWiFiAP()
{
  WiFi.disconnect();
  delay(100);
  WiFi.softAP(AP_ssid.c_str(), AP_pass.c_str());
  WiFi.softAPConfig(myAPIP, myAPIP, subnet);
  myIP = WiFi.softAPIP();

  // Start web server
  webServer.onNotFound(handleNotFound);
  webServer.on("/formwifi", handleFormWiFi);
  webServer.on("/postwifi", HTTP_POST, handlePostWiFi);
  webServer.on("/authstart", handleAuthStart);
  webServer.on("/authredirected", handleAuthRedirected);
  webServer.begin();

  // Start DNS
  dnsServer.start(53, "*", myIP);
}

// Show QR code to transfer WiFi info
void showAPQRcode()
{
  String info = "WIFI:S:" + AP_ssid + ";T:WPA;P:" + AP_pass + ";;";
  Display.clear();
  Display.qrcode(info, (screenWidth - qrcodeWidth) / 2, (screenWidth - qrcodeWidth) / 2, qrcodeWidth);
  Display.drawString("Scan WiFi", (screenWidth) / 2, (screenWidth - qrcodeWidth) / 2 + qrcodeWidth + 10);
  screenState = StateAPQRcode;
  M5Dial.Speaker.tone(8000, 20);
}

// Show QR code for form URL (not used when captive portal detected)
void showAPFormQRcode()
{
  String urlString = "http://" + String(myIP[0]) + "." + String(myIP[1]) + "." + String(myIP[2]) + "." + String(myIP[3]) + "/authstart";
  Display.clear();
  Display.qrcode(urlString, (screenWidth - qrcodeWidth) / 2, (screenHeight - qrcodeWidth) / 2, qrcodeWidth);
  Display.drawString("Scan IP", (screenWidth) / 2, (screenHeight - qrcodeWidth) / 2 + qrcodeWidth + 10);
  screenState = StateAPFormQRcode;
  M5Dial.Speaker.tone(8000, 20);
}

// Start WiFi station mode and prepare Spotify authorization
void startWiFiST()
{
  screenState = StateAuthQRcode;
  showMessage("WiFi Switching");
  dnsServer.stop();
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ST_ssid.c_str(), ST_pass.c_str());
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    timeout++;
    if (timeout > 20) // Timeout. restart from scanWiFi
    {
      scanWiFi();
      startWiFiAP();
      showAPQRcode();
      return;
    }
  }

  // Connected
  showMessage("WiFi ST Connected");

  myIP = WiFi.localIP();
  mdns_init();
  MDNS.begin("dialplayredirect");
  spotifyAuthURLString = spClient.authURLString();
  showAuthQRcode();
}

// Show QR code to redirect smartphone browser to Spotify authorization URL
void showAuthQRcode()
{
  String urlString = "http://" + String(myIP[0]) + "." + String(myIP[1]) + "." + String(myIP[2]) + "." + String(myIP[3]) + authStartPath;
  Display.clear();
  Display.qrcode(urlString, (screenWidth - qrcodeWidth) / 2, (screenHeight - qrcodeWidth) / 2, qrcodeWidth);
  Display.drawString("Spotify Auth", (screenWidth) / 2, (screenHeight - qrcodeWidth) / 2 + qrcodeWidth + 10);
  screenState = StateAuthQRcode;
  M5Dial.Speaker.tone(8000, 20);
}

// Get status and show player screen
void showPlayScreen()
{
  int result = spClient.getPlaybackState();
  screenState = StatePlay;
  tempVolume = spClient.volume;
  if (spClient.duration_ms > 0)
  {
    refreshMillis = millis() + (spClient.duration_ms - spClient.progress_ms) + 100;
  }
  else
  {
    refreshMillis = 0;
  }
  redrawPlayScreen();
}

// Redraw player screen components
void redrawPlayScreen()
{
  Display.clear();
  Display.setColor(baseColor);

  // Volume
  if (spClient.supportsVolume)
    Display.fillArc(screenWidth / 2, screenHeight / 2, screenHeight / 2, screenHeight / 2 - 8, 270, (360 * ((float)spClient.volume / 100.0f) + 270));

  // Pause / Play
  if (spClient.isPlaying)
  {
    Display.fillRect(104, 75, 10, 60);
    Display.fillRect(127, 75, 10, 60);
  }
  else
  {
    Display.fillTriangle(98, 75, 158, 105, 98, 135);
  }

  // Skip
  Display.fillTriangle(166, 80, 206, 105, 166, 130);
  Display.fillRect(198, 80, 8, 50);

  Display.fillTriangle(34, 105, 74, 80, 74, 130);
  Display.fillRect(34, 80, 8, 50);

  // Title
  Display.drawString(spClient.trackName, screenWidth / 2, 154);
  Display.drawString(spClient.artistName, screenWidth / 2, 179);
}

// Show device list screen
void showDeviceScreen()
{
  screenState = StateDeviceList;
  spClient.getDeviceList();

  for (int i = 0; i < spClient.deviceIDs.size(); i++)
  {
    if (spClient.deviceIDs[i] == spClient.deviceID)
    {
      tempDeviceIndex = i;
    }
  }

  redrawDeviceScreen(tempDeviceIndex);
}

// Redraw device screen for selected line
void redrawDeviceScreen(int selectedLine)
{
  Display.clear();
  int lineCount = spClient.deviceIDs.size();
  if (selectedLine < 0 || selectedLine >= lineCount)
    return;

  Display.fillRect(0, screenHeight / 2 - 12, screenWidth, 24, baseColor);
  for (int i = 0; i < lineCount; i++)
  {
    int y = (i - selectedLine) * 30 + screenHeight / 2;
    if (y > 0 && y < screenHeight)
    {
      if (i == selectedLine)
      {
        Display.setTextColor(BLACK);
        Display.drawString(spClient.deviceNames[i], screenWidth / 2, y);
        Display.setTextColor(baseColor);
      }
      else
      {
        Display.drawString(spClient.deviceNames[i], screenWidth / 2, y);
      }
    }
  }
}

// Send WiFi setting form
void handleFormWiFi(void)
{
  String optionList = "";
  for (size_t i = 0; i < wifiVector.size(); i++)
  {
    String item = wifiVector[i];
    item = htmlEscapedString(item);
    optionList += ("<option>" + item + "</option>\n");
  }

  webServer.setContentLength(WiFiFormPart1.length() + optionList.length() + WiFiFormPart2.length());
  webServer.sendContent(WiFiFormPart1);
  webServer.sendContent(optionList);
  webServer.sendContent(WiFiFormPart2);
}

// Receive POST content to /formwifi
void handlePostWiFi(void)
{
  ST_ssid = webServer.arg("SSID");
  ST_pass = webServer.arg("PASS");
  showMessage(ST_ssid + "|" + ST_pass);
  startWiFiST();
}

// Redirect to Spotify authentication URL
void handleAuthStart(void)
{
  if (spotifyAuthURLString.length() > 0)
  {
    webServer.sendHeader("Location", spotifyAuthURLString);
    webServer.send(302, "text/plain", "Found.");
    return;
  }
  webServer.send(404, "text/plain", "No Auth URL");
}

// Receive redirect from Spotify authentication
void handleAuthRedirected(void)
{
  String code = webServer.arg("code");
  String state = webServer.arg("state");

  webServer.send(200, "text/html", autoCloseHtml);
  webServer.stop();
  MDNS.end();
  spClient.requestAccessToken(code);
  preferences.begin("DialPlay");
  preferences.putString("refreshToken", spClient.refreshToken);
  preferences.end();

  showPlayScreen();
  if (spClient.trackName.isEmpty())
  {
    showDeviceScreen();
  }
}

// Send WiFi input form as captive portal
void handleNotFound(void)
{
  if (screenState == StateAPQRcode)
  {
    String urlString = "http://" + String(myIP[0]) + "." + String(myIP[1]) + "." + String(myIP[2]) + "." + String(myIP[3]) + "/formwifi";
    webServer.sendHeader("Location", urlString);
    webServer.send(302, "text/plain", "Found.");
    return;
  }
  webServer.send(404, "text/plain", "Not Found.");
}

// Show text on screen
void showMessage(String message)
{
  Display.clear();
  Display.drawString(message, screenWidth / 2, screenHeight / 2);
}