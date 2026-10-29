import React, { useEffect } from 'react';
import { Stack, useRootNavigationState, useRouter, useSegments } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import * as SplashScreen from 'expo-splash-screen';
import { useFonts } from 'expo-font';
import {
  BricolageGrotesque_400Regular, BricolageGrotesque_500Medium, BricolageGrotesque_600SemiBold,
  BricolageGrotesque_700Bold, BricolageGrotesque_800ExtraBold,
} from '@expo-google-fonts/bricolage-grotesque';
import { HubProvider, useHub } from '../src/state/HubProvider';
import { ThemeProvider, useTheme } from '../src/theme/theme';
import { ToastProvider } from '../src/components/Toast';

SplashScreen.preventAutoHideAsync().catch(() => {});

function Gate() {
  const { ready, connection } = useHub();
  const { t, dark } = useTheme();
  const segments = useSegments();
  const router = useRouter();
  const nav = useRootNavigationState();

  useEffect(() => {
    if (!nav?.key || !ready) return;
    if (!connection && segments[0] !== 'connect') router.replace('/connect');
  }, [nav?.key, ready, connection, segments, router]);

  useEffect(() => { if (ready) SplashScreen.hideAsync().catch(() => {}); }, [ready]);

  return (
    <>
      <StatusBar style={dark ? 'light' : 'dark'} />
      <Stack screenOptions={{ headerShown: false, contentStyle: { backgroundColor: t.bg } }}>
        <Stack.Screen name="(tabs)" />
        <Stack.Screen name="connect" options={{ animation: 'fade' }} />
        <Stack.Screen name="pairing" options={{ presentation: 'modal' }} />
        <Stack.Screen name="rule/[id]" options={{ presentation: 'modal' }} />
        <Stack.Screen name="scene/[id]" options={{ presentation: 'modal' }} />
      </Stack>
    </>
  );
}

export default function RootLayout() {
  const [loaded] = useFonts({
    'Bricolage-400': BricolageGrotesque_400Regular,
    'Bricolage-500': BricolageGrotesque_500Medium,
    'Bricolage-600': BricolageGrotesque_600SemiBold,
    'Bricolage-700': BricolageGrotesque_700Bold,
    'Bricolage-800': BricolageGrotesque_800ExtraBold,
  });
  if (!loaded) return null;
  return (
    <HubProvider>
      <ThemeProvider>
        <ToastProvider>
          <Gate />
        </ToastProvider>
      </ThemeProvider>
    </HubProvider>
  );
}
