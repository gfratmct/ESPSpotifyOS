import { useEffect, useState } from 'react';
import { Platform, StyleSheet, Text, View } from 'react-native';

import { ActionButton, AppPage, FormField, Notice } from '@/components/music-ui';
import { usePlayer } from '@/context/player-context';
import { useSpotify } from '@/context/spotify-context';

const colors = {
  muted: '#99A59D',
};

export default function SettingsScreen() {
  const { settings, settingsReady, saveSettings, storageError, tracksError } = usePlayer();
  const {
    spotifyClientId,
    spotifyRedirectUri,
    spotifyReady,
    spotifyConnected,
    saveSpotifySettings,
    disconnectSpotify,
  } = useSpotify();
  const [baseUrl, setBaseUrl] = useState('');
  const [apiKey, setApiKey] = useState('');
  const [spotifyId, setSpotifyId] = useState('');
  const [spotifyRedirect, setSpotifyRedirect] = useState('');
  const [saving, setSaving] = useState(false);
  const [savingSpotify, setSavingSpotify] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [spotifyError, setSpotifyError] = useState<string | null>(null);
  const [saved, setSaved] = useState(false);
  const [spotifySaved, setSpotifySaved] = useState(false);

  useEffect(() => {
    if (settingsReady) {
      setBaseUrl(settings.baseUrl);
      setApiKey(settings.apiKey);
    }
  }, [settings, settingsReady]);

  useEffect(() => {
    if (spotifyReady) {
      setSpotifyId(spotifyClientId);
      setSpotifyRedirect(spotifyRedirectUri);
    }
  }, [spotifyClientId, spotifyReady, spotifyRedirectUri]);

  async function onSave() {
    const trimmedUrl = baseUrl.trim().replace(/\/+$/, '');
    if (!trimmedUrl) {
      setError('Enter the base URL of your media service.');
      return;
    }
    try {
      const parsed = new URL(trimmedUrl);
      if (!['http:', 'https:'].includes(parsed.protocol)) {
        setError('Use an HTTP or HTTPS service URL.');
        return;
      }
    } catch {
      setError('Enter a valid service URL, including http:// or https://.');
      return;
    }

    setSaving(true);
    setSaved(false);
    setError(null);
    try {
      await saveSettings({ baseUrl: trimmedUrl, apiKey });
      setBaseUrl(trimmedUrl);
      setSaved(true);
    } catch (saveError) {
      setError(saveError instanceof Error ? saveError.message : 'Could not save your settings.');
    } finally {
      setSaving(false);
    }
  }

  async function onSaveSpotify() {
    const clientId = spotifyId.trim();
    const redirectUri = spotifyRedirect.trim();
    if (!clientId) {
      setSpotifyError('Enter the Client ID from your Spotify Developer Dashboard.');
      return;
    }
    try {
      const parsed = new URL(redirectUri);
      if (parsed.protocol !== 'https:') {
        setSpotifyError('Spotify requires an HTTPS redirect URI.');
        return;
      }
    } catch {
      setSpotifyError('Enter the exact HTTPS redirect URI registered in your Spotify app.');
      return;
    }

    setSavingSpotify(true);
    setSpotifySaved(false);
    setSpotifyError(null);
    try {
      await saveSpotifySettings(clientId, redirectUri);
      setSpotifyId(clientId);
      setSpotifyRedirect(redirectUri);
      setSpotifySaved(true);
    } catch (saveError) {
      setSpotifyError(
        saveError instanceof Error ? saveError.message : 'Could not save Spotify settings.'
      );
    } finally {
      setSavingSpotify(false);
    }
  }

  return (
    <AppPage
      eyebrow="Connection"
      title="Settings"
      subtitle="Connect the app to your self-hosted media service.">
      <FormField
        autoCapitalize="none"
        keyboardType="url"
        label="Service URL"
        onChangeText={(value) => {
          setBaseUrl(value);
          setSaved(false);
        }}
        placeholder="https://music.example.com"
        value={baseUrl}
      />
      <Text style={styles.helpText}>
        Use the address reachable from this iPhone, for example http://192.168.1.20:8081 on your
        home Wi-Fi. Do not use localhost for a phone connected over Wi-Fi.
      </Text>
      <FormField
        label="API key"
        onChangeText={(value) => {
          setApiKey(value);
          setSaved(false);
        }}
        placeholder="Optional if your service has no SECRET"
        secureTextEntry
        value={apiKey}
      />
      <Text style={styles.helpText}>
        {Platform.OS === 'web'
          ? 'Settings are kept in this browser tab session and are not encrypted.'
          : 'The key is stored in secure storage on this device.'}
      </Text>
      {error ? <Notice error>{error}</Notice> : null}
      {saved ? <Notice>Settings saved. The service track list will refresh automatically.</Notice> : null}
      {storageError ? <Notice error>{storageError}</Notice> : null}
      {tracksError ? <Notice error>Service connection: {tracksError}</Notice> : null}
      <View style={styles.saveButton}>
        <ActionButton
          disabled={!settingsReady || saving}
          label={saving ? 'Saving…' : 'Save connection'}
          onPress={() => void onSave()}
        />
      </View>
      <Text style={styles.note}>
        Tracks play directly from the service. When a track starts, the app downloads a local
        copy for future offline playback.
      </Text>

      <View style={styles.spotifySection}>
        <Text style={styles.sectionTitle}>Spotify Liked Songs</Text>
        <Text style={styles.helpText}>
          Uses Spotify’s official API with read-only access. No Spotify audio is imported or
          downloaded.
        </Text>
        <FormField
          label="Spotify Client ID"
          onChangeText={(value) => {
            setSpotifyId(value);
            setSpotifySaved(false);
          }}
          placeholder="Client ID from Spotify Developer Dashboard"
          value={spotifyId}
        />
        <FormField
          autoCapitalize="none"
          keyboardType="url"
          label="Spotify HTTPS redirect URI"
          onChangeText={(value) => {
            setSpotifyRedirect(value);
            setSpotifySaved(false);
          }}
          placeholder="https://your-domain.example/spotify-callback"
          value={spotifyRedirect}
        />
        <Text style={styles.helpText}>
          Register this exact URI in your Spotify app. On iOS, its domain must also be set up as
          an Associated Domain (Universal Link) for this app. Save both values here before
          connecting from Library → Spotify. This app uses PKCE, so do not enter a client secret.
          See the mobile README for setup.
        </Text>
        {spotifyError ? <Notice error>{spotifyError}</Notice> : null}
        {spotifySaved ? <Notice>Spotify settings saved.</Notice> : null}
        <View style={styles.saveButton}>
          <ActionButton
            disabled={!spotifyReady || savingSpotify}
            label={savingSpotify ? 'Saving…' : 'Save Spotify settings'}
            onPress={() => void onSaveSpotify()}
          />
        </View>
        {spotifyConnected ? (
          <View style={styles.disconnectButton}>
            <ActionButton
              label="Disconnect Spotify"
              onPress={() => void disconnectSpotify()}
            />
          </View>
        ) : null}
      </View>
    </AppPage>
  );
}

const styles = StyleSheet.create({
  helpText: {
    color: colors.muted,
    fontSize: 12,
    lineHeight: 18,
    marginTop: 8,
  },
  saveButton: {
    marginTop: 24,
  },
  note: {
    color: colors.muted,
    fontSize: 12,
    lineHeight: 18,
    marginTop: 22,
  },
  spotifySection: {
    borderTopWidth: 1,
    borderTopColor: '#28332C',
    marginTop: 32,
    paddingTop: 24,
  },
  sectionTitle: {
    color: '#F4F7F4',
    fontSize: 20,
    fontWeight: '700',
  },
  disconnectButton: {
    marginTop: 12,
  },
});
