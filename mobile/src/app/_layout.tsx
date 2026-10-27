import { DarkTheme, ThemeProvider } from 'expo-router';
import { StatusBar } from 'expo-status-bar';

import AppTabs from '@/components/app-tabs';
import { PlayerProvider } from '@/context/player-context';
import { SpotifyProvider } from '@/context/spotify-context';
import { View } from 'react-native';

export default function TabLayout() {
  return (
    <ThemeProvider value={DarkTheme}>
      {/* <View style={{ marginBottom: 0 }}> */}
        <StatusBar style="light" />
        <SpotifyProvider>
          <PlayerProvider>
            <AppTabs />
          </PlayerProvider>
        </SpotifyProvider>
      {/* </View> */}
    </ThemeProvider>
  );
}
