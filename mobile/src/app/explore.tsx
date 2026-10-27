import { useMemo, useState } from 'react';
import { Text } from 'react-native';

import { AppPage, Notice, SearchField, SectionHeading, TrackRow } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';

export default function ExploreScreen() {
  const [query, setQuery] = useState('');
  const { tracks, tracksLoading, tracksError, settings, refreshTracks, savedTracks } = usePlayer();
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
      subtitle="Search and play any track currently available in your service library.">
      <SearchField value={query} onChangeText={setQuery} />
      <SectionHeading>
        {query ? `${filteredTracks.length} results` : `${tracks.length} available tracks`}
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
