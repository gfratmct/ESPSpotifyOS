import { Platform } from 'react-native';
import * as SecureStore from 'expo-secure-store';

function getBrowserSessionStorage(): Storage {
  if (typeof window === 'undefined') {
    throw new Error('Browser session storage is unavailable.');
  }
  return window.sessionStorage;
}

export async function getStoredValue(key: string): Promise<string | null> {
  if (Platform.OS === 'web') {
    return getBrowserSessionStorage().getItem(key);
  }
  return SecureStore.getItemAsync(key);
}

export async function setStoredValue(key: string, value: string): Promise<void> {
  if (Platform.OS === 'web') {
    getBrowserSessionStorage().setItem(key, value);
    return;
  }
  await SecureStore.setItemAsync(key, value);
}

export async function removeStoredValue(key: string): Promise<void> {
  if (Platform.OS === 'web') {
    getBrowserSessionStorage().removeItem(key);
    return;
  }
  await SecureStore.deleteItemAsync(key);
}
