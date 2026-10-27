import { Image } from 'expo-image';
import { useEffect, useRef, useState } from 'react';
import { Linking, Pressable, StyleSheet, Text, View } from 'react-native';

import { ActionButton, AppPage, Notice, SectionHeading } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';
import { useSpotify } from '@/context/spotify-context';
import { importTrackByQuery } from '@/services/tracks-api';
import type { SpotifyTrack } from '@/services/spotify-api';

const colors = {
  background: '#0A0D0B',
  surface: '#141A16',
  text: '#F4F7F4',
  muted: '#99A59D',
  green: '#B7F36B',
  danger: '#FF8B81',
};

export default function SpotifyScreen() {
  const {
    spotifyClientId,
    spotifyRedirectUri,
    spotifyReady,
    spotifyAuthReady,
    spotifyConnected,
    spotifyBusy,
    spotifyError,
    likedTracks,
    likedTotal,
    likedNextOffset,
    likedLoading,
    likedError,
    connectSpotify,
    disconnectSpotify,
    refreshLikedTracks,
    loadMoreLikedTracks,
  } = useSpotify();
  const { settings, refreshTracks } = usePlayer();
  const [openError, setOpenError] = useState<string | null>(null);
  const [importError, setImportError] = useState<string | null>(null);
  const [importMessage, setImportMessage] = useState<string | null>(null);
  const [importingTrackId, setImportingTrackId] = useState<string | null>(null);
  const autoLoaded = useRef(false);

  async function importTrack(track: SpotifyTrack) {
    if (importingTrackId !== null) {
      return;
    }

    setImportingTrackId(track.id);
    setImportError(null);
    setImportMessage(null);
    try {
      const query = `${track.artists.join(', ')} ${track.title}`;
      const created = await importTrackByQuery(settings, query);
      await refreshTracks();
      setImportMessage(
        created
          ? `Added "${track.title}" to your media library.`
          : `"${track.title}" is already in your media library.`
      );
    } catch (error) {
      setImportError(error instanceof Error ? error.message : 'Could not import this track.');
    } finally {
      setImportingTrackId(null);
    }
  }

  useEffect(() => {
    if (spotifyConnected && !autoLoaded.current) {
      autoLoaded.current = true;
      void refreshLikedTracks();
    }
    if (!spotifyConnected) {
      autoLoaded.current = false;
    }
  }, [refreshLikedTracks, spotifyConnected]);

  return (
    <AppPage
      eyebrow="Official Spotify API"
      title="Liked Songs"
      subtitle="Saved Spotify tracks. Tap to open in Spotify; long-press to import to your media library.">
      {!spotifyConnected ? (
        <View style={styles.connectCard}>
          <Text style={styles.connectTitle}>Connect your Spotify account</Text>
          <Text style={styles.connectDescription}>
            Authorize read-only access to your Liked Songs. Your Spotify library is only shown
            here until you long-press a track to send its artist and title to your configured
            media service for import.
          </Text>
          {!spotifyClientId || !spotifyRedirectUri ? (
            <Notice>
              Enter both the Spotify Client ID and the exact registered HTTPS redirect URI in
              Settings, then save them. A Client ID alone is not enough.
            </Notice>
          ) : !spotifyReady ? (
            <Notice>Loading Spotify settings…</Notice>
          ) : !spotifyAuthReady ? (
            <Notice>Preparing Spotify sign-in…</Notice>
          ) : null}
          <View style={styles.connectButton}>
            <ActionButton
              disabled={
                !spotifyReady ||
                !spotifyAuthReady ||
                spotifyBusy ||
                !spotifyClientId ||
                !spotifyRedirectUri
              }
              label={
                spotifyBusy
                  ? 'Connecting…'
                  : !spotifyAuthReady
                    ? 'Preparing…'
                    : 'Connect Spotify'
              }
              onPress={() => void connectSpotify()}
            />
          </View>
        </View>
      ) : (
        <>
          <View style={styles.headerActions}>
            <Text style={styles.countText}>{likedTotal} liked tracks</Text>
            <Pressable
              accessibilityRole="button"
              onPress={() => void refreshLikedTracks()}
              style={styles.textButton}>
              <Text style={styles.textButtonLabel}>Refresh</Text>
            </Pressable>
            <Pressable
              accessibilityRole="button"
              onPress={() => void disconnectSpotify()}
              style={styles.textButton}>
              <Text style={styles.disconnectLabel}>Disconnect</Text>
            </Pressable>
          </View>
          {likedTracks.map((track) => (
            <SpotifyTrackRow
              key={`${track.id}-${track.addedAt}`}
              track={track}
              onError={setOpenError}
              onImport={importTrack}
              importing={importingTrackId === track.id}
            />
          ))}
          {likedLoading ? <Notice>Loading Spotify Liked Songs…</Notice> : null}
          {likedError ? <Notice error>{likedError}</Notice> : null}
          {openError ? <Notice error>{openError}</Notice> : null}
          {importError ? <Notice error>{importError}</Notice> : null}
          {importMessage ? <Notice>{importMessage}</Notice> : null}
          {!likedLoading && !likedError && likedTracks.length === 0 ? (
            <Notice>No Liked Songs found in this Spotify account.</Notice>
          ) : null}
          {likedNextOffset !== null ? (
            <View style={styles.moreButton}>
              <ActionButton
                disabled={likedLoading}
                label={likedLoading ? 'Loading…' : 'Load more'}
                onPress={() => void loadMoreLikedTracks()}
              />
            </View>
          ) : null}
          <SectionHeading>From Spotify</SectionHeading>
          <Text style={styles.footerText}>
            Metadata comes from Spotify’s Web API. Select a track to open it in Spotify, or
            long-press it to search for and import it into your media library.
          </Text>
        </>
      )}
      {spotifyError ? <Notice error>{spotifyError}</Notice> : null}
    </AppPage>
  );
}

function SpotifyTrackRow({
  track,
  onError,
  onImport,
  importing,
}: {
  track: SpotifyTrack;
  onError: (message: string | null) => void;
  onImport: (track: SpotifyTrack) => Promise<void>;
  importing: boolean;
}) {
  async function openTrack() {
    onError(null);
    try {
      await Linking.openURL(track.spotifyUrl);
    } catch (error) {
      onError(error instanceof Error ? error.message : 'Could not open Spotify.');
    }
  }

  return (
    <Pressable
      accessibilityRole="link"
      accessibilityLabel={`Open ${track.title} by ${track.artists.join(', ')} in Spotify`}
      accessibilityHint="Long press to add this track to your media library."
      onPress={() => void openTrack()}
      delayLongPress={650}
      onLongPress={() => void onImport(track)}
      style={({ pressed }) => [styles.trackRow, pressed && styles.pressed]}>
      {track.artworkUrl ? (
        <Image contentFit="cover" source={{ uri: track.artworkUrl }} style={styles.artwork} />
      ) : (
        <View style={[styles.artwork, styles.artworkPlaceholder]}>
          <Text style={styles.artworkMark}>♪</Text>
        </View>
      )}
      <View style={styles.trackDetails}>
        <Text numberOfLines={1} style={styles.trackTitle}>{track.title}</Text>
        <Text numberOfLines={1} style={styles.trackArtist}>
          {track.artists.join(', ') || 'Unknown artist'}
        </Text>
        <Text numberOfLines={1} style={styles.trackAlbum}>{track.album}</Text>
      </View>
      <Text style={styles.openMark}>{importing ? '…' : '↗'}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  connectCard: {
    backgroundColor: colors.surface,
    borderRadius: 18,
    padding: 18,
    marginTop: 20,
  },
  connectTitle: {
    color: colors.text,
    fontSize: 17,
    fontWeight: '700',
  },
  connectDescription: {
    color: colors.muted,
    fontSize: 13,
    lineHeight: 20,
    marginTop: 8,
  },
  connectButton: {
    marginTop: 18,
  },
  headerActions: {
    flexDirection: 'row',
    alignItems: 'center',
    marginTop: 18,
    marginBottom: 8,
  },
  countText: {
    color: colors.muted,
    fontSize: 12,
    flex: 1,
  },
  textButton: {
    paddingVertical: 8,
    paddingHorizontal: 7,
  },
  textButtonLabel: {
    color: colors.green,
    fontSize: 12,
    fontWeight: '700',
  },
  disconnectLabel: {
    color: colors.danger,
    fontSize: 12,
    fontWeight: '700',
  },
  trackRow: {
    minHeight: 68,
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 8,
  },
  artwork: {
    width: 50,
    height: 50,
    borderRadius: 8,
  },
  artworkPlaceholder: {
    backgroundColor: '#315957',
    alignItems: 'center',
    justifyContent: 'center',
  },
  artworkMark: {
    color: colors.green,
    fontSize: 22,
  },
  trackDetails: {
    flex: 1,
    marginHorizontal: 12,
  },
  trackTitle: {
    color: colors.text,
    fontSize: 14,
    fontWeight: '700',
  },
  trackArtist: {
    color: colors.muted,
    fontSize: 12,
    marginTop: 3,
  },
  trackAlbum: {
    color: colors.muted,
    opacity: 0.7,
    fontSize: 10,
    marginTop: 3,
  },
  openMark: {
    color: colors.green,
    fontSize: 18,
    paddingHorizontal: 4,
  },
  pressed: {
    opacity: 0.72,
  },
  moreButton: {
    marginTop: 14,
  },
  footerText: {
    color: colors.muted,
    fontSize: 12,
    lineHeight: 18,
  },
});
