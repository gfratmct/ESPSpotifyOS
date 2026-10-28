import { createAudioPlayer, setAudioModeAsync } from 'expo-audio';
import type { AudioPlayer, AudioStatus } from 'expo-audio';
import * as FileSystem from 'expo-file-system/legacy';
import { Platform } from 'react-native';
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
import {
  fetchTrackPage,
  getTrackStreamUrl,
  requestHeaders,
  type ServerSettings,
  type Track,
} from '@/services/tracks-api';

export type RepeatMode = 'off' | 'all' | 'one';

type PlayerContextValue = {
  settings: ServerSettings;
  settingsReady: boolean;
  storageError: string | null;
  saveSettings: (settings: ServerSettings) => Promise<void>;
  tracks: Track[];
  tracksTotal: number;
  hasMoreTracks: boolean;
  tracksLoading: boolean;
  tracksError: string | null;
  refreshTracks: () => Promise<void>;
  loadMoreTracks: () => Promise<Track[]>;
  savedTracks: SavedTrack[];
  currentTrack: Track | SavedTrack | null;
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  playbackError: string | null;
  cacheError: string | null;
  canPlayPreviousTrack: boolean;
  canPlayNextTrack: boolean;
  shuffleEnabled: boolean;
  repeatMode: RepeatMode;
  playTrack: (track: Track | SavedTrack) => Promise<void>;
  playPreviousTrack: () => Promise<void>;
  playNextTrack: () => Promise<void>;
  toggleShuffle: () => void;
  cycleRepeatMode: () => void;
  togglePlayback: () => void;
};

const PlayerContext = createContext<PlayerContextValue | null>(null);

function messageFromError(error: unknown): string {
  return error instanceof Error ? error.message : 'An unexpected error occurred.';
}

function shuffledTrackIds(tracks: Track[], currentTrackId?: number): number[] {
  const remaining = tracks
    .map((track) => track.id)
    .filter((id) => id !== currentTrackId);
  for (let index = remaining.length - 1; index > 0; index -= 1) {
    const swapIndex = Math.floor(Math.random() * (index + 1));
    [remaining[index], remaining[swapIndex]] = [remaining[swapIndex], remaining[index]];
  }
  return currentTrackId !== undefined && tracks.some((track) => track.id === currentTrackId)
    ? [currentTrackId, ...remaining]
    : remaining;
}

export function PlayerProvider({ children }: PropsWithChildren) {
  const [player, setPlayer] = useState<AudioPlayer | null>(null);
  const [status, setStatus] = useState<AudioStatus | null>(null);
  const [settings, setSettings] = useState<ServerSettings>({ baseUrl: '', apiKey: '' });
  const [settingsReady, setSettingsReady] = useState(false);
  const [storageError, setStorageError] = useState<string | null>(null);
  const [tracks, setTracks] = useState<Track[]>([]);
  const [tracksTotal, setTracksTotal] = useState(0);
  const [hasMoreTracks, setHasMoreTracks] = useState(false);
  const [tracksLoading, setTracksLoading] = useState(false);
  const [tracksError, setTracksError] = useState<string | null>(null);
  const [savedTracks, setSavedTracks] = useState<SavedTrack[]>([]);
  const [currentTrack, setCurrentTrack] = useState<Track | SavedTrack | null>(null);
  const [playbackError, setPlaybackError] = useState<string | null>(null);
  const [cacheError, setCacheError] = useState<string | null>(null);
  const [shuffleEnabled, setShuffleEnabled] = useState(false);
  const [shuffleOrder, setShuffleOrder] = useState<number[]>([]);
  const [repeatMode, setRepeatMode] = useState<RepeatMode>('off');
  const cacheInProgress = useRef(new Set<string>());
  const webAudioObjectUrl = useRef<string | null>(null);
  const savedTracksRef = useRef<SavedTrack[]>([]);
  const libraryWriteQueue = useRef(Promise.resolve());
  const trackRequestId = useRef(0);
  const finishedStatusHandled = useRef(false);
  const tracksRef = useRef<Track[]>([]);
  const tracksOffsetRef = useRef(0);
  const hasMoreTracksRef = useRef(false);
  const loadMoreRequest = useRef<Promise<Track[]> | null>(null);

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
      audioPlayer.setActiveForLockScreen(false);
      audioPlayer.remove();
      if (webAudioObjectUrl.current) {
        URL.revokeObjectURL(webAudioObjectUrl.current);
        webAudioObjectUrl.current = null;
      }
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
    loadMoreRequest.current = null;
    tracksRef.current = [];
    tracksOffsetRef.current = 0;
    hasMoreTracksRef.current = false;
    setTracksTotal(0);
    setHasMoreTracks(false);
    if (!settings.baseUrl) {
      setTracks([]);
      tracksRef.current = [];
      setTracksError(null);
      setTracksLoading(false);
      return;
    }

    setTracks([]);
    setTracksLoading(true);
    setTracksError(null);
    try {
      const page = await fetchTrackPage(settings.baseUrl, settings.apiKey, 0);
      if (requestId === trackRequestId.current) {
        tracksRef.current = page.tracks;
        tracksOffsetRef.current = page.tracks.length;
        hasMoreTracksRef.current = page.tracks.length > 0 && page.tracks.length < page.total;
        setTracks(page.tracks);
        setTracksTotal(page.total);
        setHasMoreTracks(hasMoreTracksRef.current);
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

  const loadMoreTracks = useCallback((): Promise<Track[]> => {
    if (loadMoreRequest.current) {
      return loadMoreRequest.current;
    }
    if (!hasMoreTracksRef.current || !settings.baseUrl) {
      return Promise.resolve([]);
    }

    const requestId = trackRequestId.current;
    const offset = tracksOffsetRef.current;
    setTracksLoading(true);
    const request = fetchTrackPage(settings.baseUrl, settings.apiKey, offset)
      .then((page) => {
        if (requestId !== trackRequestId.current) {
          return [];
        }
        const existingIds = new Set(tracksRef.current.map((track) => track.id));
        const addedTracks = page.tracks.filter((track) => !existingIds.has(track.id));
        const nextTracks = [...tracksRef.current, ...addedTracks];
        tracksRef.current = nextTracks;
        tracksOffsetRef.current = offset + page.tracks.length;
        hasMoreTracksRef.current =
          page.tracks.length > 0 && tracksOffsetRef.current < page.total;
        setTracks(nextTracks);
        setTracksTotal(page.total);
        setHasMoreTracks(hasMoreTracksRef.current);
        setTracksError(null);

        if (shuffleEnabled && addedTracks.length > 0) {
          const byId = new Map(addedTracks.map((track) => [track.id, track]));
          const orderedTracks = shuffledTrackIds(addedTracks)
            .map((id) => byId.get(id))
            .filter((track): track is Track => track !== undefined);
          setShuffleOrder((current) => [...current, ...orderedTracks.map((track) => track.id)]);
          return orderedTracks;
        }
        return addedTracks;
      })
      .catch((error: unknown) => {
        if (requestId === trackRequestId.current) {
          setTracksError(messageFromError(error));
        }
        return [];
      })
      .finally(() => {
        if (requestId === trackRequestId.current) {
          setTracksLoading(false);
        }
        if (loadMoreRequest.current === request) {
          loadMoreRequest.current = null;
        }
      });
    loadMoreRequest.current = request;
    return request;
  }, [settings, shuffleEnabled]);

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

  const startTrack = useCallback(
    async (track: Track | SavedTrack) => {
      setPlaybackError(null);
      setCacheError(null);
      try {
        const isSavedTrack = 'sourceUrl' in track;
        const saved = savedTracks.find(
          (item) => item.id === track.id && item.sourceUrl === settings.baseUrl
        );
        const localUri = isSavedTrack ? track.localUri : saved?.localUri;
        const localFile =
          Platform.OS !== 'web' && localUri ? await FileSystem.getInfoAsync(localUri) : null;

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
          const streamUrl = getTrackStreamUrl(settings.baseUrl, track.id);
          if (Platform.OS === 'web') {
            const response = await fetch(streamUrl, { headers: requestHeaders(settings.apiKey) });
            if (!response.ok) {
              const body = await response.text();
              throw new Error(
                `Track stream failed (${response.status}): ${body || response.statusText}`
              );
            }
            const objectUrl = URL.createObjectURL(await response.blob());
            try {
              player.replace({ uri: objectUrl, name: track.title });
            } catch (error) {
              URL.revokeObjectURL(objectUrl);
              throw error;
            }
            if (webAudioObjectUrl.current) {
              URL.revokeObjectURL(webAudioObjectUrl.current);
            }
            webAudioObjectUrl.current = objectUrl;
          } else {
            player.replace({
              uri: streamUrl,
              headers: settings.apiKey ? { 'X-API-Key': settings.apiKey } : undefined,
              name: track.title,
            });
          }
        }
        player.setActiveForLockScreen(true, {
          title: track.title,
          artist: track.artist || 'Unknown artist',
          albumTitle: track.album || 'ESP Spotify',
        });
        player.play();
        setCurrentTrack(track);

        const cacheId = `${settings.baseUrl}:${track.id}`;
        if (
          Platform.OS !== 'web' &&
          settings.baseUrl &&
          !localFile?.exists &&
          !cacheInProgress.current.has(cacheId)
        ) {
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

  const playbackOrder = useMemo(() => {
    if (!shuffleEnabled) {
      return tracks;
    }
    const byId = new Map(tracks.map((track) => [track.id, track]));
    const shuffled = shuffleOrder
      .map((id) => byId.get(id))
      .filter((track): track is Track => track !== undefined);
    const included = new Set(shuffled.map((track) => track.id));
    return [...shuffled, ...tracks.filter((track) => !included.has(track.id))];
  }, [shuffleEnabled, shuffleOrder, tracks]);

  const currentTrackIndex = currentTrack
    ? playbackOrder.findIndex((track) => track.id === currentTrack.id)
    : -1;
  const canPlayPreviousTrack =
    currentTrackIndex > 0 ||
    (repeatMode === 'all' &&
      !hasMoreTracks &&
      currentTrackIndex === 0 &&
      playbackOrder.length > 1);
  const canPlayNextTrack =
    (currentTrackIndex >= 0 && currentTrackIndex < playbackOrder.length - 1) ||
    (hasMoreTracks && currentTrackIndex === playbackOrder.length - 1) ||
    (repeatMode === 'all' &&
      !hasMoreTracks &&
      currentTrackIndex === playbackOrder.length - 1 &&
      playbackOrder.length > 1);

  const playTrack = useCallback(async (track: Track | SavedTrack) => {
    if (shuffleEnabled) {
      setShuffleOrder(shuffledTrackIds(tracks, track.id));
    }
    await startTrack(track);
  }, [shuffleEnabled, startTrack, tracks]);

  const playPreviousTrack = useCallback(async () => {
    if (currentTrackIndex > 0) {
      await startTrack(playbackOrder[currentTrackIndex - 1]);
    } else if (
      repeatMode === 'all' &&
      !hasMoreTracks &&
      currentTrackIndex === 0 &&
      playbackOrder.length > 1
    ) {
      await startTrack(playbackOrder[playbackOrder.length - 1]);
    }
  }, [currentTrackIndex, hasMoreTracks, playbackOrder, repeatMode, startTrack]);

  const playNextTrack = useCallback(async () => {
    if (currentTrackIndex >= 0 && currentTrackIndex < playbackOrder.length - 1) {
      await startTrack(playbackOrder[currentTrackIndex + 1]);
    } else if (hasMoreTracks && currentTrackIndex === playbackOrder.length - 1) {
      const nextPage = await loadMoreTracks();
      if (nextPage.length > 0) {
        await startTrack(nextPage[0]);
      } else if (hasMoreTracks) {
        setPlaybackError('Could not load the next tracks. Try again.');
      }
    } else if (
      repeatMode === 'all' &&
      !hasMoreTracks &&
      currentTrackIndex === playbackOrder.length - 1 &&
      playbackOrder.length > 1
    ) {
      await startTrack(playbackOrder[0]);
    }
  }, [
    currentTrackIndex,
    hasMoreTracks,
    loadMoreTracks,
    playbackOrder,
    repeatMode,
    startTrack,
  ]);

  const toggleShuffle = useCallback(() => {
    if (!shuffleEnabled) {
      setShuffleOrder(shuffledTrackIds(tracks, currentTrack?.id));
    }
    setShuffleEnabled(!shuffleEnabled);
  }, [currentTrack?.id, shuffleEnabled, tracks]);

  const cycleRepeatMode = useCallback(() => {
    setRepeatMode((mode) => (mode === 'off' ? 'all' : mode === 'all' ? 'one' : 'off'));
  }, []);

  useEffect(() => {
    if (!status?.didJustFinish) {
      finishedStatusHandled.current = false;
      return;
    }
    if (finishedStatusHandled.current || !currentTrack) {
      return;
    }
    finishedStatusHandled.current = true;
    if (repeatMode === 'one' || (repeatMode === 'all' && playbackOrder.length === 1)) {
      player?.seekTo(0);
      player?.play();
    } else if (canPlayNextTrack) {
      void playNextTrack();
    }
  }, [
    canPlayNextTrack,
    currentTrack,
    playbackOrder.length,
    playNextTrack,
    player,
    repeatMode,
    status?.didJustFinish,
  ]);

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
      tracksTotal,
      hasMoreTracks,
      tracksLoading,
      tracksError,
      refreshTracks,
      loadMoreTracks,
      savedTracks,
      currentTrack,
      isPlaying: status?.playing ?? false,
      currentTime: status?.currentTime ?? 0,
      duration: status?.duration ?? 0,
      playbackError: status?.error ?? playbackError,
      cacheError,
      canPlayPreviousTrack,
      canPlayNextTrack,
      shuffleEnabled,
      repeatMode,
      playTrack,
      playPreviousTrack,
      playNextTrack,
      toggleShuffle,
      cycleRepeatMode,
      togglePlayback,
    }),
    [
      cacheError,
      canPlayNextTrack,
      canPlayPreviousTrack,
      cycleRepeatMode,
      currentTrack,
      hasMoreTracks,
      loadMoreTracks,
      playbackError,
      playTrack,
      playNextTrack,
      playPreviousTrack,
      repeatMode,
      refreshTracks,
      savedTracks,
      saveSettings,
      settings,
      settingsReady,
      shuffleEnabled,
      storageError,
      status?.currentTime,
      status?.duration,
      status?.error,
      status?.playing,
      toggleShuffle,
      togglePlayback,
      tracks,
      tracksTotal,
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
