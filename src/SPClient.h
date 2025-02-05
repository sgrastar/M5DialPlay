#ifndef SPOTIFYREQUEST_H_INCLUDE
#define SPOTIFYREQUEST_H_INCLUDE

#include <Arduino.h>
#include <HTTPClient.h>

extern const char *SpotifyPEM;
extern String clientID;
// extern String clientSecret;

class SPClient
{
public:
  SPClient();
  String codeVerifier;
  String authState;
  String accessToken;
  String refreshToken;

  boolean needsRefresh;

  std::vector <String> deviceIDs;
  std::vector <String> deviceNames;

  String deviceID;
  boolean isPlaying;
  String trackName;
  String artistName;
  String imageURL;
  boolean supportsVolume;
  int volume;
  long progress_ms;
  long duration_ms;

  String authURLString();
  int requestAccessToken(String code);
  int refreshAccessToken();

  int getPlaybackState();
  int getDeviceList();

  int sendPutCommand(String urlString, String payload);
  int sendPostCommand(String urlString, String payload);

  int changeVolume(int newVolume);
  int resumePlayback();
  int pausePlayback();

  int skipToNext();
  int skipToPrev();

  int selectDevice(String newDeviceID);

private:
  HTTPClient httpClient;
};

#endif