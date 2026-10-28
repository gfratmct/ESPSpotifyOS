import { useRouter } from 'expo-router';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { ActionButton, AppPage, Notice } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';

const colors = {
  background: '#0A0D0B',
  surface: '#141A16',
  text: '#F4F7F4',
  muted: '#99A59D',
  green: '#B7F36B',
  border: '#28332C',
};

function formatTime(seconds: number): string {
  const totalSeconds = Math.max(0, Math.floor(seconds || 0));
  return `${Math.floor(totalSeconds / 60)}:${String(totalSeconds % 60).padStart(2, '0')}`;
}

export default function PlayerScreen() {
  const router = useRouter();
  const {
    currentTrack,
    isPlaying,
    currentTime,
    duration,
    playbackError,
    cacheError,
    canPlayPreviousTrack,
    canPlayNextTrack,
    shuffleEnabled,
    repeatMode,
    playPreviousTrack,
    playNextTrack,
    toggleShuffle,
    cycleRepeatMode,
    togglePlayback,
  } = usePlayer();
  const progress = duration > 0 ? Math.min(currentTime / duration, 1) : 0;

  return (
    <AppPage
      eyebrow="Now playing"
      title="Player"
      showMiniPlayer={false}>
      {currentTrack ? (
        <View style={styles.content}>
          <View style={styles.artwork}>
            <Text style={styles.artworkNote}>♫</Text>
            <View style={styles.waveform}>
              {[22, 38, 28, 54, 34, 46, 24, 40, 20].map((height, index) => (
                <View key={index} style={[styles.waveBar, { height }]} />
              ))}
            </View>
          </View>

          <View style={styles.trackInfo}>
            <Text numberOfLines={2} style={styles.trackTitle}>{currentTrack.title}</Text>
            <Text numberOfLines={1} style={styles.trackArtist}>
              {currentTrack.artist || 'Unknown artist'}
            </Text>
            <Text numberOfLines={1} style={styles.trackAlbum}>
              {currentTrack.album || 'Your media library'}
            </Text>
          </View>

          <View accessibilityLabel="Playback progress" style={styles.progressTrack}>
            <View style={[styles.progressFill, { width: `${progress * 100}%` }]} />
          </View>
          <View style={styles.timeRow}>
            <Text style={styles.timeText}>{formatTime(currentTime)}</Text>
            <Text style={styles.timeText}>{formatTime(duration)}</Text>
          </View>

          <View style={styles.controls}>
            <Pressable
              accessibilityRole="button"
              accessibilityLabel={shuffleEnabled ? 'Turn shuffle off' : 'Turn shuffle on'}
              accessibilityState={{ selected: shuffleEnabled }}
              onPress={toggleShuffle}
              style={({ pressed }) => [
                styles.modeButton,
                shuffleEnabled && styles.activeModeButton,
                pressed && styles.pressed,
              ]}>
              <Text style={[styles.modeIcon, shuffleEnabled && styles.activeModeText]}>⤨</Text>
            </Pressable>
            <Pressable
              accessibilityRole="button"
              accessibilityLabel="Play previous track"
              disabled={!canPlayPreviousTrack}
              onPress={() => void playPreviousTrack()}
              style={({ pressed }) => [
                styles.skipButton,
                !canPlayPreviousTrack && styles.disabledControl,
                pressed && styles.pressed,
              ]}>
              <Text style={styles.skipIcon}>|◀</Text>
            </Pressable>
            <Pressable
              accessibilityRole="button"
              accessibilityLabel={isPlaying ? 'Pause' : 'Play'}
              onPress={togglePlayback}
              style={({ pressed }) => [styles.playButton, pressed && styles.pressed]}>
              <Text style={styles.playIcon}>{isPlaying ? 'Ⅱ' : '▶'}</Text>
            </Pressable>
            <Pressable
              accessibilityRole="button"
              accessibilityLabel="Play next track"
              disabled={!canPlayNextTrack}
              onPress={() => void playNextTrack()}
              style={({ pressed }) => [
                styles.skipButton,
                !canPlayNextTrack && styles.disabledControl,
                pressed && styles.pressed,
              ]}>
              <Text style={styles.skipIcon}>▶|</Text>
            </Pressable>
            <Pressable
              accessibilityRole="button"
              accessibilityLabel={`Repeat ${repeatMode}`}
              accessibilityState={{ selected: repeatMode !== 'off' }}
              onPress={cycleRepeatMode}
              style={({ pressed }) => [
                styles.modeButton,
                repeatMode !== 'off' && styles.activeModeButton,
                pressed && styles.pressed,
              ]}>
              <Text style={[styles.modeIcon, repeatMode !== 'off' && styles.activeModeText]}>
                ↻
              </Text>
              {repeatMode === 'one' ? <Text style={styles.repeatOneBadge}>1</Text> : null}
            </Pressable>
          </View>
          {playbackError ? <Notice error>{playbackError}</Notice> : null}
          {cacheError ? <Notice error>{cacheError}</Notice> : null}
        </View>
      ) : (
        <View style={styles.emptyState}>
          <View style={styles.emptyArtwork}>
            <Text style={styles.emptyNote}>♫</Text>
          </View>
          <Text style={styles.emptyTitle}>Nothing playing yet</Text>
          <ActionButton label="Explore tracks" onPress={() => router.push('/explore')} />
        </View>
      )}
    </AppPage>
  );
}

const styles = StyleSheet.create({
  content: {
    alignItems: 'stretch',
    paddingTop: 16,
  },
  artwork: {
    width: '100%',
    maxWidth: 340,
    aspectRatio: 1,
    alignSelf: 'center',
    borderRadius: 28,
    backgroundColor: colors.surface,
    alignItems: 'center',
    justifyContent: 'center',
    overflow: 'hidden',
  },
  artworkNote: {
    color: colors.green,
    fontSize: 104,
    fontWeight: '700',
  },
  waveform: {
    position: 'absolute',
    bottom: 36,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 7,
  },
  waveBar: {
    width: 5,
    borderRadius: 3,
    backgroundColor: colors.green,
    opacity: 0.75,
  },
  trackInfo: {
    marginTop: 28,
  },
  trackTitle: {
    color: colors.text,
    fontSize: 25,
    fontWeight: '800',
    textAlign: 'center',
  },
  trackArtist: {
    color: colors.muted,
    fontSize: 16,
    marginTop: 8,
    textAlign: 'center',
  },
  trackAlbum: {
    color: colors.muted,
    fontSize: 12,
    marginTop: 5,
    textAlign: 'center',
  },
  progressTrack: {
    height: 4,
    borderRadius: 2,
    backgroundColor: colors.border,
    marginTop: 28,
    overflow: 'hidden',
  },
  progressFill: {
    height: '100%',
    borderRadius: 2,
    backgroundColor: colors.green,
  },
  timeRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 8,
  },
  timeText: {
    color: colors.muted,
    fontSize: 11,
  },
  controls: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 8,
    marginTop: 28,
    alignSelf: 'center',
  },
  skipButton: {
    width: 42,
    height: 48,
    alignItems: 'center',
    justifyContent: 'center',
  },
  skipIcon: {
    color: colors.text,
    fontSize: 26,
    fontWeight: '700',
  },
  playButton: {
    width: 64,
    height: 64,
    borderRadius: 32,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.green,
  },
  playIcon: {
    color: colors.background,
    fontSize: 30,
    fontWeight: '800',
  },
  disabledControl: {
    opacity: 0.3,
  },
  pressed: {
    opacity: 0.72,
  },
  modeButton: {
    width: 40,
    height: 44,
    alignItems: 'center',
    justifyContent: 'center',
    position: 'relative',
    borderRadius: 22,
  },
  activeModeButton: {
    backgroundColor: colors.surface,
  },
  modeIcon: {
    color: colors.muted,
    fontSize: 21,
    fontWeight: '700',
  },
  activeModeText: {
    color: colors.green,
  },
  repeatOneBadge: {
    position: 'absolute',
    right: 3,
    top: 2,
    minWidth: 13,
    height: 13,
    overflow: 'hidden',
    borderRadius: 7,
    backgroundColor: colors.green,
    color: colors.background,
    fontSize: 9,
    fontWeight: '800',
    textAlign: 'center',
    lineHeight: 13,
  },
  emptyState: {
    alignItems: 'center',
    paddingTop: 34,
  },
  emptyArtwork: {
    width: 200,
    height: 200,
    borderRadius: 28,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.surface,
  },
  emptyNote: {
    color: colors.green,
    fontSize: 82,
  },
  emptyTitle: {
    color: colors.text,
    fontSize: 22,
    fontWeight: '800',
    marginTop: 24,
  },
});
