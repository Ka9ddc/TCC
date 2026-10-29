import AsyncStorage from '@react-native-async-storage/async-storage';
import * as SecureStore from 'expo-secure-store';

export interface Connection { host: string; token: string }

export interface Scene {
  id: string;
  name: string;
  icon: string;
  steps: { device: string; action: 'on' | 'off' }[];
}

/** Dados que vivem só no celular (o firmware não guarda nomes nem cenas). */
export interface Meta {
  aliases: Record<string, string>;
  ruleNames: Record<string, string>;
  scenes: Scene[];
  themeMode: 'auto' | 'light' | 'dark';
}
export const DEFAULT_META: Meta = { aliases: {}, ruleNames: {}, scenes: [], themeMode: 'auto' };

const HOST = 'meuhub.host';
const TOKEN = 'meuhub.token';
const META = 'meuhub.meta';

async function getSecret(): Promise<string | null> {
  try { return await SecureStore.getItemAsync(TOKEN); }
  catch { return AsyncStorage.getItem(TOKEN); }
}
async function setSecret(v: string) {
  try { await SecureStore.setItemAsync(TOKEN, v); }
  catch { await AsyncStorage.setItem(TOKEN, v); }
}
async function delSecret() {
  try { await SecureStore.deleteItemAsync(TOKEN); } catch { /* noop */ }
  await AsyncStorage.removeItem(TOKEN);
}

export async function loadConnection(): Promise<Connection | null> {
  const host = await AsyncStorage.getItem(HOST);
  const token = await getSecret();
  return host && token ? { host, token } : null;
}
export async function saveConnection(c: Connection) {
  await AsyncStorage.setItem(HOST, c.host);
  await setSecret(c.token);
}
export async function clearConnection() {
  await AsyncStorage.removeItem(HOST);
  await delSecret();
}

export async function loadMeta(): Promise<Meta> {
  try {
    const raw = await AsyncStorage.getItem(META);
    return raw ? { ...DEFAULT_META, ...JSON.parse(raw) } : DEFAULT_META;
  } catch { return DEFAULT_META; }
}
export async function saveMeta(m: Meta) {
  await AsyncStorage.setItem(META, JSON.stringify(m));
}
