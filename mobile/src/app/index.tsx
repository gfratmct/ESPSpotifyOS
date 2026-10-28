import { useRouter } from 'expo-router';
import { Platform, Pressable, StyleSheet, Text, View } from 'react-native';

import { ActionButton, AppPage, Notice, SectionHeading, TrackRow } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';

const colors = {
  green: '#B7F36B',
  text: '#F4F7F4',
  muted: '#99A59D',
  surface: '#141A16',
  border: '#28332C',
};

export default function HomeScreen() {
  const router = useRouter();
  const {
    settings,
    storageError,
    tracks,
    tracksTotal,
    tracksLoading,
    tracksError,
    refreshTracks,
    savedTracks,
  } = usePlayer();

  return (
    <AppPage
      eyebrow="Your music, your way"
      title="Listen local."
      subtitle={
        Platform.OS === 'web'
          ? 'Stream directly from your library in the browser.'
          : 'Stream from your library. Keep every played track ready for offline listening.'
      }>
      <View style={styles.heroCard}>
        <Text style={styles.heroLabel}>YOUR SERVICE LIBRARY</Text>
        <Text style={styles.heroCount}>{tracksTotal}</Text>
        <Text style={styles.heroCaption}>
          {tracksTotal === 1 ? 'track ready to play' : 'tracks ready to play'}
        </Text>
        <View style={styles.heroActions}>
          <Pressable
            accessibilityRole="button"
            onPress={() => router.push('/explore')}
            style={styles.heroLink}>
            <Text style={styles.heroLinkText}>Explore tracks  ›</Text>
          </Pressable>
          <Text style={styles.heroOffline}>
            {Platform.OS === 'web' ? 'Offline in mobile app' : `${savedTracks.length} saved offline`}
          </Text>
        </View>
        <View style={styles.heroAccent} />
      </View>

      {!settings.baseUrl ? (
        <Notice>
          Connect your media service to get started.{' '}
          <Text style={styles.inlineLink} onPress={() => router.push('/settings')}>
            Open Settings
          </Text>
        </Notice>
      ) : null}
      {storageError ? <Notice error>{storageError}</Notice> : null}
      {tracksError ? (
        <Notice error>
          {tracksError}{' '}
          <Text style={styles.inlineLink} onPress={() => void refreshTracks()}>
            Try again
          </Text>
        </Notice>
      ) : null}

      <View style={styles.sectionHeader}>
        <SectionHeading>Recently added</SectionHeading>
        <Pressable onPress={() => void refreshTracks()} accessibilityRole="button">
          <Text style={styles.refreshText}>{tracksLoading ? 'Loading…' : 'Refresh'}</Text>
        </Pressable>
      </View>
      {tracks.slice(0, 5).map((track) => (
        <TrackRow
          key={track.id}
          track={track}
          saved={savedTracks.some(
            (saved) => saved.id === track.id && saved.sourceUrl === settings.baseUrl
          )}
        />
      ))}
      {!tracksLoading && tracks.length === 0 && !tracksError && settings.baseUrl ? (
        <Notice>Your service library is empty. Add tracks to the service to see them here.</Notice>
      ) : null}
      {!settings.baseUrl ? (
        <View style={styles.configButton}>
          <ActionButton label="Connect your service" onPress={() => router.push('/settings')} />
        </View>
      ) : null}
    </AppPage>
  );
}

const styles = StyleSheet.create({
  heroCard: {
    overflow: 'hidden',
    backgroundColor: '#21372A',
    borderRadius: 22,
    minHeight: 184,
    padding: 21,
    marginTop: 18,
  },
  heroLabel: {
    color: colors.green,
    fontSize: 10,
    fontWeight: '800',
    letterSpacing: 1.8,
  },
  heroCount: {
    color: colors.text,
    fontSize: 46,
    fontWeight: '800',
    marginTop: 8,
  },
  heroCaption: {
    color: '#C4D0C7',
    fontSize: 13,
    marginTop: -2,
  },
  heroActions: {
    flexDirection: 'row',
    alignItems: 'center',
    marginTop: 18,
  },
  heroLink: {
    paddingRight: 18,
  },
  heroLinkText: {
    color: colors.green,
    fontSize: 13,
    fontWeight: '700',
  },
  heroOffline: {
    borderLeftWidth: 1,
    borderLeftColor: '#506557',
    paddingLeft: 14,
    color: '#C4D0C7',
    fontSize: 12,
  },
  heroAccent: {
    position: 'absolute',
    width: 130,
    height: 130,
    right: -30,
    top: 28,
    borderRadius: 65,
    borderWidth: 1,
    borderColor: '#527154',
    opacity: 0.6,
  },
  sectionHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  refreshText: {
    color: colors.green,
    fontSize: 12,
    fontWeight: '700',
    marginTop: 24,
    marginBottom: 12,
  },
  inlineLink: {
    color: colors.green,
    fontWeight: '700',
  },
  configButton: {
    marginTop: 18,
  },
});
