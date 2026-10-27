import { useRouter } from 'expo-router';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  type TextInputProps,
} from 'react-native';
import { SafeAreaView, useSafeAreaInsets } from 'react-native-safe-area-context';

import { usePlayer } from '@/context/player-context';
import type { Track } from '@/services/tracks-api';
import type { ReactNode } from 'react';

const colors = {
  background: '#0A0D0B',
  surface: '#141A16',
  raised: '#1C241F',
  text: '#F4F7F4',
  muted: '#99A59D',
  green: '#B7F36B',
  border: '#28332C',
  danger: '#FF8B81',
};

export function AppPage({
  eyebrow,
  title,
  subtitle,
  children,
}: {
  eyebrow: string;
  title: string;
  subtitle?: string;
  children: ReactNode;
}) {
  const insets = useSafeAreaInsets();

  return (
    <SafeAreaView style={styles.safeArea} edges={['top', 'left', 'right']}>
      <ScrollView
        style={styles.page}
        contentContainerStyle={[
          styles.pageContent,
          { paddingBottom: 24 + insets.bottom + 72 },
        ]}
        keyboardDismissMode="on-drag"
        keyboardShouldPersistTaps="handled">
        <Text style={styles.eyebrow}>{eyebrow.toUpperCase()}</Text>
        <Text style={styles.pageTitle}>{title}</Text>
        {subtitle ? <Text style={styles.subtitle}>{subtitle}</Text> : null}
        {children}
      </ScrollView>
      <MiniPlayer />
    </SafeAreaView>
  );
}

export function TrackRow({
  track,
  saved = false,
}: {
  track: Track;
  saved?: boolean;
}) {
  const { playTrack, currentTrack, isPlaying, togglePlayback } = usePlayer();
  const isCurrent = currentTrack?.id === track.id;
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={`${isCurrent && isPlaying ? 'Pause' : 'Play'} ${track.title} by ${track.artist}`}
      onPress={() => {
        if (isCurrent && isPlaying) {
          togglePlayback();
        } else {
          void playTrack(track);
        }
      }}
      style={({ pressed }) => [styles.trackRow, pressed && styles.pressed]}>
      <View style={[styles.artwork, { backgroundColor: artworkColor(track.id) }]}>
        <Text style={styles.artworkMark}>{track.title.slice(0, 1).toUpperCase() || '♪'}</Text>
      </View>
      <View style={styles.trackDetails}>
        <Text numberOfLines={1} style={[styles.trackTitle, isCurrent && styles.activeTrack]}>
          {track.title}
        </Text>
        <Text numberOfLines={1} style={styles.trackArtist}>
          {track.artist || 'Unknown artist'}
          {saved ? '  ·  OFFLINE' : ''}
        </Text>
      </View>
      <Text style={styles.duration}>{formatTime(track.duration)}</Text>
      <View style={styles.rowPlay}>
        <Text style={styles.rowPlayText}>{isCurrent && isPlaying ? 'Ⅱ' : '▶'}</Text>
      </View>
    </Pressable>
  );
}

export function SectionHeading({ children }: { children: React.ReactNode }) {
  return <Text style={styles.sectionHeading}>{children}</Text>;
}

export function Notice({
  children,
  error = false,
}: {
  children: ReactNode;
  error?: boolean;
}) {
  return (
    <View style={[styles.notice, error && styles.errorNotice]}>
      <Text style={[styles.noticeText, error && styles.errorText]}>{children}</Text>
    </View>
  );
}

export function SearchField({
  value,
  onChangeText,
}: {
  value: string;
  onChangeText: (value: string) => void;
}) {
  return (
    <View style={styles.searchBox}>
      <Text style={styles.searchIcon}>⌕</Text>
      <TextInput
        accessibilityLabel="Search tracks"
        autoCapitalize="none"
        onChangeText={onChangeText}
        placeholder="Search title, artist, or album"
        placeholderTextColor={colors.muted}
        returnKeyType="search"
        style={styles.searchInput}
        value={value}
      />
      {value ? (
        <Pressable accessibilityLabel="Clear search" onPress={() => onChangeText('')}>
          <Text style={styles.clearText}>Clear</Text>
        </Pressable>
      ) : null}
    </View>
  );
}

export function ActionButton({
  label,
  onPress,
  disabled = false,
}: {
  label: string;
  onPress: () => void;
  disabled?: boolean;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      disabled={disabled}
      onPress={onPress}
      style={({ pressed }) => [
        styles.actionButton,
        disabled && styles.disabledButton,
        pressed && !disabled && styles.pressed,
      ]}>
      <Text style={styles.actionButtonText}>{label}</Text>
    </Pressable>
  );
}

export function FormField({
  label,
  value,
  onChangeText,
  placeholder,
  secureTextEntry = false,
  autoCapitalize = 'none',
  keyboardType = 'default',
}: {
  label: string;
  value: string;
  onChangeText: (value: string) => void;
  placeholder: string;
  secureTextEntry?: boolean;
  autoCapitalize?: 'none' | 'sentences' | 'words' | 'characters';
  keyboardType?: TextInputProps['keyboardType'];
}) {
  return (
    <View style={styles.formField}>
      <Text style={styles.fieldLabel}>{label}</Text>
      <TextInput
        autoCapitalize={autoCapitalize}
        autoCorrect={false}
        keyboardType={keyboardType}
        onChangeText={onChangeText}
        placeholder={placeholder}
        placeholderTextColor={colors.muted}
        secureTextEntry={secureTextEntry}
        style={styles.fieldInput}
        value={value}
      />
    </View>
  );
}

function MiniPlayer() {
  const router = useRouter();
  const {
    currentTrack,
    isPlaying,
    currentTime,
    duration,
    playbackError,
    cacheError,
    togglePlayback,
    savedTracks,
    settings,
  } = usePlayer();

  if (!currentTrack) {
    return null;
  }
  const progress = duration > 0 ? Math.min(currentTime / duration, 1) : 0;
  const sourceUrl = 'sourceUrl' in currentTrack ? currentTrack.sourceUrl : settings.baseUrl;
  const saved = savedTracks.some(
    (track) => track.id === currentTrack.id && track.sourceUrl === sourceUrl
  );

  return (
    <View style={styles.playerContainer}>
      <Pressable style={styles.playerInfo} onPress={() => router.push('/library')}>
        <View style={styles.playerArt}>
          <Text style={styles.playerArtMark}>♪</Text>
        </View>
        <View style={styles.playerText}>
          <Text numberOfLines={1} style={styles.playerTitle}>
            {currentTrack.title}
          </Text>
          <Text numberOfLines={1} style={styles.playerSubtitle}>
            {playbackError ? 'Playback issue' : saved ? 'Saved for offline' : 'Saving for offline'}
          </Text>
        </View>
      </Pressable>
      <Pressable
        accessibilityRole="button"
        accessibilityLabel={isPlaying ? 'Pause playback' : 'Resume playback'}
        onPress={togglePlayback}
        style={styles.playerControl}>
        <Text style={styles.playerControlText}>{isPlaying ? 'Ⅱ' : '▶'}</Text>
      </Pressable>
      <View style={styles.progressTrack}>
        <View style={[styles.progressFill, { width: `${progress * 100}%` }]} />
      </View>
      {playbackError || cacheError ? (
        <Text numberOfLines={2} style={styles.playerError}>
          {playbackError ?? cacheError}
        </Text>
      ) : null}
    </View>
  );
}

function artworkColor(id: number): string {
  const palette = ['#60454F', '#315957', '#594B77', '#765C3D', '#31516B'];
  return palette[Math.abs(id) % palette.length];
}

export function formatTime(seconds: number): string {
  const safeSeconds = Math.max(0, Math.floor(seconds || 0));
  return `${Math.floor(safeSeconds / 60)}:${String(safeSeconds % 60).padStart(2, '0')}`;
}

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: colors.background,
  },
  page: {
    flex: 1,
  },
  pageContent: {
    flexGrow: 1,
    paddingHorizontal: 22,
    paddingTop: 16,
    paddingBottom: 22,
  },
  eyebrow: {
    color: colors.green,
    fontSize: 11,
    fontWeight: '700',
    letterSpacing: 2,
    marginBottom: 10,
  },
  pageTitle: {
    color: colors.text,
    fontSize: 34,
    fontWeight: '800',
    letterSpacing: -1,
  },
  subtitle: {
    color: colors.muted,
    fontSize: 14,
    lineHeight: 21,
    marginTop: 7,
    marginBottom: 22,
  },
  sectionHeading: {
    color: colors.text,
    fontSize: 18,
    fontWeight: '700',
    marginTop: 24,
    marginBottom: 12,
  },
  trackRow: {
    minHeight: 68,
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 8,
  },
  artwork: {
    width: 48,
    height: 48,
    borderRadius: 9,
    alignItems: 'center',
    justifyContent: 'center',
  },
  artworkMark: {
    color: '#FFFFFF',
    fontSize: 21,
    fontWeight: '800',
  },
  trackDetails: {
    flex: 1,
    marginHorizontal: 12,
  },
  trackTitle: {
    color: colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  activeTrack: {
    color: colors.green,
  },
  trackArtist: {
    color: colors.muted,
    fontSize: 12,
    marginTop: 4,
  },
  duration: {
    color: colors.muted,
    fontSize: 12,
    marginRight: 10,
  },
  rowPlay: {
    width: 28,
    height: 32,
    alignItems: 'center',
    justifyContent: 'center',
  },
  rowPlayText: {
    color: colors.green,
    fontSize: 14,
    fontWeight: '700',
  },
  pressed: {
    opacity: 0.72,
  },
  notice: {
    borderRadius: 12,
    backgroundColor: colors.surface,
    paddingHorizontal: 14,
    paddingVertical: 12,
    marginTop: 12,
  },
  noticeText: {
    color: colors.muted,
    fontSize: 13,
    lineHeight: 19,
  },
  errorNotice: {
    borderWidth: 1,
    borderColor: '#603A37',
  },
  errorText: {
    color: colors.danger,
  },
  searchBox: {
    minHeight: 52,
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: colors.surface,
    borderRadius: 14,
    paddingHorizontal: 14,
    marginTop: 18,
  },
  searchIcon: {
    color: colors.muted,
    fontSize: 24,
    marginRight: 10,
  },
  searchInput: {
    flex: 1,
    color: colors.text,
    fontSize: 14,
    paddingVertical: 12,
  },
  clearText: {
    color: colors.green,
    fontSize: 12,
    fontWeight: '700',
  },
  actionButton: {
    minHeight: 50,
    backgroundColor: colors.green,
    borderRadius: 14,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: 18,
  },
  actionButtonText: {
    color: '#16200F',
    fontSize: 14,
    fontWeight: '800',
  },
  disabledButton: {
    opacity: 0.48,
  },
  playerContainer: {
    borderTopWidth: 1,
    borderColor: colors.border,
    backgroundColor: colors.surface,
    paddingHorizontal: 16,
    paddingTop: 10,
    paddingBottom: 9,
  },
  playerInfo: {
    flexDirection: 'row',
    alignItems: 'center',
    flex: 1,
  },
  playerArt: {
    width: 38,
    height: 38,
    borderRadius: 8,
    backgroundColor: '#345A40',
    alignItems: 'center',
    justifyContent: 'center',
  },
  playerArtMark: {
    color: colors.green,
    fontSize: 21,
  },
  playerText: {
    flex: 1,
    marginHorizontal: 11,
  },
  playerTitle: {
    color: colors.text,
    fontSize: 13,
    fontWeight: '700',
  },
  playerSubtitle: {
    color: colors.muted,
    fontSize: 11,
    marginTop: 3,
  },
  playerControl: {
    position: 'absolute',
    right: 17,
    top: 10,
    width: 42,
    height: 42,
    alignItems: 'center',
    justifyContent: 'center',
  },
  playerControlText: {
    color: colors.green,
    fontSize: 19,
    fontWeight: '800',
  },
  progressTrack: {
    height: 2,
    backgroundColor: colors.border,
    marginTop: 8,
  },
  progressFill: {
    height: 2,
    backgroundColor: colors.green,
  },
  playerError: {
    color: colors.danger,
    fontSize: 11,
    marginTop: 6,
  },
  formField: {
    marginTop: 18,
  },
  fieldLabel: {
    color: colors.text,
    fontSize: 13,
    fontWeight: '700',
    marginBottom: 8,
  },
  fieldInput: {
    minHeight: 50,
    borderRadius: 12,
    backgroundColor: colors.surface,
    paddingHorizontal: 14,
    color: colors.text,
    fontSize: 14,
  },
});
