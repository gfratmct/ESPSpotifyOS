# ESP Spotify mobile

An Expo app for iOS with a browser version. It browses the media service library and streams
tracks; the iOS and Android apps also save played tracks on the device for offline playback. The
Player tab shows the current track and provides play, pause, previous, next, shuffle, and repeat
controls.

## Run

From `mobile/`, install dependencies and start Expo:

```bash
pnpm install
pnpm ios
```

`pnpm ios` opens the iOS Simulator on macOS with Xcode installed. On an iPhone, run `pnpm start`
and open the project with Expo Go or a development build. Use a custom development build to apply
the app's local-network and background-audio settings on iOS and Android.

The web version keeps settings and Spotify sign-in data only in the current browser tab session,
without encryption. Offline track downloads require the iOS or Android app.
On iOS and Android, the Spotify Client ID, redirect URI, OAuth token/refresh token, and pending
PKCE verifier are stored using SecureStore.

## Connect to the media service

Open **Settings** in the app and enter the service base URL and API key. The URL must be reachable
from the iPhone and should not include `/tracks`. The API key is optional when the service has no
`SECRET` configured. It is stored using SecureStore on iOS and Android.

For a service running on your computer, use its LAN address (for example
`http://192.168.1.20:8081`) while the phone and computer are on the same Wi-Fi network. Do not use
`localhost` from a physical iPhone.

The app fetches the available tracks from `GET /tracks`, streams from
`GET /tracks/:id/stream`, and sends the configured key in the `X-API-Key` header. When playback
starts, a separate download saves the full track in the app's document storage. Tracks in **Your
library** can then be played from the local copy. Removing the app removes those local files. In a
web browser, tracks stream without being saved for offline playback.

## Spotify Liked Songs

The **Library → Spotify** section uses Spotify's official Web API to read track metadata with the
`user-library-read` scope. It does not stream or download Spotify audio. Selecting a track opens
its Spotify page; long-pressing a track sends its artist and title to the configured media service
to search for and import a matching track.

1. Create a Spotify app in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
   and copy its Client ID into **Settings → Spotify Liked Songs**. A client secret is not needed:
   this app is a public client and uses Authorization Code with PKCE.
2. Register an HTTPS redirect URI in that Spotify app, then enter the exact same URI in Settings.
   Save both values, then select **Connect Spotify** in **Library → Spotify**. If the redirect does
   not return to the app automatically, paste the full callback URL or its authorization code
   into the callback field in **Library → Spotify**. Start Connect Spotify first so the app has the
   matching PKCE verifier.
3. On iOS, the redirect host must be an Apple Universal Link associated with the installed app.
   Set `SPOTIFY_ASSOCIATED_DOMAIN` to the host (without `https://` or a path) when building, and
   configure that host's `apple-app-site-association` file for the app's Apple Team ID and bundle
   identifier. The redirect path must be handled by the app. Rebuild after changing the associated
   domain.

Spotify's redirect URI must be HTTPS and exactly match the URI registered in the dashboard.
The OAuth token is stored in iOS SecureStore and refreshed when necessary. Use **Disconnect** to
remove the locally stored token. Spotify sign-in requires a custom iOS build with the Universal
Link association; it cannot complete in Expo Go.
