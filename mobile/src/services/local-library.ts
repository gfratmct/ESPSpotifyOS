import * as FileSystem from 'expo-file-system/legacy';
import { Platform } from 'react-native';

import { getStoredValue, setStoredValue } from './key-value-storage';
import type { ServerSettings, Track } from './tracks-api';

export type SavedTrack = Track & {
  localUri: string;
  sourceUrl: string;
};

const settingsUrlKey = 'espspotify.serverUrl';
const settingsApiKey = 'espspotify.apiKey';
const libraryFileName = 'espspotify-library.json';

function libraryFileUri(): string {
  if (!FileSystem.documentDirectory) {
    throw new Error('The app document directory is unavailable.');
  }
  return `${FileSystem.documentDirectory}${libraryFileName}`;
}

export async function loadServerSettings(): Promise<ServerSettings> {
  const [baseUrl, apiKey] = await Promise.all([
    getStoredValue(settingsUrlKey),
    getStoredValue(settingsApiKey),
  ]);
  return { baseUrl: baseUrl ?? '', apiKey: apiKey ?? '' };
}

export async function saveServerSettings(settings: ServerSettings): Promise<void> {
  await Promise.all([
    setStoredValue(settingsUrlKey, settings.baseUrl),
    setStoredValue(settingsApiKey, settings.apiKey),
  ]);
}

export async function loadSavedTracks(): Promise<SavedTrack[]> {
  if (Platform.OS === 'web') {
    return [];
  }

  const fileUri = libraryFileUri();
  const info = await FileSystem.getInfoAsync(fileUri);
  if (!info.exists) {
    return [];
  }

  const contents = await FileSystem.readAsStringAsync(fileUri);
  const parsed: unknown = JSON.parse(contents);
  if (!Array.isArray(parsed)) {
    throw new Error('The saved library file has an invalid format.');
  }
  const parsedTracks = parsed.filter(
    (track): track is SavedTrack =>
      typeof track === 'object' &&
      track !== null &&
      'id' in track &&
      typeof track.id === 'number' &&
      'localUri' in track &&
      typeof track.localUri === 'string' &&
      'sourceUrl' in track &&
      typeof track.sourceUrl === 'string'
  );
  const savedTracks: SavedTrack[] = [];
  for (const track of parsedTracks) {
    if ((await FileSystem.getInfoAsync(track.localUri)).exists) {
      savedTracks.push(track);
    }
  }
  if (savedTracks.length !== parsedTracks.length) {
    await saveSavedTracks(savedTracks);
  }
  return savedTracks;
}

export async function saveSavedTracks(tracks: SavedTrack[]): Promise<void> {
  if (Platform.OS === 'web') {
    throw new Error('Offline track storage is not available in a web browser.');
  }
  await FileSystem.writeAsStringAsync(libraryFileUri(), JSON.stringify(tracks));
}

function extensionForEncoding(encoding: string): string {
  const knownExtensions: Record<string, string> = {
    aac: 'aac',
    flac: 'flac',
    m4a: 'm4a',
    mp3: 'mp3',
    ogg: 'ogg',
    opus: 'opus',
    wav: 'wav',
  };
  return knownExtensions[encoding.toLowerCase()] ?? 'audio';
}

function cacheNamespace(baseUrl: string): string {
  let hash = 0x811c9dc5;
  for (const character of baseUrl.replace(/\/+$/, '')) {
    hash = Math.imul(hash ^ character.charCodeAt(0), 0x01000193);
  }
  return (hash >>> 0).toString(16);
}

export async function downloadTrack(
  track: Track,
  baseUrl: string,
  apiKey: string
): Promise<SavedTrack> {
  if (Platform.OS === 'web') {
    throw new Error('Offline track downloads are not available in a web browser.');
  }
  if (!FileSystem.documentDirectory) {
    throw new Error('The app document directory is unavailable.');
  }

  const directoryUri = `${FileSystem.documentDirectory}tracks/`;
  const directory = await FileSystem.getInfoAsync(directoryUri);
  if (!directory.exists) {
    await FileSystem.makeDirectoryAsync(directoryUri, { intermediates: true });
  }

  const normalizedBaseUrl = baseUrl.replace(/\/+$/, '');
  const localUri = `${directoryUri}${cacheNamespace(normalizedBaseUrl)}-${track.id}.${extensionForEncoding(track.encoding)}`;
  const temporaryUri = `${localUri}.download`;
  await FileSystem.deleteAsync(temporaryUri, { idempotent: true });
  try {
    const result = await FileSystem.downloadAsync(
      `${normalizedBaseUrl}/tracks/${track.id}/stream`,
      temporaryUri,
      apiKey ? { headers: { 'X-API-Key': apiKey } } : undefined
    );
    if (result.status < 200 || result.status >= 300) {
      throw new Error(`Track download failed (${result.status}).`);
    }
    await FileSystem.deleteAsync(localUri, { idempotent: true });
    await FileSystem.moveAsync({ from: result.uri, to: localUri });
    return { ...track, localUri, sourceUrl: normalizedBaseUrl };
  } catch (error) {
    try {
      await FileSystem.deleteAsync(temporaryUri, { idempotent: true });
    } catch (cleanupError) {
      const downloadMessage = error instanceof Error ? error.message : String(error);
      const cleanupMessage =
        cleanupError instanceof Error ? cleanupError.message : String(cleanupError);
      throw new Error(`${downloadMessage} Could not remove incomplete download: ${cleanupMessage}`);
    }
    throw error;
  }
}
