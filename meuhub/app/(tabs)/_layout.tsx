import React from 'react';
import { Tabs } from 'expo-router';
import { Ionicons } from '@expo/vector-icons';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { useTheme } from '../../src/theme/theme';

const tab = (name: string, title: string, on: string, off: string) => ({ name, title, on, off });
const TABS = [
  tab('index', 'Início', 'home', 'home-outline'),
  tab('devices', 'Dispositivos', 'bulb', 'bulb-outline'),
  tab('automations', 'Automações', 'git-network', 'git-network-outline'),
  tab('settings', 'Ajustes', 'settings', 'settings-outline'),
];

export default function TabsLayout() {
  const { t } = useTheme();
  const insets = useSafeAreaInsets();
  return (
    <Tabs
      screenOptions={{
        headerShown: false,
        tabBarActiveTintColor: t.ink,
        tabBarInactiveTintColor: t.inkMuted,
        tabBarLabelStyle: { fontFamily: 'Bricolage-600', fontSize: 11 },
        tabBarStyle: { backgroundColor: t.surface, borderTopColor: t.line, height: 58 + insets.bottom, paddingTop: 6 },
        sceneStyle: { backgroundColor: t.bg },
      }}
    >
      {TABS.map((x) => (
        <Tabs.Screen
          key={x.name} name={x.name}
          options={{
            title: x.title,
            tabBarIcon: ({ focused, color }) => <Ionicons name={(focused ? x.on : x.off) as any} size={23} color={color} />,
          }}
        />
      ))}
    </Tabs>
  );
}
