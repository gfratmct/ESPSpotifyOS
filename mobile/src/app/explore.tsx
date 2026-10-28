import { useMemo, useState } from 'react';
import { Pressable, Text } from 'react-native';

import { AppPage, Notice, SearchField, SectionHeading, TrackRow } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';

export default function ExploreScreen() {
  const [query, setQuery] = useState('');
  const {
    tracks,
    tracksTotal,
    hasMoreTracks,
    tracksLoading,
    tracksError,
    settings,
    refreshTracks,
    loadMoreTracks,
    savedTracks,
  } = usePlayer();
  const filteredTracks = useMemo(() => {
    const search = query.trim().toLocaleLowerCase();
    if (!search) {
      return tracks;
    }
    return tracks.filter((track) =>
      [track.title, track.artist, track.album].some((value) =>
        value.toLocaleLowerCase().includes(search)
      )
    );
  }, [query, tracks]);

  return (
    <AppPage
      eyebrow="Discover"
      title="Explore"
      onScroll={({ nativeEvent }) => {
        const { contentOffset, contentSize, layoutMeasurement } = nativeEvent;
        if (
          !query &&
          hasMoreTracks &&
          !tracksLoading &&
          contentOffset.y + layoutMeasurement.height >= contentSize.height - 280
        ) {
          void loadMoreTracks();
        }
      }}>
      <SearchField value={query} onChangeText={setQuery} />
      <SectionHeading>
        {query ? `${filteredTracks.length} results` : `${tracksTotal} tracks`}
      </SectionHeading>

      {tracksLoading ? <Notice>Loading tracks from your service…</Notice> : null}
      {tracksError ? (
        <Notice error>
          {tracksError}{' '}
          <Text onPress={() => void refreshTracks()} style={{ color: '#B7F36B', fontWeight: '700' }}>
            Try again
          </Text>
        </Notice>
      ) : null}
      {!settings.baseUrl ? (
        <Notice>Set your media service URL and API key in Settings to browse tracks.</Notice>
      ) : null}
      {filteredTracks.map((track) => (
        <TrackRow
          key={track.id}
          track={track}
          saved={savedTracks.some(
            (saved) => saved.id === track.id && saved.sourceUrl === settings.baseUrl
          )}
        />
      ))}
      {query && hasMoreTracks ? (
        <Pressable
          accessibilityRole="button"
          accessibilityLabel="Load more tracks"
          disabled={tracksLoading}
          onPress={() => void loadMoreTracks()}
          style={{
            alignSelf: 'center',
            paddingHorizontal: 20,
            paddingVertical: 12,
            opacity: tracksLoading ? 0.5 : 1,
          }}>
          <Text style={{ color: '#B7F36B', fontSize: 24 }}>↓</Text>
        </Pressable>
      ) : null}
      {!tracksLoading && settings.baseUrl && !tracksError && filteredTracks.length === 0 ? (
        <Notice>
          {query
            ? 'No tracks match your search. Try another title or artist.'
            : 'No tracks are available yet. Add them to the service library and refresh.'}
        </Notice>
      ) : null}
    </AppPage>
  );
}
