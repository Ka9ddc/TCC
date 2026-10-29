import React from 'react';
import { View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { T, useTheme } from '../theme/theme';
import { Card, IconName } from './ui';
import { Link, UiDevice } from '../state/HubProvider';
import { HubStatus } from '../api/types';
import { formatUptime } from '../lib/format';

type Health = 'ok' | 'warn' | 'bad' | 'unknown';

export function uartHealth(s: HubStatus | null): { ratio: number | null; level: Health } {
  if (!s) return { ratio: null, level: 'unknown' };
  const ok = s.uart_success_responses;
  const bad = s.uart_error_responses + s.command_timeouts;
  const total = ok + bad;
  if (total === 0) return { ratio: null, level: 'unknown' };
  const ratio = ok / total;
  return { ratio, level: ratio >= 0.9 ? 'ok' : ratio >= 0.6 ? 'warn' : 'bad' };
}

/** Mostra a arquitetura distribuída: celular → hub (S3) → rádio (C6) → dispositivos Zigbee. */
export function HubPulse({ link, status, devices }: { link: Link; status: HubStatus | null; devices: UiDevice[] }) {
  const { t } = useTheme();
  const reach = link === 'online' || link === 'unauthorized';
  const uart = uartHealth(status);
  const zig = devices.filter((d) => d.protocol === 'zigbee');
  const zigOnline = zig.filter((d) => d.online).length;

  const color = (h: Health) => (h === 'ok' ? t.signal : h === 'warn' ? t.lampText : h === 'bad' ? t.danger : t.inkMuted);
  const phoneHealth: Health = reach ? 'ok' : link === 'connecting' ? 'unknown' : 'bad';
  const s3Health: Health = reach ? 'ok' : link === 'connecting' ? 'unknown' : 'bad';
  const c6Health: Health = !reach ? 'unknown' : uart.level;
  const zigHealth: Health = !reach || zig.length === 0 ? 'unknown' : zigOnline === zig.length ? 'ok' : zigOnline > 0 ? 'warn' : 'bad';

  const nodes: { icon: IconName; label: string; h: Health }[] = [
    { icon: 'phone-portrait-outline', label: 'Celular', h: phoneHealth },
    { icon: 'hardware-chip-outline', label: 'Hub S3', h: s3Health },
    { icon: 'radio-outline', label: 'Rádio C6', h: c6Health },
    { icon: 'bulb-outline', label: 'Zigbee', h: zigHealth },
  ];

  return (
    <Card style={{ padding: 18 }}>
      <View style={{ flexDirection: 'row', alignItems: 'flex-start' }}>
        {nodes.map((n, i) => (
          <React.Fragment key={n.label}>
            <View style={{ alignItems: 'center', width: 62 }}>
              <View style={{ width: 46, height: 46, borderRadius: 23, borderWidth: 2, borderColor: color(n.h), alignItems: 'center', justifyContent: 'center', backgroundColor: t.surface }}>
                <Ionicons name={n.icon} size={22} color={color(n.h)} />
              </View>
              <T v="small" w={600} c="inkSoft" style={{ marginTop: 6, fontSize: 12 }} numberOfLines={1}>{n.label}</T>
            </View>
            {i < nodes.length - 1 ? (
              <View style={{ flex: 1, height: 46, justifyContent: 'center' }}>
                <View style={{ height: 3, borderRadius: 2, backgroundColor: color(nodes[i + 1].h === 'unknown' || n.h === 'unknown' ? 'unknown' : nodes[i + 1].h === 'bad' || n.h === 'bad' ? 'bad' : 'ok'), opacity: 0.8 }} />
              </View>
            ) : null}
          </React.Fragment>
        ))}
      </View>
      <View style={{ flexDirection: 'row', marginTop: 18, gap: 8 }}>
        <Stat value={`${zigOnline}/${zig.length}`} label="Zigbee on-line" />
        <Stat value={status ? `${status.enabled_rules}/${status.configured_rules}` : '—'} label="Regras ativas" />
        <Stat value={uart.ratio === null ? '—' : `${Math.round(uart.ratio * 100)}%`} label="Enlace S3↔C6" />
      </View>
      <T v="small" c="inkMuted" w={400} style={{ marginTop: 14 }}>
        {status ? `Operando localmente, sem nuvem · ligado há ${formatUptime(status.uptime_seconds)}` : 'Aguardando resposta do hub…'}
      </T>
    </Card>
  );
}

function Stat({ value, label }: { value: string; label: string }) {
  const { t } = useTheme();
  return (
    <View style={{ flex: 1, backgroundColor: t.surfaceAlt, borderRadius: 16, padding: 12 }}>
      <T v="title" w={800}>{value}</T>
      <T v="small" c="inkSoft" w={400} style={{ fontSize: 12, lineHeight: 15 }}>{label}</T>
    </View>
  );
}
