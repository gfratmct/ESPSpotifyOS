import { AppPage, Notice, SectionHeading, TrackRow } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';

export default function LibraryScreen() {
  const { savedTracks, storageError } = usePlayer();

  return (
    <AppPage
      eyebrow="Available anywhere"
      title="Your library"
      subtitle={`${savedTracks.length} ${savedTracks.length === 1 ? 'track' : 'tracks'} saved on this device for offline playback.`}>
      <SectionHeading>Saved for offline</SectionHeading>
      {storageError ? <Notice error>{storageError}</Notice> : null}
      {savedTracks.map((track) => <TrackRow key={track.id} track={track} saved />)}
      {savedTracks.length === 0 ? (
        <Notice>
          Tracks are saved here automatically after you start streaming them. Keep the app open
          until the download finishes.
        </Notice>
      ) : null}
    </AppPage>
  );
}
