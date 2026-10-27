# ESP Spotify mobile

An Expo app for iOS that browses the media service library, streams tracks, and saves played
tracks on the device for offline playback.

## Run

From `mobile/`, install dependencies and start Expo:

```bash
pnpm install
pnpm ios
```

`pnpm ios` opens the iOS Simulator on macOS with Xcode installed. On an iPhone, run `pnpm start`
and open the project with Expo Go or a development build. A custom development build is recommended
to apply the app's local-network and background-audio iOS settings.

## Connect to the media service

Open **Settings** in the app and enter the service base URL and API key. The URL must be reachable
from the iPhone and should not include `/tracks`. The API key is optional when the service has no
`SECRET` configured. It is stored using iOS SecureStore.

For a service running on your computer, use its LAN address (for example
`http://192.168.1.20:8081`) while the phone and computer are on the same Wi-Fi network. Do not use
`localhost` from a physical iPhone.

The app fetches the available tracks from `GET /tracks`, streams from
`GET /tracks/:id/stream`, and sends the configured key in the `X-API-Key` header. When playback
starts, a separate download saves the full track in the app's document storage. Tracks in **Your
library** can then be played from the local copy. Removing the app removes those local files.

## Spotify Liked Songs

The **Liked** tab uses Spotify's official Web API to read track metadata with the
`user-library-read` scope. It does not stream or download Spotify audio. Selecting a track opens
its Spotify page; long-pressing a track sends its artist and title to the configured media service
to search for and import a matching track.

1. Create a Spotify app in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
   and copy its Client ID into **Settings → Spotify Liked Songs**. A client secret is not needed:
   this app is a public client and uses Authorization Code with PKCE.
2. Register an HTTPS redirect URI in that Spotify app, then enter the exact same URI in Settings.
   Save both values, then select **Connect Spotify** on the **Liked** tab.
3. On iOS, the redirect host must be an Apple Universal Link associated with the installed app.
   Set `SPOTIFY_ASSOCIATED_DOMAIN` to the host (without `https://` or a path) when building, and
   configure that host's `apple-app-site-association` file for the app's Apple Team ID and bundle
   identifier. The redirect path must be handled by the app. Rebuild after changing the associated
   domain.

Spotify's redirect URI must be HTTPS and exactly match the URI registered in the dashboard.
The OAuth token is stored in iOS SecureStore and refreshed when necessary. Use **Disconnect** to
remove the locally stored token. Spotify sign-in requires a custom iOS build with the Universal
Link association; it cannot complete in Expo Go.
