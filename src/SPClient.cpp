#include <mbedtls/md.h>
#include "base64.hpp"
#include <UrlEncode.h>
#include "JsonStreamScanner.h"
#include "SPClient.h"

#define authRedirectURL "http://dialplayredirect.local/authredirected"
#define authtokenURL "https://accounts.spotify.com/api/token"

// Generate random 64 characters
String randomString64()
{
    const char *possibleChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    char result[65];
    for (int i = 0; i < 65; i++)
    {
        result[i] = possibleChars[random(62)];
    }
    result[64] = 0;
    return String(result);
}

// Generate SHA256 hash and convert to Base64 for spotify API
String SHA256HashInBase64(String source)
{
    // SHA256 Hash
    const char *payload = source.c_str();

    unsigned char hashResult[33];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *)payload, strlen(payload));
    mbedtls_md_finish(&ctx, hashResult); // 32 bytes
    mbedtls_md_free(&ctx);

    // Base64
    unsigned char base64result[49];
    encode_base64(hashResult, 32, base64result);

    // Replace URL characters
    String result = String((char *)base64result);
    result.replace("=", "");
    result.replace("+", "-");
    result.replace("/", "_");

    return result;
}

SPClient::SPClient()
{
}

// Generate code verifier and return authentication URL
String SPClient::authURLString()
{
    String urlString = "https://accounts.spotify.com/authorize";
    randomSeed(millis());
    codeVerifier = randomString64();
    authState = randomString64();

    urlString += "?client_id=" + urlEncode(clientID);
    urlString += "&response_type=code";
    urlString += "&redirect_uri=" + urlEncode(authRedirectURL);
    urlString += "&state=" + urlEncode(authState);
    urlString += "&scope=user-read-playback-state%20user-modify-playback-state";
    urlString += "&code_challenge_method=S256";
    urlString += "&code_challenge=" + SHA256HashInBase64(codeVerifier);

    return urlString;
}

// Request access token with given code
int SPClient::requestAccessToken(String code)
{
    String payload = "grant_type=authorization_code";
    payload += "&code=" + urlEncode(code);
    payload += "&redirect_uri=" + urlEncode(authRedirectURL);
    payload += "&client_id=" + urlEncode(clientID);
    payload += "&code_verifier=" + urlEncode(codeVerifier);

    httpClient.begin(authtokenURL, SpotifyPEM);
    httpClient.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const char *headerKeys[] = {"Transfer-Encoding"};
    httpClient.collectHeaders(headerKeys, 1);
    int result = httpClient.POST(payload);
    if (result == HTTP_CODE_OK)
    {
        boolean chunked = (httpClient.header("Transfer-Encoding") == "chunked");
        WiFiClient *stream = httpClient.getStreamPtr();
        JsonStreamScanner scanner = JsonStreamScanner(stream, chunked);
        while (scanner.available())
        {
            String path = scanner.scanNextKey();

            if (path == "/access_token")
            {
                accessToken = scanner.scanString();
                log_e("accessToken: %s", accessToken.c_str());
            }
            else if (path == "/refresh_token")
            {
                refreshToken = scanner.scanString();
                log_e("refreshToken: %s", refreshToken.c_str());
            }
        }
        needsRefresh = false;
    }
    else
    {
        log_e("Error: %d, %s", result, httpClient.getString().c_str());
    }
    httpClient.end();
    return result;
}

// Refresh access token with refresh token
int SPClient::refreshAccessToken()
{
    if (refreshToken.isEmpty())
        return 0;
    String payload = "grant_type=refresh_token";
    payload += "&refresh_token=" + urlEncode(refreshToken);
    payload += "&client_id=" + urlEncode(clientID);

    httpClient.begin(authtokenURL, SpotifyPEM);
    httpClient.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const char *headerKeys[] = {"Transfer-Encoding"};
    httpClient.collectHeaders(headerKeys, 1);
    int result = httpClient.POST(payload);
    if (result == HTTP_CODE_OK)
    {
        boolean chunked = (httpClient.header("Transfer-Encoding") == "chunked");
        accessToken = "";
        WiFiClient *stream = httpClient.getStreamPtr();
        JsonStreamScanner scanner = JsonStreamScanner(stream, chunked);
        while (scanner.available())
        {
            String path = scanner.scanNextKey();
            if (path == "/access_token")
            {
                accessToken = scanner.scanString();
            }
            else if (path == "/refresh_token")
            {
                refreshToken = scanner.scanString();
            }
        }
        needsRefresh = false;
    }
    httpClient.end();
    return result;
}

// Get current playback state
int SPClient::getPlaybackState()
{
    deviceID = "";
    artistName = "";
    trackName = "";
    imageURL = "";
    duration_ms = 0;
    progress_ms = 0;
    volume = 0;
    supportsVolume = false;
    isPlaying = false;

    if (accessToken.length() == 0)
        return 0;
    httpClient.begin("https://api.spotify.com/v1/me/player", SpotifyPEM);
    httpClient.addHeader("Authorization", "Bearer " + accessToken);
    const char *headerKeys[] = {"Transfer-Encoding"};
    httpClient.collectHeaders(headerKeys, 1);
    int result = httpClient.GET();
    if (result == HTTP_CODE_OK)
    {
        boolean chunked = (httpClient.header("Transfer-Encoding") == "chunked");
        WiFiClient *stream = httpClient.getStreamPtr();
        JsonStreamScanner scanner = JsonStreamScanner(stream, chunked);
        while (scanner.available())
        {
            String path = scanner.scanNextKey();
            if (path == "/device/id")
            {
                deviceID = scanner.scanString();
            }
            else if (path == "/device/volume_percent")
            {
                volume = scanner.scanInt();
            }
            else if (path == "/device/supports_volume")
            {
                supportsVolume = scanner.scanBoolean();
            }
            else if (path == "/progress_ms")
            {
                progress_ms = scanner.scanInt();
            }
            else if (path == "/is_playing")
            {
                isPlaying = scanner.scanBoolean();
            }
            else if (path == "/item/artists/name")
            {
                //artistName = scanner.scanString();
                String currentArtist = scanner.scanString();
                if (artistName.length() > 0) {
                    artistName += ", " + currentArtist;
                } else {
                    artistName = currentArtist;
                }
            }
            else if (path == "/item/duration_ms")
            {
                duration_ms = scanner.scanInt();
            }
            else if (path == "/item/name")
            {
                trackName = scanner.scanString();
            }
            else if (path == "/item/album/images/url")
            {
                imageURL = scanner.scanString();
            }
        }
    }
    else
    {
        log_e("Error: %d", result);
    }
    httpClient.end();
    if (result == 401)
        needsRefresh = true;
    return result;
}

// Get device list
int SPClient::getDeviceList()
{
    deviceIDs.clear();
    deviceNames.clear();

    httpClient.begin("https://api.spotify.com/v1/me/player/devices", SpotifyPEM);
    httpClient.addHeader("Authorization", "Bearer " + accessToken);
    const char *headerKeys[] = {"Transfer-Encoding"};
    httpClient.collectHeaders(headerKeys, 1);
    int result = httpClient.GET();
    if (result == HTTP_CODE_OK)
    {
        boolean chunked = (httpClient.header("Transfer-Encoding") == "chunked");
        WiFiClient *stream = httpClient.getStreamPtr();
        JsonStreamScanner scanner = JsonStreamScanner(stream, chunked);
        while (scanner.available())
        {
            String path = scanner.scanNextKey();
            if (path == "/devices/id")
            {
                String idString = scanner.scanString();
                if (!idString.isEmpty())
                    deviceIDs.push_back(idString);
            }
            else if (path == "/devices/name")
            {
                String nameString = scanner.scanString();
                if (!nameString.isEmpty())
                    deviceNames.push_back(nameString);
            }
        }
    }
    else
    {
        log_e("Error: %d", result);
    }
    httpClient.end();
    if (result == 401)
        needsRefresh = true;
    return result;
}

// Send API command using PUT method
int SPClient::sendPutCommand(String urlString, String payload)
{
    httpClient.begin(urlString, SpotifyPEM);
    httpClient.addHeader("Authorization", "Bearer " + accessToken);
    int result = httpClient.PUT(payload);
    httpClient.end();
    if (result == 401)
        needsRefresh = true;
    return result;
}

// Send API command using POST method
int SPClient::sendPostCommand(String urlString, String payload)
{
    httpClient.begin(urlString, SpotifyPEM);
    httpClient.addHeader("Authorization", "Bearer " + accessToken);
    httpClient.addHeader("Content-Length", String(payload.length()));
    int result = httpClient.POST(payload);
    httpClient.end();
    if (result == 401)
        needsRefresh = true;
    return result;
}

// Request changing volume
int SPClient::changeVolume(int newVolume)
{
    return sendPutCommand("https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(newVolume), "{}");
}

// Request resume
int SPClient::resumePlayback()
{
    return sendPutCommand("https://api.spotify.com/v1/me/player/play", "{}");
}

// Request pause
int SPClient::pausePlayback()
{
    return sendPutCommand("https://api.spotify.com/v1/me/player/pause", "{}");
}

// Request skipping to next track
int SPClient::skipToNext()
{
    return sendPostCommand("https://api.spotify.com/v1/me/player/next", "");
}

// Request skipping to previous track
int SPClient::skipToPrev()
{
    return sendPostCommand("https://api.spotify.com/v1/me/player/previous", "");
}

// Transfer Playback to specified device
int SPClient::selectDevice(String newDeviceID)
{
    return sendPutCommand("https://api.spotify.com/v1/me/player", "{ \"device_ids\": [\"" + newDeviceID + "\"] }");
}
