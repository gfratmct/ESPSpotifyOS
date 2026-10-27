import { createAudioPlayer, setAudioModeAsync } from 'expo-audio';
import type { AudioPlayer, AudioStatus } from 'expo-audio';
import * as FileSystem from 'expo-file-system/legacy';
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type PropsWithChildren,
} from 'react';

import {
  downloadTrack,
  loadSavedTracks,
  loadServerSettings,
  saveSavedTracks,
  saveServerSettings,
  type SavedTrack,
} from '@/services/local-library';
import { fetchTracks, getTrackStreamUrl, type ServerSettings, type Track } from '@/services/tracks-api';

type PlayerContextValue = {
  settings: ServerSettings;
  settingsReady: boolean;
  storageError: string | null;
  saveSettings: (settings: ServerSettings) => Promise<void>;
  tracks: Track[];
  tracksLoading: boolean;
  tracksError: string | null;
  refreshTracks: () => Promise<void>;
  savedTracks: SavedTrack[];
  currentTrack: Track | SavedTrack | null;
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  playbackError: string | null;
  cacheError: string | null;
  playTrack: (track: Track | SavedTrack) => Promise<void>;
  togglePlayback: () => void;
};

const PlayerContext = createContext<PlayerContextValue | null>(null);

function messageFromError(error: unknown): string {
  return error instanceof Error ? error.message : 'An unexpected error occurred.';
}

export function PlayerProvider({ children }: PropsWithChildren) {
  const [player, setPlayer] = useState<AudioPlayer | null>(null);
  const [status, setStatus] = useState<AudioStatus | null>(null);
  const [settings, setSettings] = useState<ServerSettings>({ baseUrl: '', apiKey: '' });
  const [settingsReady, setSettingsReady] = useState(false);
  const [storageError, setStorageError] = useState<string | null>(null);
  const [tracks, setTracks] = useState<Track[]>([]);
  const [tracksLoading, setTracksLoading] = useState(false);
  const [tracksError, setTracksError] = useState<string | null>(null);
  const [savedTracks, setSavedTracks] = useState<SavedTrack[]>([]);
  const [currentTrack, setCurrentTrack] = useState<Track | SavedTrack | null>(null);
  const [playbackError, setPlaybackError] = useState<string | null>(null);
  const [cacheError, setCacheError] = useState<string | null>(null);
  const cacheInProgress = useRef(new Set<string>());
  const savedTracksRef = useRef<SavedTrack[]>([]);
  const libraryWriteQueue = useRef(Promise.resolve());
  const trackRequestId = useRef(0);

  useEffect(() => {
    let audioPlayer: AudioPlayer;
    try {
      audioPlayer = createAudioPlayer(null, { updateInterval: 500 });
    } catch (error) {
      setPlaybackError(messageFromError(error));
      return;
    }

    const subscription = audioPlayer.addListener('playbackStatusUpdate', setStatus);
    setPlayer(audioPlayer);
    setStatus(audioPlayer.currentStatus);
    void setAudioModeAsync({
      playsInSilentMode: true,
      interruptionMode: 'doNotMix',
      allowsRecording: false,
      shouldPlayInBackground: true,
    }).catch((error: unknown) => setPlaybackError(messageFromError(error)));

    return () => {
      subscription.remove();
      audioPlayer.remove();
      setPlayer(null);
      setStatus(null);
    };
  }, []);

  useEffect(() => {
    let active = true;
    void Promise.all([loadServerSettings(), loadSavedTracks()])
      .then(([loadedSettings, loadedTracks]) => {
        if (active) {
          setSettings(loadedSettings);
          savedTracksRef.current = loadedTracks;
          setSavedTracks(loadedTracks);
        }
      })
      .catch((error: unknown) => {
        if (active) {
          setStorageError(`Unable to load saved app data: ${messageFromError(error)}`);
        }
      })
      .finally(() => {
        if (active) {
          setSettingsReady(true);
        }
      });

    return () => {
      active = false;
    };
  }, []);

  const refreshTracks = useCallback(async () => {
    const requestId = ++trackRequestId.current;
    if (!settings.baseUrl) {
      setTracks([]);
      setTracksError(null);
      setTracksLoading(false);
      return;
    }

    setTracks([]);
    setTracksLoading(true);
    setTracksError(null);
    try {
      const results = await fetchTracks(settings);
      if (requestId === trackRequestId.current) {
        setTracks(results);
      }
    } catch (error) {
      if (requestId === trackRequestId.current) {
        setTracksError(messageFromError(error));
      }
    } finally {
      if (requestId === trackRequestId.current) {
        setTracksLoading(false);
      }
    }
  }, [settings]);

  useEffect(() => {
    if (settingsReady) {
      void refreshTracks();
    }
  }, [refreshTracks, settingsReady]);

  const saveSettings = useCallback(async (nextSettings: ServerSettings) => {
    const normalized = {
      baseUrl: nextSettings.baseUrl.trim().replace(/\/+$/, ''),
      apiKey: nextSettings.apiKey.trim(),
    };
    await saveServerSettings(normalized);
    setSettings(normalized);
  }, []);

  const playTrack = useCallback(
    async (track: Track | SavedTrack) => {
      setPlaybackError(null);
      setCacheError(null);
      try {
        const isSavedTrack = 'sourceUrl' in track;
        const saved = savedTracks.find(
          (item) => item.id === track.id && item.sourceUrl === settings.baseUrl
        );
        const localUri = isSavedTrack ? track.localUri : saved?.localUri;
        const localFile = localUri ? await FileSystem.getInfoAsync(localUri) : null;

        if (!player) {
          setPlaybackError('The audio player is not ready yet. Try again in a moment.');
          return;
        }

        if (localFile?.exists && localUri) {
          player.replace({ uri: localUri, name: track.title });
        } else {
          if (isSavedTrack && track.sourceUrl !== settings.baseUrl) {
            setPlaybackError('This offline file is missing. Reconnect to its original service to download it again.');
            return;
          }
          if (!settings.baseUrl) {
            setPlaybackError('Add your media service URL in Settings before streaming this track.');
            return;
          }
          player.replace({
            uri: getTrackStreamUrl(settings.baseUrl, track.id),
            headers: settings.apiKey ? { 'X-API-Key': settings.apiKey } : undefined,
            name: track.title,
          });
        }
        player.play();
        setCurrentTrack(track);

        const cacheId = `${settings.baseUrl}:${track.id}`;
        if (settings.baseUrl && !localFile?.exists && !cacheInProgress.current.has(cacheId)) {
          cacheInProgress.current.add(cacheId);
          void downloadTrack(track, settings.baseUrl, settings.apiKey)
            .then((downloaded) => {
              const saveOperation = libraryWriteQueue.current.then(async () => {
                const nextTracks = [
                  downloaded,
                  ...savedTracksRef.current.filter(
                    (item) =>
                      item.id !== downloaded.id || item.sourceUrl !== downloaded.sourceUrl
                  ),
                ];
                await saveSavedTracks(nextTracks);
                savedTracksRef.current = nextTracks;
                setSavedTracks(nextTracks);
                setCacheError(null);
              });
              libraryWriteQueue.current = saveOperation.catch(() => undefined);
              return saveOperation;
            })
            .catch((error: unknown) => {
              setCacheError(`Could not save this track for offline listening: ${messageFromError(error)}`);
            })
            .finally(() => cacheInProgress.current.delete(cacheId));
        }
      } catch (error) {
        setPlaybackError(messageFromError(error));
      }
    },
    [player, savedTracks, settings]
  );

  const togglePlayback = useCallback(() => {
    if (!currentTrack || !player) {
      return;
    }
    if (status?.playing) {
      player.pause();
    } else {
      player.play();
    }
  }, [currentTrack, player, status?.playing]);

  const value = useMemo<PlayerContextValue>(
    () => ({
      settings,
      settingsReady,
      storageError,
      saveSettings,
      tracks,
      tracksLoading,
      tracksError,
      refreshTracks,
      savedTracks,
      currentTrack,
      isPlaying: status?.playing ?? false,
      currentTime: status?.currentTime ?? 0,
      duration: status?.duration ?? 0,
      playbackError: status?.error ?? playbackError,
      cacheError,
      playTrack,
      togglePlayback,
    }),
    [
      cacheError,
      currentTrack,
      playbackError,
      playTrack,
      refreshTracks,
      savedTracks,
      saveSettings,
      settings,
      settingsReady,
      storageError,
      status?.currentTime,
      status?.duration,
      status?.error,
      status?.playing,
      togglePlayback,
      tracks,
      tracksError,
      tracksLoading,
    ]
  );

  return <PlayerContext.Provider value={value}>{children}</PlayerContext.Provider>;
}

export function usePlayer(): PlayerContextValue {
  const context = useContext(PlayerContext);
  if (!context) {
    throw new Error('usePlayer must be used within a PlayerProvider.');
  }
  return context;
}
