# Music Streaming API Research (2024)

## Background
Due to Spotify's API policy changes effective May 15, 2025, which require organizations with 250k+ MAUs for extended quota mode, we researched alternative music streaming APIs for IoT/hardware projects like M5Dial.

## API Comparison

### 🟢 Viable for IoT Projects

#### 1. **Deezer API**
- **Pros:**
  - Comprehensive endpoints (playlists, user data, streaming)
  - Beginner-friendly (basic HTTP commands)
  - Global music catalog access
  - Available for individual developers
  - Well-documented API
- **Cons:**
  - Smaller user base than Spotify
- **Use Cases:**
  - Apps targeting international audiences
  - Platforms offering global music streaming services

#### 2. **Last.fm API**
- **Pros:**
  - Open API with minimal restrictions
  - Scrobbling functionality (listening history)
  - Integration with various music services
  - Social music data and recommendations
- **Cons:**
  - No direct playback control (metadata-focused)
- **Use Cases:**
  - Music analytics and social features
  - Listening habit tracking

#### 3. **SoundCloud API**
- **Pros:**
  - Well-documented public API
  - Upload and playlist management
  - Focus on indie/user-generated content
- **Cons:**
  - New app registration suspended (as of 2024)
  - Limited to existing registered apps
- **Use Cases:**
  - Indie music platforms
  - Creator-focused applications

### 🟡 Limited but Possible

#### 4. **Apple Music API**
- **Pros:**
  - Official API available
  - Catalog and personal content integration
  - High-quality streaming
- **Cons:**
  - Requires Apple Developer Program
  - iOS-centric ecosystem
  - More complex authentication
- **Documentation:** https://developer.apple.com/documentation/applemusicapi/

#### 5. **Tidal API**
- **Pros:**
  - High-fidelity audio streaming
  - Official API provided
  - Exclusive content access
- **Cons:**
  - Premium market focus
  - Smaller user base
  - Registration via TIDAL Developer Portal required

### 🔴 Not Suitable for IoT

#### 6. **YouTube Music**
- No dedicated API
- Limited access via YouTube Data API
- Not designed for music playback control
- Video-centric rather than audio-focused

## Technical Considerations for IoT Integration

### Authentication Methods
- **OAuth 2.0**: Standard for most APIs
- **API Keys**: Simpler but less secure
- **PKCE Flow**: Recommended for public clients (IoT devices)

### Common Features Across APIs
1. Search functionality
2. Playlist management
3. Track metadata retrieval
4. User library access
5. Playback state (varies by API)

### IoT-Specific Challenges
1. **HTTPS Requirements**: Most IoT devices only support HTTP
2. **Token Management**: Handling refresh tokens on embedded devices
3. **Rate Limits**: APIs have different quotas for requests
4. **Network Discovery**: mDNS or similar for local device discovery

## Recommendations for M5Dial Project

### Primary Recommendation: **Deezer API**
```cpp
// Example implementation structure
class DeezerClient {
    String appId;
    String secretKey;
    String accessToken;
    
    void authenticate();
    void getPlaybackState();
    void controlPlayback(String action);
    void searchTracks(String query);
};
```

### Migration Strategy from Spotify
1. **Dual Support Phase**
   - Maintain Spotify code with user-provided credentials
   - Add Deezer support as alternative option

2. **Feature Parity Checklist**
   - [ ] Authentication flow
   - [ ] Playback control (play/pause/skip)
   - [ ] Volume control
   - [ ] Playlist browsing
   - [ ] Album art display
   - [ ] Device selection

3. **User Experience Considerations**
   - Clear documentation for both APIs
   - Setup wizard for API selection
   - Graceful fallback options

## Implementation Examples

### MQTT Integration Pattern
```
IoT Device → MQTT Broker → API Gateway → Music Service API
```

### Successful IoT Music Projects (2024)
- n8n workflows for Spotify-MQTT integration
- Home Assistant music control integrations
- 2Smart platform with voice assistant support

## Future Considerations

1. **API Stability**: Monitor policy changes across platforms
2. **Multi-Service Support**: Design architecture to support multiple APIs
3. **Offline Capabilities**: Cache metadata for better UX
4. **Community Solutions**: Explore federated approaches for API access

## Resources

- [Spotify API Alternatives (Zuplo Blog)](https://zuplo.com/blog/2024/12/02/spotify-api-alternatives)
- [Nordic APIs - Music Streaming APIs](https://nordicapis.com/7-music-streaming-apis/)
- [Public APIs Directory - Music Category](https://publicapis.dev/category/music)

## Conclusion

While Spotify's new restrictions pose challenges for open-source IoT projects, alternatives like Deezer provide viable paths forward. The key is designing flexible architecture that can adapt to changing API landscapes while maintaining user experience.

---
*Research conducted: December 2024*
*Last updated: December 2024*