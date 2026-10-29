import React, { useRef, useState } from 'react';
import { ActivityIndicator, Animated, Pressable, View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { T, useTheme } from '../theme/theme';
import { UiDevice } from '../state/HubProvider';
import { Dot } from './ui';
import { num } from '../lib/format';

/** Tile liga/desliga. Ligado = âmbar com brilho; é o único lugar do app com cor quente. */
export function DeviceTile({ device, name, onToggle, onOpen }: {
  device: UiDevice; name: string; onToggle: () => Promise<void>; onOpen: () => void;
}) {
  const { t } = useTheme();
  const scale = useRef(new Animated.Value(1)).current;
  const [pending, setPending] = useState(false);
  const offline = !device.online;
  const on = device.state === true && !offline;
  const unknown = device.state === undefined;
  const fg = on ? t.lampInk : t.ink;
  const sub = offline ? 'Sem sinal' : unknown ? 'Estado desconhecido' : on ? 'Ligado' : 'Desligado';

  const press = (to: number) => Animated.spring(scale, { toValue: to, useNativeDriver: true, speed: 40, bounciness: 0 }).start();

  return (
    <Animated.View style={{ flex: 1, transform: [{ scale }] }}>
      <Pressable
        onPressIn={() => press(0.96)}
        onPressOut={() => press(1)}
        onLongPress={onOpen}
        onPress={async () => {
          if (offline || pending) { onOpen(); return; }
          setPending(true);
          try { await onToggle(); } finally { setPending(false); }
        }}
        style={[{
          minHeight: 136, borderRadius: 28, padding: 16, justifyContent: 'space-between',
          backgroundColor: on ? t.lamp : t.surface,
          borderWidth: offline ? 1.5 : 1, borderStyle: offline ? 'dashed' : 'solid',
          borderColor: on ? t.lamp : t.line, opacity: offline ? 0.7 : 1,
        }, on ? { shadowColor: t.lamp, shadowOpacity: 0.55, shadowRadius: 20, shadowOffset: { width: 0, height: 8 }, elevation: 10 } : null]}
      >
        <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'flex-start' }}>
          <View style={{ width: 40, height: 40, borderRadius: 20, alignItems: 'center', justifyContent: 'center', backgroundColor: on ? 'rgba(59,38,0,0.12)' : t.surfaceAlt }}>
            <Ionicons name={on ? 'bulb' : 'bulb-outline'} size={22} color={fg} />
          </View>
          {pending ? <ActivityIndicator size="small" color={fg} /> : <Dot on={!offline} />}
        </View>
        <View>
          <T v="h3" numberOfLines={2} style={{ color: fg }}>{name}</T>
          <T v="small" w={500} style={{ color: fg, opacity: on ? 0.75 : 1 }} c={on ? undefined : 'inkMuted'}>
            {sub}{device.estimated && !offline ? ' · estimado' : ''}
          </T>
        </View>
      </Pressable>
    </Animated.View>
  );
}

export function SensorCard({ device, name, onOpen }: { device: UiDevice; name: string; onOpen: () => void }) {
  const { t } = useTheme();
  const seen = device.last_seen_seconds;
  const ago = seen === undefined ? '' : seen < 5 ? 'agora' : seen < 60 ? `há ${seen} s` : `há ${Math.floor(seen / 60)} min`;
  return (
    <Pressable
      onPress={onOpen}
      style={({ pressed }) => ({
        flexDirection: 'row', alignItems: 'center', gap: 14, padding: 16, borderRadius: 22, backgroundColor: t.surface,
        borderWidth: device.online ? 1 : 1.5, borderStyle: device.online ? 'solid' : 'dashed', borderColor: t.line,
        opacity: pressed ? 0.85 : device.online ? 1 : 0.7,
      })}
    >
      <View style={{ width: 44, height: 44, borderRadius: 14, backgroundColor: t.signalSoft, alignItems: 'center', justifyContent: 'center' }}>
        <Ionicons name="sunny-outline" size={23} color={t.signal} />
      </View>
      <View style={{ flex: 1 }}>
        <T v="h3" numberOfLines={1}>{name}</T>
        <T v="small" c="inkMuted" w={400}>{device.online ? `Wi-Fi · ${ago}` : 'Wi-Fi · sem sinal'}</T>
      </View>
      <View style={{ alignItems: 'flex-end' }}>
        <T v="title" w={800}>{device.value !== undefined ? num(device.value) : '—'}</T>
        {device.unit ? <T v="small" c="inkMuted">{device.unit}</T> : null}
      </View>
    </Pressable>
  );
}
