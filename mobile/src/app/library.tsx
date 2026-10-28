import { useState } from 'react';
import { Platform, Pressable, StyleSheet, Text, View } from 'react-native';

import { AppPage, Notice, SectionHeading, TrackRow } from '@/components/music-ui';
import SpotifyLikedSection from '@/components/spotify-liked-section';
import { usePlayer } from '@/context/player-context';

const colors = {
  green: '#B7F36B',
  muted: '#99A59D',
  surface: '#141A16',
  border: '#28332C',
  text: '#F4F7F4',
};

export default function LibraryScreen() {
  const [section, setSection] = useState<'device' | 'spotify'>('device');
  const { savedTracks, storageError } = usePlayer();

  return (
    <AppPage
      eyebrow="Available anywhere"
      title="Your library"
      subtitle={section === 'device' ? (
        Platform.OS === 'web'
          ? 'Offline downloads require the iOS or Android app.'
          : `${savedTracks.length} ${savedTracks.length === 1 ? 'track' : 'tracks'} saved on this device for offline playback.`
      ) : undefined}>
      <View accessibilityRole="tablist" style={styles.sectionPicker}>
        <Pressable
          accessibilityRole="tab"
          accessibilityState={{ selected: section === 'device' }}
          onPress={() => setSection('device')}
          style={[styles.sectionTab, section === 'device' && styles.selectedSectionTab]}>
          <Text style={[styles.sectionTabText, section === 'device' && styles.selectedSectionText]}>
            Downloads
          </Text>
        </Pressable>
        <Pressable
          accessibilityRole="tab"
          accessibilityState={{ selected: section === 'spotify' }}
          onPress={() => setSection('spotify')}
          style={[styles.sectionTab, section === 'spotify' && styles.selectedSectionTab]}>
          <Text style={[styles.sectionTabText, section === 'spotify' && styles.selectedSectionText]}>
            Spotify
          </Text>
        </Pressable>
      </View>
      {section === 'spotify' ? (
        <SpotifyLikedSection />
      ) : (
        <>
          <SectionHeading>Saved for offline</SectionHeading>
          {storageError ? <Notice error>{storageError}</Notice> : null}
          {savedTracks.map((track) => <TrackRow key={track.id} track={track} saved />)}
          {savedTracks.length === 0 ? (
            <Notice>
              {Platform.OS === 'web'
                ? 'Nothing is saved offline in this browser.'
                : 'Tracks are saved here automatically after you start streaming them. Keep the app open until the download finishes.'}
            </Notice>
          ) : null}
        </>
      )}
    </AppPage>
  );
}

const styles = StyleSheet.create({
  sectionPicker: {
    flexDirection: 'row',
    gap: 8,
    marginTop: 16,
    padding: 4,
    borderRadius: 14,
    borderWidth: 1,
    borderColor: colors.border,
    backgroundColor: colors.surface,
  },
  sectionTab: {
    flex: 1,
    alignItems: 'center',
    paddingVertical: 10,
    borderRadius: 10,
  },
  selectedSectionTab: {
    backgroundColor: colors.green,
  },
  sectionTabText: {
    color: colors.muted,
    fontSize: 13,
    fontWeight: '700',
  },
  selectedSectionText: {
    color: '#0A0D0B',
  },
});
