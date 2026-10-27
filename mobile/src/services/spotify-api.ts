import * as SecureStore from 'expo-secure-store';

export type SpotifyTrack = {
  id: string;
  title: string;
  artists: string[];
  album: string;
  artworkUrl: string | null;
  spotifyUrl: string;
  durationMs: number;
  addedAt: string;
};

export type SpotifyPage = {
  tracks: SpotifyTrack[];
  total: number;
  nextOffset: number | null;
};

export type SpotifyTokenData = {
  accessToken: string;
  tokenType: 'bearer' | 'mac';
  expiresIn?: number;
  refreshToken?: string;
  scope?: string;
  issuedAt?: number;
};

const clientIdKey = 'espspotify.spotifyClientId';
const redirectUriKey = 'espspotify.spotifyRedirectUri';
const tokenKey = 'espspotify.spotifyToken';
const apiBaseUrl = 'https://api.spotify.com/v1';

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

function isSpotifyTokenData(value: unknown): value is SpotifyTokenData {
  if (!isRecord(value)) {
    return false;
  }
  return (
    typeof value.accessToken === 'string' &&
    (value.tokenType === 'bearer' || value.tokenType === 'mac') &&
    (value.expiresIn === undefined || typeof value.expiresIn === 'number') &&
    (value.refreshToken === undefined || typeof value.refreshToken === 'string') &&
    (value.scope === undefined || typeof value.scope === 'string') &&
    (value.issuedAt === undefined || typeof value.issuedAt === 'number')
  );
}

export async function loadSpotifySettings(): Promise<{ clientId: string; redirectUri: string }> {
  const [clientId, redirectUri] = await Promise.all([
    SecureStore.getItemAsync(clientIdKey),
    SecureStore.getItemAsync(redirectUriKey),
  ]);
  return { clientId: clientId ?? '', redirectUri: redirectUri ?? '' };
}

export async function saveSpotifySettings(
  clientId: string,
  redirectUri: string
): Promise<void> {
  await Promise.all([
    SecureStore.setItemAsync(clientIdKey, clientId),
    SecureStore.setItemAsync(redirectUriKey, redirectUri),
  ]);
}

export async function loadSpotifyToken(): Promise<SpotifyTokenData | null> {
  const value = await SecureStore.getItemAsync(tokenKey);
  if (!value) {
    return null;
  }
  const parsed: unknown = JSON.parse(value);
  if (!isSpotifyTokenData(parsed)) {
    throw new Error('Stored Spotify sign-in data has an invalid format. Sign in again.');
  }
  return parsed;
}

export async function saveSpotifyToken(token: SpotifyTokenData): Promise<void> {
  await SecureStore.setItemAsync(tokenKey, JSON.stringify(token));
}

export async function clearSpotifyToken(): Promise<void> {
  await SecureStore.deleteItemAsync(tokenKey);
}

function readTrack(value: unknown): SpotifyTrack | null {
  if (!isRecord(value) || !isRecord(value.track)) {
    return null;
  }
  const track = value.track;
  if (
    typeof track.id !== 'string' ||
    typeof track.name !== 'string' ||
    typeof track.duration_ms !== 'number' ||
    !isRecord(track.album) ||
    typeof track.album.name !== 'string' ||
    !Array.isArray(track.artists)
  ) {
    return null;
  }

  const artists = track.artists
    .filter((artist): artist is Record<string, unknown> => isRecord(artist))
    .map((artist) => artist.name)
    .filter((name): name is string => typeof name === 'string');
  const images = Array.isArray(track.album.images) ? track.album.images : [];
  const artwork = images.find(
    (image): image is Record<string, unknown> => isRecord(image) && typeof image.url === 'string'
  );
  const externalUrls = isRecord(track.external_urls) ? track.external_urls : null;

  return {
    id: track.id,
    title: track.name,
    artists,
    album: track.album.name,
    artworkUrl: artwork && typeof artwork.url === 'string' ? artwork.url : null,
    spotifyUrl:
      externalUrls && typeof externalUrls.spotify === 'string'
        ? externalUrls.spotify
        : `https://open.spotify.com/track/${encodeURIComponent(track.id)}`,
    durationMs: track.duration_ms,
    addedAt: typeof value.added_at === 'string' ? value.added_at : '',
  };
}

export async function fetchSpotifyLikedTracks(
  accessToken: string,
  offset: number
): Promise<SpotifyPage> {
  const query = new URLSearchParams({ limit: '50', offset: String(offset) });
  const response = await fetch(`${apiBaseUrl}/me/tracks?${query}`, {
    headers: { Authorization: `Bearer ${accessToken}` },
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(
      `Spotify Liked Songs request failed (${response.status}): ${body || response.statusText}`
    );
  }

  const result: unknown = await response.json();
  if (!isRecord(result) || !Array.isArray(result.items) || typeof result.total !== 'number') {
    throw new Error('Spotify returned an invalid Liked Songs response.');
  }
  const tracks = result.items
    .map(readTrack)
    .filter((track): track is SpotifyTrack => track !== null);
  const next = typeof result.next === 'string' ? result.next : null;
  return {
    tracks,
    total: result.total,
    nextOffset: next ? offset + result.items.length : null,
  };
}
