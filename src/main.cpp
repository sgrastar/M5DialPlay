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
  StateMenu = 6,          // New state for menu screen
  StateDeviceList = 7,    // Updated index
  StatePlaylistList = 8   // New state for playlist selection
} ScreenState;

// Define menu items
typedef enum
{
  MenuBack = 0,
  MenuPlaylists = 1,
  MenuDevices = 2,
  MenuItemCount
} MenuItem;

ScreenState screenState;

// Menu items
String menuItems[MenuItemCount] = {"<< Back", "Select Playlist", "Select Device"};
int selectedMenuItem = 0;
int selectedPlaylistIndex = -1;
String selectedPlaylistId = "";

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

// Screen
M5GFX Display;
#define screenWidth 240
#define screenHeight 240
#define qrcodeWidth 160
#define baseColor 0xFB40
bool needFullClear = true;

// スクロールテキスト用の変数
LGFX_Sprite albumArtSprite(&Display);  // アルバムアート用スプライト
LGFX_Sprite trackNameSprite(&Display);
LGFX_Sprite artistNameSprite(&Display);
LGFX_Sprite playlistImageSprite(&Display);  // プレイリストイメージ用スプライト
size_t trackNamePos = 0;
size_t artistNamePos = 0;
int32_t trackNameCursorX = 0;
int32_t artistNameCursorX = 0;
unsigned long lastScrollTime = 0;
const int scrollDelay = 50;  // スクロール速度（ms）
const int textPause = 1000;  // 端までスクロールした後の待機時間（ms）
bool isTrackScrolling = false;
bool isArtistScrolling = false;
unsigned long trackPauseTime = 0;
unsigned long artistPauseTime = 0;
String currentImageURL = "";  // 現在表示中の画像URL
String currentPlaylistImageURL = "";  // 現在表示中のプレイリスト画像URL
String previousTrackName = "";
String previousArtistName = "";

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
void showSpotifyAuthQRcode();

void showPlayScreen();
void redrawPlayScreen();
void showMenuScreen();
void redrawMenuScreen(int selectedLine);
void showDeviceScreen();
void redrawDeviceScreen(int selectedLine);
void showPlaylistScreen();
void redrawPlaylistScreen(int selectedLine);
void updateScrollingText();
void downloadAndDisplayAlbumArt();
void downloadAndDisplayPlaylistImage(String imageURL);

void handleRootGet(void);
void handleFormWiFi(void);
void handlePostWiFi(void);
void handleCodeReceiverOptions(void);
void handleCodeReceiver(void);
void handleNotFound(void);

void showMessage(String message);

// Function to show the menu screen
void showMenuScreen() {
  screenState = StateMenu;
  selectedMenuItem = 0;
  redrawMenuScreen(selectedMenuItem);
}

// Function to redraw the menu screen with the selected item
void redrawMenuScreen(int selectedLine) {
  Display.clear();
  
  Display.fillRect(0, screenHeight / 2 - 12, screenWidth, 24, baseColor);
  
  for (int i = 0; i < MenuItemCount; i++) {
    int y = (i - selectedLine) * 30 + screenHeight / 2;
    if (y > 0 && y < screenHeight) {
      if (i == selectedLine) {
        Display.setTextColor(BLACK);
        Display.drawString(menuItems[i], screenWidth / 2, y);
        Display.setTextColor(baseColor);
      } else {
        Display.drawString(menuItems[i], screenWidth / 2, y);
      }
    }
  }
}

// Function to show playlist selection screen
void showPlaylistScreen() {
  screenState = StatePlaylistList;
  Display.clear();
  Display.drawString("Loading playlists...", screenWidth / 2, screenHeight / 2);

  spClient.getUserPlaylists();
  
  // デフォルトで先頭の「<< Back」を選択
  tempDeviceIndex = 0;
  
  // 以前に選択したプレイリストがある場合、そのインデックスを探す (1オフセット)
  if (!selectedPlaylistId.isEmpty()) {
    for (int i = 0; i < spClient.playlistIds.size(); i++) {
      if (spClient.playlistIds[i] == selectedPlaylistId) {
        tempDeviceIndex = i + 1; // +1 for Back option
        break;
      }
    }
  }
  
  redrawPlaylistScreen(tempDeviceIndex);
}

// Enhanced redrawPlaylistScreen with images
void redrawPlaylistScreen(int selectedLine) {
  Display.clear();
  
  // プレイリスト数 + 戻るオプション
  int lineCount = 1 + spClient.playlistIds.size(); // +1 for Back option
  
  if (spClient.playlistIds.size() == 0) { // プレイリストがない場合
    Display.drawString("No playlists found", screenWidth / 2, screenHeight / 2);
    return;
  }
  
  if (selectedLine < 0) selectedLine = 0;
  if (selectedLine >= lineCount) selectedLine = lineCount - 1;
  
  // 選択エリアをハイライト
  Display.fillRect(0, screenHeight / 2 - 12, screenWidth, 24, baseColor);
  Display.fillRect(0, 0, screenWidth, 42, BLACK);
  
  // すべての行を描画
  for (int i = 0; i < lineCount; i++) {
    int y = (i - selectedLine) * 30 + screenHeight / 2;
    if (y > 0 && y < screenHeight) {
      String displayName;
      
      if (i == 0) {
        displayName = "<< Back"; // 戻るオプション
      } else {
        // i-1で実際のプレイリストインデックスを取得
        displayName = spClient.playlistNames[i-1];
        
        // 選択中のプレイリストにチェックマーク表示
        if (spClient.playlistIds[i-1] == selectedPlaylistId) {
          displayName = ">> " + displayName;
        }
      }
      
      if (i == selectedLine) {
        Display.setTextColor(BLACK);
        Display.drawString(displayName, screenWidth / 2, y);
        Display.setTextColor(baseColor);
      } else {
        Display.drawString(displayName, screenWidth / 2, y);
      }
    }
  }

  // トラック数表示 (戻るオプション以外が選択されている場合)
  Display.fillRect(0, 0, screenWidth, 42, BLACK);
  if (selectedLine > 0 && (selectedLine-1) < spClient.playlistTrackCounts.size()) {
    Display.setTextSize(1);
    Display.drawString(String(spClient.playlistTrackCounts[selectedLine-1]) + " tracks", 
                      screenWidth / 2, screenHeight / 2 - 94);
  }
  
  // ナビゲーションヘルプの表示
  //Display.drawString("Select: Press", screenWidth / 2, screenHeight - 20);
}

// Function to download and display playlist image
void downloadAndDisplayPlaylistImage(String imageURL) {
  if (currentPlaylistImageURL == imageURL) {
      return;  // 同じ画像なら再ダウンロードしない
  }
  
  if (imageURL.isEmpty()) {
      playlistImageSprite.fillScreen(BLACK);
      currentPlaylistImageURL = "";
      return;
  }
  
  currentPlaylistImageURL = imageURL;
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(imageURL);
  http.addHeader("User-Agent", "ESP32/M5Dial");
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
      WiFiClient *stream = http.getStreamPtr();
      size_t size = http.getSize();
      
      if (size > 0) {
          playlistImageSprite.fillScreen(BLACK);  // スプライトをクリア
          
          // エラーハンドリングを強化
          uint8_t *buffer = (uint8_t *)malloc(size);
          if (buffer) {
              size_t bytesRead = stream->readBytes(buffer, size);
              
              // バッファからスプライトに描画、失敗時の処理を追加
              if (!playlistImageSprite.drawJpg(buffer, bytesRead)) {
                  // 画像描画失敗時はプレースホルダーを表示
                  playlistImageSprite.fillRect(0, 0, 50, 50, baseColor);
                  playlistImageSprite.setTextColor(BLACK);
                  playlistImageSprite.drawString("", 25, 25);
              }
              free(buffer);
          } else {
              // メモリ確保失敗時の処理
              playlistImageSprite.fillRect(0, 0, 50, 50, baseColor);
              playlistImageSprite.setTextColor(BLACK);
              playlistImageSprite.drawString("!", 25, 25);
          }
      }
  } else {
      // HTTP要求失敗時の処理
      playlistImageSprite.fillRect(0, 0, 50, 50, baseColor);
      playlistImageSprite.setTextColor(BLACK);
      playlistImageSprite.drawString("X", 25, 25);
  }
  http.end();
}

// スクロールテキストの更新処理
void updateScrollingText() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < scrollDelay) {
    return;  // 更新間隔が短すぎる場合はスキップ
  }
  lastUpdate = millis();

  // 曲が変わったかチェック
  if (previousTrackName != spClient.trackName) {
    trackNameCursorX = 0;  // カーソル位置をリセット
    isTrackScrolling = false;
    previousTrackName = spClient.trackName;
  }

  // アーティストが変わったかチェック
  if (previousArtistName != spClient.artistName) {
    artistNameCursorX = 0;  // カーソル位置をリセット
    isArtistScrolling = false;
    previousArtistName = spClient.artistName;
  }

  bool needUpdate = false;  // スクロールが必要か判定

  // Track nameのスクロール処理
  int16_t trackWidth = trackNameSprite.textWidth(spClient.trackName);
  if (trackWidth > 100) {  // スプライトの幅より大きい場合のみスクロール
    needUpdate = true;

    if (!isTrackScrolling && millis() - trackPauseTime > textPause) {
      isTrackScrolling = true;
    }
    
    if (isTrackScrolling) {
      trackNameCursorX--;
      if (trackNameCursorX < -trackWidth) {
        trackNameCursorX = 100;  // スプライトの幅
        isTrackScrolling = false;
        trackPauseTime = millis();
      }
    }
  }

  // Artist nameのスクロール処理
  int16_t artistWidth = artistNameSprite.textWidth(spClient.artistName);
  if (artistWidth > 100) {  // スプライトの幅より大きい場合のみスクロール
    needUpdate = true;
    if (!isArtistScrolling && millis() - artistPauseTime > textPause) {
      isArtistScrolling = true;
    }
    
    if (isArtistScrolling) {
      artistNameCursorX--;
      if (artistNameCursorX < -artistWidth) {
        artistNameCursorX = 100;  // スプライトの幅
        isArtistScrolling = false;
        artistPauseTime = millis();
      }
    }
  }

  if (needUpdate) {
    redrawPlayScreen();
  }
}

// Setup M5Dial
void setup()
{
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\nSetup initiated.");

  Display.begin();
  M5Dial.update();

  // スプライトの初期化
  albumArtSprite.setColorDepth(16);    
  albumArtSprite.createSprite(50, 50);
  
  // プレイリスト画像用スプライトの初期化
  playlistImageSprite.setColorDepth(16);
  playlistImageSprite.createSprite(50, 50);

  trackNameSprite.setColorDepth(8);
  trackNameSprite.setFont(&fonts::lgfxJapanGothic_20);
  trackNameSprite.setTextWrap(false);
  trackNameSprite.createSprite(110, 25);  // 適切なサイズに調整

  artistNameSprite.setColorDepth(8);
  artistNameSprite.setFont(&fonts::lgfxJapanGothic_20);
  artistNameSprite.setTextWrap(false);
  artistNameSprite.createSprite(110, 25);  // 適切なサイズに調整

  oldPosition = M5Dial.Encoder.read();

  Display.setTextColor(baseColor);
  Display.setTextDatum(middle_center);
  Display.setFont(&fonts::lgfxJapanGothic_20);
  Display.setTextSize(1);

  // Reset if button is pressed when power-on
  if (M5Dial.BtnA.isPressed()) {
    Serial.println("Button pressed on boot. Resetting WiFi and Auth.");
    resetWiFiAndAuth();
    return;
  }

  // Connect if WiFi saved
  Serial.println("Attempting to connect to saved WiFi...");
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
      Serial.println("\nWiFi connection timeout. Starting AP mode.");
      scanWiFi();
      startWiFiAP();
      showAPQRcode();
      return;
    }
  }
  Serial.println("\nWiFi connected.");

  // Preferences
  preferences.begin("DialPlay");
  spClient.refreshToken = preferences.getString("refreshToken");
  selectedPlaylistId = preferences.getString("selPlaylist"); // 選択されたプレイリストの読み込み
  if (spClient.refreshToken.length())
  {
    Serial.println("Found refresh token. Attempting to refresh access token...");
    if (spClient.refreshAccessToken() == 200)
    {
      Serial.println("Access token refreshed successfully.");
      preferences.putString("refreshToken", spClient.refreshToken);
      preferences.end();
      delay(100);
      needFullClear = true;
      showPlayScreen();
      if (spClient.trackName.isEmpty())
      {
        showDeviceScreen();
      }
      return;
    }
    Serial.println("Failed to refresh access token.");
  }
  preferences.end();

  // Start web server
  Serial.println("Starting Web Server for authentication.");
  webServer.onNotFound(handleNotFound);
  webServer.on("/formwifi", HTTP_ANY, handleFormWiFi);
  webServer.on("/postwifi", HTTP_POST, handlePostWiFi);
  webServer.on("/", HTTP_GET, handleRootGet);
  webServer.on("/", HTTP_OPTIONS, handleCodeReceiverOptions);
  webServer.on("/", HTTP_POST, handleCodeReceiver);
  webServer.begin();

  myIP = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(myIP);
  
  if (MDNS.begin("dialplayredirect")) {
    Serial.println("mDNS responder started: dialplayredirect.local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  
  spotifyAuthURLString = spClient.authURLString();
  showSpotifyAuthQRcode();
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
        showSpotifyAuthQRcode();
      }
      return;
    }

    if (M5Dial.BtnA.wasReleased())
    {
      M5Dial.Speaker.tone(8000, 20);
      // 変更：showDeviceScreen()からshowMenuScreen()に
      showMenuScreen();
      return;
    }
    
    // 長押しで即座にプレイリスト再生
    if (M5Dial.BtnA.wasReleaseFor(1000) && !selectedPlaylistId.isEmpty())
    {
      M5Dial.Speaker.tone(8000, 50);
      spClient.playPlaylist(selectedPlaylistId);
      delay(100);
      needFullClear = true;
      showPlayScreen();
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
        Display.fillArc(screenWidth / 2, screenHeight / 2, screenWidth / 2, screenWidth / 2 - 8, 270, (360 + 270), BLACK);
        Display.fillArc(screenWidth / 2, screenHeight / 2, screenWidth / 2, screenWidth / 2 - 8, 270, (360 * ((float)tempVolume / 100.0f) + 270), baseColor);
      }

      // Position not changed. Wait 1 second and request volume change
      else
      {
        if (millis() - oldMillis > 1000 && tempVolume != spClient.volume)
        {
          M5Dial.Speaker.tone(8000, 20);
          spClient.changeVolume(tempVolume);
          delay(100);
          needFullClear = true;
          showPlayScreen();
          return;
        }
      }
    }

    // Auto redraw
    if (refreshMillis != 0 && refreshMillis < millis())
    {
      refreshMillis = 0;
      needFullClear = true;
      showPlayScreen();
      return;
    }

    // Touch
    auto touchDetail = M5Dial.Touch.getDetail();
    if (touchDetail.state == m5::touch_state_t::touch_end)
    {
      // Play or Pause
      //if (touchDetail.y > 75 && touchDetail.y < (75 + 60))
      if (touchDetail.y > 65 && touchDetail.y < (65 + 60))
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
          needFullClear = false;
          showPlayScreen();
        }
        else if (touchDetail.x > (75 + 60))
        {
          M5Dial.Speaker.tone(8000, 20);
          spClient.skipToNext();
          delay(100);
          needFullClear = false;
          showPlayScreen();
        }
      }
    }
    updateScrollingText();  // スクロール更新
    return;
  }
  
  case StateMenu:
  {
    // ボタン押下で選択
    if (M5Dial.BtnA.wasReleased())
    {
      M5Dial.Speaker.tone(8000, 20);
      
      switch (selectedMenuItem)
      {
        case MenuDevices:
          showDeviceScreen();
          break;
          
        case MenuPlaylists:
          showPlaylistScreen();
          break;
          
        case MenuBack:
          needFullClear = true;  // 完全リフレッシュの確保
          showPlayScreen();
          break;
      }
      return;
    }
    
    // ダイヤル回転でメニュー選択
    long newPosition = M5Dial.Encoder.read();
    if (newPosition != oldPosition && newPosition % 4 == 0)
    {
      selectedMenuItem += (newPosition - oldPosition) / 4;
      if (selectedMenuItem < 0)
        selectedMenuItem = 0;
      if (selectedMenuItem >= MenuItemCount)
        selectedMenuItem = MenuItemCount - 1;
        
      redrawMenuScreen(selectedMenuItem);
      oldPosition = newPosition;
    }
    return;
  }
  
  case StateDeviceList:
  {
    // Toggle screen
    if (M5Dial.BtnA.wasReleased())
    {
      M5Dial.Speaker.tone(8000, 20);
      
      if (tempDeviceIndex == 0) {
        // 「<< Back」が選択されている場合
        showMenuScreen();
        return;
      }
      
      // 通常のデバイス選択処理
      int actualDeviceIndex = tempDeviceIndex - 1; // Back optionの分を調整
      if (actualDeviceIndex >= 0 && actualDeviceIndex < spClient.deviceIDs.size())
      {
        String selectedDeviceID = spClient.deviceIDs[actualDeviceIndex];
        if (selectedDeviceID != spClient.deviceID)
        {
          spClient.selectDevice(selectedDeviceID);
          delay(100);
        }
      }
      needFullClear = true;
      showPlayScreen();
      return;
    }
    
    // 長押しで戻る
    // if (M5Dial.BtnA.wasReleaseFor(1000))
    // {
    //   M5Dial.Speaker.tone(8000, 50);
    //   showMenuScreen();
    //   return;
    // }
    
    long newPosition = M5Dial.Encoder.read();

    // Select device
    if (newPosition != oldPosition && newPosition % 4 == 0)
  {
    tempDeviceIndex += (newPosition - oldPosition) / 4;
    if (tempDeviceIndex < 0)
      tempDeviceIndex = 0;
    // 変更: lineCountにバックオプションを含める
    int lineCount = 1 + spClient.deviceIDs.size(); // +1 for Back option
    if (tempDeviceIndex >= lineCount)
      tempDeviceIndex = lineCount - 1;

    redrawDeviceScreen(tempDeviceIndex);
    oldPosition = newPosition;
  }
  return;
  }
  
  case StatePlaylistList:
  {
    // ボタン押下時の処理
    if (M5Dial.BtnA.wasReleased())
    {
      M5Dial.Speaker.tone(8000, 20);
      
      if (tempDeviceIndex == 0) {
        // 「<< Back」が選択されている場合
        showMenuScreen();
        return;
      }
      
      // 通常のプレイリスト選択処理
      int actualPlaylistIndex = tempDeviceIndex - 1; // Back optionの分を調整
      if (spClient.playlistIds.size() > 0 && 
          actualPlaylistIndex >= 0 && 
          actualPlaylistIndex < spClient.playlistIds.size())
      {
        // 選択したプレイリストを保存
        selectedPlaylistId = spClient.playlistIds[actualPlaylistIndex];
        
        // Preferencesに選択を保存
        preferences.begin("DialPlay");
        preferences.putString("selPlaylist", selectedPlaylistId);
        preferences.end();
        
        // 選択したプレイリストを再生
        spClient.playPlaylist(selectedPlaylistId);
        
        // 再生画面に戻る
        delay(100);
        needFullClear = true;
        showPlayScreen();
      }
      return;
    }
    
    // 長押し処理は削除（「<< Back」オプションで代替）
    
    long newPosition = M5Dial.Encoder.read();
    
    // プレイリストをスクロール
    if (newPosition != oldPosition && newPosition % 4 == 0)
    {
      tempDeviceIndex += (newPosition - oldPosition) / 4;
      if (tempDeviceIndex < 0)
        tempDeviceIndex = 0;
      // 変更: lineCountにバックオプションを含める
      int lineCount = 1 + spClient.playlistIds.size(); // +1 for Back option
      if (tempDeviceIndex >= lineCount)
        tempDeviceIndex = lineCount - 1;
        
      redrawPlaylistScreen(tempDeviceIndex);
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
  Serial.println("Resetting WiFi and Auth data.");
  WiFi.disconnect(false, true);
  preferences.begin("DialPlay");
  preferences.remove("refreshToken");
  preferences.remove("selPlaylist"); // プレイリスト選択も削除
  preferences.end();

  scanWiFi();
  startWiFiAP();
  showAPQRcode();
}

// Scan WiFi access points
void scanWiFi()
{
  showMessage("Scanning WiFi");
  Serial.println("Scanning for WiFi networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int count = WiFi.scanNetworks();
  Serial.printf("%d networks found.\n", count);
  wifiVector.clear();
  for (int i = 0; i < count; i++)
  {
    String ssid = WiFi.SSID(i);
    wifiVector.push_back(ssid);
    Serial.println(ssid);
  }
  WiFi.scanDelete();
}

// Start access point mode, web server, and DNS server
void startWiFiAP()
{
  Serial.println("Starting Access Point mode...");
  WiFi.disconnect();
  delay(100);
  WiFi.softAP(AP_ssid.c_str(), AP_pass.c_str());
  WiFi.softAPConfig(myAPIP, myAPIP, subnet);
  myIP = WiFi.softAPIP();
  Serial.printf("AP IP address: %s\n", myIP.toString().c_str());

  // Start web server
  Serial.println("Starting Web Server for WiFi setup.");
  webServer.onNotFound(handleNotFound);
  webServer.on("/formwifi", handleFormWiFi);
  webServer.on("/postwifi", HTTP_POST, handlePostWiFi);
  webServer.on("/", HTTP_GET, handleRootGet);
  webServer.on("/", HTTP_OPTIONS, handleCodeReceiverOptions);
  webServer.on("/", HTTP_POST, handleCodeReceiver);
  webServer.begin();

  // Start DNS
  Serial.println("Starting DNS Server.");
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
  String urlString = "http://" + String(myIP[0]) + "." + String(myIP[1]) + "." + String(myIP[2]) + "." + String(myIP[3]) + "/formwifi";
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
  Serial.println("Switching to Station mode...");
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
      Serial.println("\nWiFi connection timeout. Starting AP mode.");
      scanWiFi();
      startWiFiAP();
      showAPQRcode();
      return;
    }
  }

  // Connected
  showMessage("WiFi ST Connected");
  Serial.println("\nWiFi ST Connected.");

  myIP = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(myIP);

  if (MDNS.begin("dialplayredirect")) {
    Serial.println("mDNS responder started: dialplayredirect.local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  
  spotifyAuthURLString = spClient.authURLString();
  showSpotifyAuthQRcode();
}

// Show QR code to redirect smartphone browser to Spotify authorization URL
void showSpotifyAuthQRcode()
{
  Display.clear();
  Display.qrcode(spotifyAuthURLString, (screenWidth - qrcodeWidth) / 2, (screenHeight - qrcodeWidth) / 2 - 10, qrcodeWidth);
  Display.drawString("Spotify Auth", (screenWidth) / 2, (screenHeight - qrcodeWidth) / 2 + qrcodeWidth);
  Display.drawString(myIP.toString(), screenWidth / 2, (screenHeight - qrcodeWidth) / 2 + qrcodeWidth + 20);
  screenState = StateAuthQRcode;
  M5Dial.Speaker.tone(8000, 20);
  Serial.println("Displaying Spotify Auth QR Code.");
  Serial.print("Auth URL: ");
  Serial.println(spotifyAuthURLString);
}

void downloadAndDisplayAlbumArt() {
  //log_e("--- Start downloadAndDisplayAlbumArt ---");
  //log_e("Current URL: %s", currentImageURL.c_str());
  //log_e("New URL: %s", spClient.imageURL.c_str());

  if (currentImageURL == spClient.imageURL) {
    //log_e("Same image URL, skipping download");
    return;
  }

  if (spClient.imageURL.isEmpty()) {
    //log_e("Image URL is empty, clearing sprite");
    albumArtSprite.fillScreen(BLACK);
    currentImageURL = "";
    return;
  }

  currentImageURL = spClient.imageURL;
  //log_e("Starting HTTP request...");

  HTTPClient http;
  http.setTimeout(10000);  // タイムアウトを10秒に設定
  http.begin(spClient.imageURL);
  
  // User-Agentヘッダーを追加
  http.addHeader("User-Agent", "ESP32/M5Dial");
  
  int httpCode = http.GET();
  //log_e("HTTP response code: %d", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();
    size_t size = http.getSize();
    //log_e("Image size: %d bytes", size);
    
    if (size > 0) {
      //log_e("Drawing image to sprite...");
      albumArtSprite.fillScreen(BLACK);  // スプライトをクリア

      // 画像データをメモリにバッファ
      uint8_t *buffer = (uint8_t *)malloc(size);
      if (buffer) {
        size_t bytesRead = stream->readBytes(buffer, size);
        //log_e("Bytes read to buffer: %d", bytesRead);
        
        // バッファからスプライトに描画
        bool success = albumArtSprite.drawJpg(buffer, bytesRead);
        //log_e("Draw result: %s", success ? "success" : "failed");
        free(buffer);
      } else {
        //log_e("Failed to allocate buffer");
      }
      
      //log_e("Sprite properties - width: %d, height: %d, colorDepth: %d", 
      //     albumArtSprite.width(), 
      //     albumArtSprite.height(),
      //     albumArtSprite.getColorDepth());
    } else {
      //log_e("Image size is 0");
    }
  } else {
    //log_e("HTTP GET failed, error: %s", http.errorToString(httpCode).c_str());
  }
  http.end();
  //log_e("--- End downloadAndDisplayAlbumArt ---");
}

// Get status and show player screen
void showPlayScreen()
{
  if (needFullClear) {
      Display.clear();
      needFullClear = false;
  }

  int result = spClient.getPlaybackState();
  screenState = StatePlay;
  tempVolume = spClient.volume;

  // スクロール位置をリセット
  trackNameCursorX = 0;
  artistNameCursorX = 0;
  isTrackScrolling = false;
  isArtistScrolling = false;

  if (spClient.duration_ms > 0)
  {
    refreshMillis = millis() + (spClient.duration_ms - spClient.progress_ms) + 100;
  }
  else
  {
    refreshMillis = 0;
  }

  downloadAndDisplayAlbumArt();  // アルバムアートをダウンロード
  previousTrackName = spClient.trackName;
  previousArtistName = spClient.artistName;

  redrawPlayScreen();
}

// Redraw player screen components
void redrawPlayScreen()
{

  static bool lastPlayState = !spClient.isPlaying;  // 前回の再生状態
  static int lastVolume = -1;  // 前回のボリューム

  // 再生状態やボリュームが変化した場合は全画面クリア
  if (lastPlayState != spClient.isPlaying || lastVolume != spClient.volume)
  {
    Display.clear();
    lastPlayState = spClient.isPlaying;
    lastVolume = spClient.volume;
  }
  else
  {
    // スクロールテキストの領域のみクリア
    Display.fillRect(90, 150, 150, 50, BLACK);
  }
  Display.setColor(baseColor);

  // Volume
  if (spClient.supportsVolume)
    Display.fillArc(screenWidth / 2, screenHeight / 2, screenHeight / 2, screenHeight / 2 - 8, 270, (360 * ((float)spClient.volume / 100.0f) + 270));

  // Pause / Play
  if (spClient.isPlaying)
  {
    Display.fillRect(104, 65, 10, 60);
    Display.fillRect(127, 65, 10, 60);
  }
  else
  {
    Display.fillTriangle(98, 65, 158, 95, 98, 125);
  }

  // Skip
  Display.fillTriangle(166, 70, 206, 95, 166, 120);
  Display.fillRect(198, 70, 8, 50);

  Display.fillTriangle(34, 95, 74, 70, 74, 120);
  Display.fillRect(34, 70, 8, 50);

  // Album art
  albumArtSprite.pushSprite(&Display, 30, 150);  // 左端に表示

  // Track name スプライトの更新と描画
  trackNameSprite.clear();
  trackNameSprite.setCursor(trackNameCursorX, 0);
  trackNameSprite.print(spClient.trackName);
  trackNameSprite.pushSprite(&Display, 90, 150);

  // Artist name スプライトの更新と描画
  artistNameSprite.clear();
  artistNameSprite.setCursor(artistNameCursorX, 0);
  artistNameSprite.print(spClient.artistName);
  artistNameSprite.pushSprite(&Display, 90, 180);
}

// Show device list screen
void showDeviceScreen()
{
  screenState = StateDeviceList;
  spClient.getDeviceList();

  tempDeviceIndex = 0;

  for (int i = 0; i < spClient.deviceIDs.size(); i++)
  {
    if (spClient.deviceIDs[i] == spClient.deviceID)
    {
      tempDeviceIndex = i + 1; // +1 for Back option
      break;
    }
  }

  redrawDeviceScreen(tempDeviceIndex);
}

// Redraw device screen for selected line
void redrawDeviceScreen(int selectedLine)
{
  Display.clear();
  // int lineCount = spClient.deviceIDs.size();
  // if (selectedLine < 0 || selectedLine >= lineCount)
  //   return;

  // Display.fillRect(0, screenHeight / 2 - 12, screenWidth, 24, baseColor);

  int lineCount = 1 + spClient.deviceIDs.size(); // +1 for Back option
  
  if (selectedLine < 0) selectedLine = 0;
  if (selectedLine >= lineCount) selectedLine = lineCount - 1;

  Display.fillRect(0, screenHeight / 2 - 12, screenWidth, 24, baseColor);

  
  for (int i = 0; i < lineCount; i++)
  {
    int y = (i - selectedLine) * 30 + screenHeight / 2;
    if (y > 0 && y < screenHeight)
    {
      String displayText;
      
      if (i == 0) {
        displayText = "<< Back"; // 戻るオプション
      } else {
        // i-1で実際のデバイスインデックスを取得
        displayText = spClient.deviceNames[i-1];
      }
      
      if (i == selectedLine)
      {
        Display.setTextColor(BLACK);
        Display.drawString(displayText, screenWidth / 2, y);
        Display.setTextColor(baseColor);
      }
      else
      {
        Display.drawString(displayText, screenWidth / 2, y);
      }
    }
  }
  
  // 戻るボタンの表示
  //Display.drawString("Back (long press button)", screenWidth / 2, screenHeight - 20);
}

// Send WiFi setting form
void handleFormWiFi(void)
{
  Serial.println("Serving WiFi form.");
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
  Serial.printf("Received WiFi credentials for SSID: %s\n", ST_ssid.c_str());
  showMessage(ST_ssid + "|" + ST_pass);
  startWiFiST();
}

// Handler for GET requests to the root URL
void handleRootGet(void) {
  Serial.println("Received GET request for root. Sending OK.");
  webServer.send(200, "text/plain", "M5Dial server is running. Ready to receive auth code.");
}

// Handles CORS preflight requests
void handleCodeReceiverOptions(void) {
  Serial.println("Received OPTIONS request for code receiver.");
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Access-Control-Max-Age", "10000");
  webServer.sendHeader("Access-Control-Allow-Methods", "POST,OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
  webServer.send(204);
}

// Receive code from GitHub Pages
void handleCodeReceiver(void)
{
  Serial.println("Received POST request for code receiver.");
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  String code = webServer.arg("plain");

  if (code.length() > 0) {
    Serial.printf("Received code: %s\n", code.c_str());
    webServer.send(200, "text/plain", "OK");
    showMessage("Received code. Requesting token...");
    
    if (spClient.requestAccessToken(code) == 200) {
      Serial.println("Successfully obtained access token.");
      preferences.begin("DialPlay");
      preferences.putString("refreshToken", spClient.refreshToken);
      preferences.end();

      needFullClear = true;
      showPlayScreen();
      if (spClient.trackName.isEmpty())
      {
        showDeviceScreen();
      }
    } else {
      Serial.println("Error: Failed to obtain access token from Spotify.");
      showMessage("Auth Error");
      delay(3000);
      showSpotifyAuthQRcode();
    }
  } else {
    Serial.println("Error: POST request did not contain a code.");
    webServer.send(400, "text/plain", "Bad Request");
  }
}


// Send WiFi input form as captive portal
void handleNotFound(void)
{
  Serial.printf("handleNotFound: screenState=%d\n", screenState);
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
