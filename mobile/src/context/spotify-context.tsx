import {
  exchangeCodeAsync,
  ResponseType,
  TokenResponse,
  useAuthRequest,
} from 'expo-auth-session';
import type { DiscoveryDocument } from 'expo-auth-session';
import * as WebBrowser from 'expo-web-browser';
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
import { Platform } from 'react-native';

import {
  clearSpotifyToken,
  fetchSpotifyLikedTracks,
  loadSpotifySettings,
  loadSpotifyToken,
  saveSpotifySettings as persistSpotifySettings,
  saveSpotifyToken,
  type SpotifyPage,
  type SpotifyTokenData,
  type SpotifyTrack,
} from '@/services/spotify-api';

const spotifyDiscovery: DiscoveryDocument = {
  authorizationEndpoint: 'https://accounts.spotify.com/authorize',
  tokenEndpoint: 'https://accounts.spotify.com/api/token',
};
const spotifyScopes = ['user-library-read'];
const placeholderRedirectUri = 'https://example.com/spotify-callback';

type SpotifyContextValue = {
  spotifyClientId: string;
  spotifyRedirectUri: string;
  spotifyReady: boolean;
  spotifyAuthReady: boolean;
  spotifyConnected: boolean;
  spotifyBusy: boolean;
  spotifyError: string | null;
  likedTracks: SpotifyTrack[];
  likedTotal: number;
  likedNextOffset: number | null;
  likedLoading: boolean;
  likedError: string | null;
  saveSpotifySettings: (clientId: string, redirectUri: string) => Promise<void>;
  connectSpotify: () => Promise<void>;
  disconnectSpotify: () => Promise<void>;
  refreshLikedTracks: () => Promise<void>;
  loadMoreLikedTracks: () => Promise<void>;
};

const SpotifyContext = createContext<SpotifyContextValue | null>(null);

function messageFromError(error: unknown): string {
  return error instanceof Error ? error.message : 'An unexpected error occurred.';
}

function tokenData(token: TokenResponse): SpotifyTokenData {
  return {
    accessToken: token.accessToken,
    tokenType: token.tokenType,
    expiresIn: token.expiresIn,
    refreshToken: token.refreshToken,
    scope: token.scope,
    issuedAt: token.issuedAt,
  };
}

export function SpotifyProvider({ children }: PropsWithChildren) {
  const [spotifyClientId, setSpotifyClientId] = useState('');
  const [spotifyRedirectUri, setSpotifyRedirectUri] = useState('');
  const [spotifyReady, setSpotifyReady] = useState(false);
  const [token, setToken] = useState<SpotifyTokenData | null>(null);
  const [spotifyBusy, setSpotifyBusy] = useState(false);
  const [spotifyError, setSpotifyError] = useState<string | null>(null);
  const [likedTracks, setLikedTracks] = useState<SpotifyTrack[]>([]);
  const [likedTotal, setLikedTotal] = useState(0);
  const [likedNextOffset, setLikedNextOffset] = useState<number | null>(null);
  const [likedLoading, setLikedLoading] = useState(false);
  const [likedError, setLikedError] = useState<string | null>(null);
  const likedRequestId = useRef(0);
  const requestConfig = useMemo(
    () => ({
      clientId: spotifyClientId || 'unconfigured-spotify-client-id',
      redirectUri: spotifyRedirectUri || placeholderRedirectUri,
      responseType: ResponseType.Code,
      scopes: spotifyScopes,
      usePKCE: true,
    }),
    [spotifyClientId, spotifyRedirectUri]
  );
  const [request, response, promptAsync] = useAuthRequest(requestConfig, spotifyDiscovery);

  useEffect(() => {
    if (Platform.OS === 'web') {
      WebBrowser.maybeCompleteAuthSession();
    }
  }, []);

  useEffect(() => {
    let active = true;
    void Promise.all([loadSpotifySettings(), loadSpotifyToken()])
      .then(([settings, storedToken]) => {
        if (active) {
          setSpotifyClientId(settings.clientId);
          setSpotifyRedirectUri(settings.redirectUri);
          setToken(storedToken);
        }
      })
      .catch((error: unknown) => {
        if (active) {
          setSpotifyError(`Unable to load Spotify sign-in data: ${messageFromError(error)}`);
        }
      })
      .finally(() => {
        if (active) {
          setSpotifyReady(true);
        }
      });

    return () => {
      active = false;
    };
  }, []);

  useEffect(() => {
    if (!response) {
      return;
    }
    if (response.type === 'error') {
      setSpotifyBusy(false);
      setSpotifyError(response.error?.message ?? 'Spotify sign-in was not completed.');
      return;
    }
    if (response.type !== 'success') {
      setSpotifyBusy(false);
      return;
    }
    const code = response.params.code;
    if (!code || !request?.codeVerifier || !spotifyClientId || !spotifyRedirectUri) {
      setSpotifyBusy(false);
      setSpotifyError('Spotify did not return a valid authorization code. Check your redirect URI and try again.');
      return;
    }

    let active = true;
    setSpotifyBusy(true);
    setSpotifyError(null);
    void exchangeCodeAsync(
      {
        clientId: spotifyClientId,
        code,
        redirectUri: spotifyRedirectUri,
        scopes: spotifyScopes,
        extraParams: { code_verifier: request.codeVerifier },
      },
      spotifyDiscovery
    )
      .then(async (result) => {
        const nextToken = tokenData(result);
        await saveSpotifyToken(nextToken);
        if (active) {
          setToken(nextToken);
          setLikedTracks([]);
          setLikedTotal(0);
          setLikedNextOffset(null);
        }
      })
      .catch((error: unknown) => {
        if (active) {
          setSpotifyError(`Spotify sign-in failed: ${messageFromError(error)}`);
        }
      })
      .finally(() => {
        if (active) {
          setSpotifyBusy(false);
        }
      });
    return () => {
      active = false;
    };
  }, [response, request, spotifyClientId, spotifyRedirectUri]);

  const saveSettings = useCallback(
    async (clientId: string, redirectUri: string) => {
      const nextClientId = clientId.trim();
      const nextRedirectUri = redirectUri.trim();
      await persistSpotifySettings(nextClientId, nextRedirectUri);
      if (nextClientId !== spotifyClientId || nextRedirectUri !== spotifyRedirectUri) {
        await clearSpotifyToken();
        setToken(null);
        setLikedTracks([]);
        setLikedTotal(0);
        setLikedNextOffset(null);
      }
      setSpotifyClientId(nextClientId);
      setSpotifyRedirectUri(nextRedirectUri);
      setSpotifyError(null);
    },
    [spotifyClientId, spotifyRedirectUri]
  );

  const connectSpotify = useCallback(async () => {
    if (!spotifyClientId || !spotifyRedirectUri) {
      setSpotifyError('Add your Spotify Client ID and HTTPS redirect URI in Settings first.');
      return;
    }
    if (!request) {
      setSpotifyError('Spotify sign-in is still preparing. Try again in a moment.');
      return;
    }
    setSpotifyBusy(true);
    setSpotifyError(null);
    try {
      const result = await promptAsync();
      if (result.type !== 'success') {
        setSpotifyBusy(false);
      }
    } catch (error) {
      setSpotifyBusy(false);
      setSpotifyError(messageFromError(error));
    }
  }, [promptAsync, request, spotifyClientId, spotifyRedirectUri]);

  const disconnectSpotify = useCallback(async () => {
    await clearSpotifyToken();
    setToken(null);
    setLikedTracks([]);
    setLikedTotal(0);
    setLikedNextOffset(null);
    setLikedError(null);
    setSpotifyError(null);
  }, []);

  const validAccessToken = useCallback(async (): Promise<string> => {
    if (!token) {
      throw new Error('Connect your Spotify account to view Liked Songs.');
    }
    const tokenResponse = new TokenResponse(token);
    if (TokenResponse.isTokenFresh(tokenResponse, -60)) {
      return token.accessToken;
    }
    if (!token.refreshToken) {
      await clearSpotifyToken();
      setToken(null);
      throw new Error('Your Spotify session expired. Connect your account again.');
    }

    const refreshed = await tokenResponse.refreshAsync(
      { clientId: spotifyClientId, scopes: spotifyScopes },
      spotifyDiscovery
    );
    const refreshedToken = tokenData(refreshed);
    await saveSpotifyToken(refreshedToken);
    setToken(refreshedToken);
    return refreshedToken.accessToken;
  }, [spotifyClientId, token]);

  const fetchLikedPage = useCallback(
    async (offset: number, append: boolean) => {
      const requestId = ++likedRequestId.current;
      setLikedLoading(true);
      setLikedError(null);
      try {
        const accessToken = await validAccessToken();
        const page: SpotifyPage = await fetchSpotifyLikedTracks(accessToken, offset);
        if (requestId === likedRequestId.current) {
          setLikedTracks((existing) => (append ? [...existing, ...page.tracks] : page.tracks));
          setLikedTotal(page.total);
          setLikedNextOffset(page.nextOffset);
        }
      } catch (error) {
        if (requestId === likedRequestId.current) {
          setLikedError(messageFromError(error));
        }
      } finally {
        if (requestId === likedRequestId.current) {
          setLikedLoading(false);
        }
      }
    },
    [validAccessToken]
  );

  const refreshLikedTracks = useCallback(async () => {
    setLikedTracks([]);
    setLikedTotal(0);
    setLikedNextOffset(null);
    await fetchLikedPage(0, false);
  }, [fetchLikedPage]);

  const loadMoreLikedTracks = useCallback(async () => {
    if (likedNextOffset !== null && !likedLoading) {
      await fetchLikedPage(likedNextOffset, true);
    }
  }, [fetchLikedPage, likedLoading, likedNextOffset]);

  const value = useMemo<SpotifyContextValue>(
    () => ({
      spotifyClientId,
      spotifyRedirectUri,
      spotifyReady,
      spotifyAuthReady: request !== null,
      spotifyConnected: Boolean(token),
      spotifyBusy,
      spotifyError,
      likedTracks,
      likedTotal,
      likedNextOffset,
      likedLoading,
      likedError,
      saveSpotifySettings: saveSettings,
      connectSpotify,
      disconnectSpotify,
      refreshLikedTracks,
      loadMoreLikedTracks,
    }),
    [
      connectSpotify,
      disconnectSpotify,
      likedError,
      likedLoading,
      likedNextOffset,
      likedTotal,
      likedTracks,
      loadMoreLikedTracks,
      refreshLikedTracks,
      saveSettings,
      spotifyBusy,
      spotifyClientId,
      request,
      spotifyError,
      spotifyReady,
      spotifyRedirectUri,
      token,
    ]
  );

  return <SpotifyContext.Provider value={value}>{children}</SpotifyContext.Provider>;
}

export function useSpotify(): SpotifyContextValue {
  const context = useContext(SpotifyContext);
  if (!context) {
    throw new Error('useSpotify must be used within a SpotifyProvider.');
  }
  return context;
}
