import { Tabs } from 'expo-router';
import { StyleSheet, Text, View } from 'react-native';

const colors = {
  background: '#0A0D0B',
  text: '#F4F7F4',
  muted: '#99A59D',
  green: '#B7F36B',
  border: '#28332C',
};

const icons: Record<string, string> = {
  index: '⌂',
  explore: '⌕',
  player: '▶',
  library: '↓',
  settings: '⚙',
};

export default function AppTabsWeb() {
  return (
    <Tabs
      screenOptions={({ route }) => ({
        headerShown: false,
        tabBarPosition: 'bottom',
        tabBarActiveTintColor: colors.text,
        tabBarInactiveTintColor: colors.muted,
        tabBarStyle: styles.tabBar,
        tabBarLabelStyle: styles.label,
        tabBarIcon: ({ color }) => (
          <View style={[styles.iconWrap, route.name === 'player' && styles.playerIconWrap]}>
            <Text
              style={[
                styles.icon,
                { color: route.name === 'player' ? colors.background : color },
                route.name === 'player' && styles.playerIcon,
              ]}>
              {icons[route.name] ?? '•'}
            </Text>
          </View>
        ),
      })}>
      <Tabs.Screen name="index" options={{ title: 'Home' }} />
      <Tabs.Screen name="explore" options={{ title: 'Explore' }} />
      <Tabs.Screen name="player" options={{ title: 'Player' }} />
      <Tabs.Screen name="library" options={{ title: 'Library' }} />
      <Tabs.Screen name="settings" options={{ title: 'Settings' }} />
    </Tabs>
  );
}

const styles = StyleSheet.create({
  tabBar: {
    height: 66,
    paddingTop: 5,
    paddingBottom: 4,
    backgroundColor: colors.background,
    borderTopColor: colors.border,
  },
  label: {
    fontSize: 10,
    fontWeight: '600',
    marginTop: 1,
  },
  iconWrap: {
    width: 30,
    height: 26,
    alignItems: 'center',
    justifyContent: 'center',
  },
  icon: {
    fontSize: 20,
    lineHeight: 24,
    fontWeight: '700',
  },
  playerIconWrap: {
    width: 34,
    height: 34,
    marginTop: -4,
    borderRadius: 17,
    backgroundColor: colors.green,
  },
  playerIcon: {
    fontSize: 14,
    lineHeight: 18,
  },
});
