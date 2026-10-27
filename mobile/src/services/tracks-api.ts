export type Track = {
  id: number;
  yt_id: string;
  title: string;
  artist: string;
  album: string;
  duration: number;
  encoding: string;
};

type TrackPage = {
  tracks: Track[];
  total: number;
};

export type ServerSettings = {
  baseUrl: string;
  apiKey: string;
};

function requestHeaders(apiKey: string): Record<string, string> {
  return apiKey ? { 'X-API-Key': apiKey } : {};
}

function isTrack(value: unknown): value is Track {
  if (typeof value !== 'object' || value === null) {
    return false;
  }
  return (
    'id' in value &&
    typeof value.id === 'number' &&
    'title' in value &&
    typeof value.title === 'string' &&
    'artist' in value &&
    typeof value.artist === 'string' &&
    'album' in value &&
    typeof value.album === 'string' &&
    'duration' in value &&
    typeof value.duration === 'number' &&
    'encoding' in value &&
    typeof value.encoding === 'string' &&
    'yt_id' in value &&
    typeof value.yt_id === 'string'
  );
}

async function requestTrackPage(
  baseUrl: string,
  apiKey: string,
  offset: number
): Promise<TrackPage> {
  const response = await fetch(
    `${baseUrl.replace(/\/+$/, '')}/tracks?limit=100&offset=${offset}`,
    { headers: requestHeaders(apiKey) }
  );
  if (!response.ok) {
    const body = await response.text();
    throw new Error(`Track request failed (${response.status}): ${body || response.statusText}`);
  }

  const result: unknown = await response.json();
  if (
    typeof result !== 'object' ||
    result === null ||
    !('tracks' in result) ||
    !Array.isArray(result.tracks) ||
    !result.tracks.every(isTrack) ||
    !('total' in result) ||
    typeof result.total !== 'number'
  ) {
    throw new Error('The service returned an invalid track list.');
  }
  return { tracks: result.tracks, total: result.total };
}

export async function fetchTracks(settings: ServerSettings): Promise<Track[]> {
  if (!settings.baseUrl) {
    return [];
  }

  const tracks: Track[] = [];
  let total = 0;
  do {
    const page = await requestTrackPage(settings.baseUrl, settings.apiKey, tracks.length);
    tracks.push(...page.tracks);
    total = page.total;
    if (page.tracks.length === 0) {
      break;
    }
  } while (tracks.length < total);
  return tracks;
}

export async function importTrackByQuery(
  settings: ServerSettings,
  query: string
): Promise<boolean> {
  const normalizedQuery = query.trim();
  if (!settings.baseUrl.trim() || !normalizedQuery) {
    throw new Error('A media service URL and track search query are required.');
  }

  const response = await fetch(`${settings.baseUrl.replace(/\/+$/, '')}/tracks`, {
    method: 'POST',
    headers: {
      ...requestHeaders(settings.apiKey),
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ query: normalizedQuery }),
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(`Track import failed (${response.status}): ${body || response.statusText}`);
  }

  return response.status === 201;
}

export function getTrackStreamUrl(baseUrl: string, trackId: number): string {
  return `${baseUrl.replace(/\/+$/, '')}/tracks/${trackId}/stream`;
}
