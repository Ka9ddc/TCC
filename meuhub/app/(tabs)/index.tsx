import React, { useState } from 'react';
import { Pressable, ScrollView, View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useRouter } from 'expo-router';
import { Banner, Button, Card, Empty, Header, IconButton, Row, Screen, SectionTitle } from '../../src/components/ui';
import { DeviceTile, SensorCard } from '../../src/components/DeviceTile';
import { HubPulse } from '../../src/components/HubPulse';
import { useToast } from '../../src/components/Toast';
import { T, useTheme } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';
import { errorMessage } from '../../src/api/client';
import { agoFromUptime } from '../../src/lib/format';
import { describeHistory } from '../../src/lib/history';

export default function Home() {
  const hub = useHub();
  const { t } = useTheme();
  const router = useRouter();
  const toast = useToast();
  const [refreshing, setRefreshing] = useState(false);
  const [running, setRunning] = useState<string | null>(null);

  const onoff = hub.devices.filter((d) => d.type === 'on_off');
  const sensors = hub.devices.filter((d) => d.type !== 'on_off');
  const uptime = hub.status?.uptime_seconds ?? 0;
  const anyOn = onoff.some((d) => d.state);

  const onRefresh = async () => { setRefreshing(true); await hub.refresh(); setRefreshing(false); };

  const toggle = (id: string) => async () => {
    try { await hub.toggleDevice(id); } catch (e) { toast.show(errorMessage(e), 'error'); }
  };

  const run = async (key: string, steps: { device: string; action: 'on' | 'off' }[], label: string) => {
    if (running) return;
    setRunning(key);
    const r = await hub.runScene({ steps });
    setRunning(null);
    if (r.fail === 0) toast.show(`${label} executada`, 'ok');
    else toast.show(`${label}: ${r.fail} de ${r.ok + r.fail} comandos falharam`, 'error');
  };

  return (
    <Screen tab refreshing={refreshing} onRefresh={onRefresh}>
      <Header
        title="Minha casa"
        subtitle={hub.link === 'online' ? 'Hub conectado' : hub.link === 'connecting' ? 'Conectando…' : 'Hub indisponível'}
        right={<IconButton icon="refresh" label="Atualizar" onPress={onRefresh} />}
      />

      <HubPulse link={hub.link} status={hub.status} devices={hub.devices} />

      <SectionTitle title="Cenas" />
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10, paddingRight: 20 }} style={{ marginHorizontal: -20, paddingHorizontal: 20 }}>
        <SceneChip
          icon="power" label="Desligar tudo" busy={running === 'all-off'} dim={!anyOn}
          onPress={() => run('all-off', onoff.map((d) => ({ device: d.id, action: 'off' as const })), 'Desligar tudo')}
        />
        {hub.meta.scenes.map((s) => (
          <SceneChip
            key={s.id} icon={s.icon as any} label={s.name} busy={running === s.id}
            onPress={() => run(s.id, s.steps, s.name)}
            onLongPress={() => router.push(`/scene/${s.id}`)}
          />
        ))}
        <SceneChip icon="add" label="Nova cena" ghost onPress={() => router.push('/scene/new')} />
      </ScrollView>

      <SectionTitle title="Dispositivos" action={hub.devices.length ? 'Ver todos' : undefined} onAction={() => router.push('/devices')} />
      {hub.devices.length === 0 ? (
        <Empty
          icon="bulb-outline" title="Nenhum dispositivo ainda"
          body="Abra a rede Zigbee e coloque a lâmpada ou o interruptor em modo de pareamento."
          action={<Button label="Adicionar dispositivo" small icon="add" onPress={() => router.push('/pairing')} />}
        />
      ) : (
        <View style={{ gap: 12 }}>
          {chunk(onoff.slice(0, 4), 2).map((pair, i) => (
            <View key={i} style={{ flexDirection: 'row', gap: 12 }}>
              {pair.map((d) => (
                <DeviceTile key={d.id} device={d} name={hub.displayName(d.id)} onToggle={toggle(d.id)} onOpen={() => router.push(`/device/${d.id}`)} />
              ))}
              {pair.length === 1 ? <View style={{ flex: 1 }} /> : null}
            </View>
          ))}
          {sensors.map((d) => (
            <SensorCard key={d.id} device={d} name={hub.displayName(d.id)} onOpen={() => router.push(`/device/${d.id}`)} />
          ))}
        </View>
      )}

      <SectionTitle title="Atividade recente" action={hub.history.length ? 'Ver tudo' : undefined} onAction={() => router.push('/activity')} />
      {!hub.historyAvailable ? (
        <Banner tone="warn" icon="alert-circle-outline" title="Histórico indisponível" body="A rota /api/history não respondeu. Veja Ajustes › Diagnóstico." />
      ) : hub.history.length === 0 ? (
        <T v="body" c="inkSoft">Nada aconteceu desde que o hub ligou.</T>
      ) : (
        <Card style={{ paddingVertical: 4 }}>
          {[...hub.history].reverse().slice(0, 4).map((h, i, arr) => {
            const d = describeHistory(h, hub.displayName);
            return <Row key={i} icon={d.icon} title={d.title} subtitle={agoFromUptime(h.uptime_seconds, uptime)} last={i === arr.length - 1} />;
          })}
        </Card>
      )}
    </Screen>
  );
}

function SceneChip({ icon, label, onPress, onLongPress, busy, ghost, dim }: {
  icon: any; label: string; onPress: () => void; onLongPress?: () => void; busy?: boolean; ghost?: boolean; dim?: boolean;
}) {
  const { t } = useTheme();
  return (
    <Pressable
      onPress={onPress} onLongPress={onLongPress}
      style={({ pressed }) => ({
        height: 48, paddingHorizontal: 18, borderRadius: 24, flexDirection: 'row', alignItems: 'center', gap: 8,
        backgroundColor: ghost ? 'transparent' : t.surface, borderWidth: 1.5, borderStyle: ghost ? 'dashed' : 'solid', borderColor: t.line,
        opacity: pressed || busy ? 0.6 : dim ? 0.55 : 1,
      })}
    >
      <Ionicons name={busy ? 'hourglass-outline' : icon} size={18} color={t.ink} />
      <T v="small" w={700}>{label}</T>
    </Pressable>
  );
}

function chunk<A>(arr: A[], n: number): A[][] {
  const out: A[][] = [];
  for (let i = 0; i < arr.length; i += n) out.push(arr.slice(i, i + n));
  return out;
}
