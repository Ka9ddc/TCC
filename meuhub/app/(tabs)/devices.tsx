import React, { useState } from 'react';
import { View } from 'react-native';
import { useRouter } from 'expo-router';
import { Button, Empty, Header, IconButton, Screen, Segmented } from '../../src/components/ui';
import { DeviceTile, SensorCard } from '../../src/components/DeviceTile';
import { useToast } from '../../src/components/Toast';
import { T } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';
import { errorMessage } from '../../src/api/client';

type Filter = 'all' | 'zigbee' | 'wifi';
const MAX_ZIGBEE = 8; // MAX_ZIGBEE_DEVICES no firmware

export default function Devices() {
  const hub = useHub();
  const router = useRouter();
  const toast = useToast();
  const [filter, setFilter] = useState<Filter>('all');
  const [refreshing, setRefreshing] = useState(false);

  const list = hub.devices.filter((d) => filter === 'all' || d.protocol === filter);
  const onoff = list.filter((d) => d.type === 'on_off');
  const sensors = list.filter((d) => d.type !== 'on_off');
  const zigbeeCount = hub.devices.filter((d) => d.protocol === 'zigbee').length;

  const toggle = (id: string) => async () => {
    try { await hub.toggleDevice(id); } catch (e) { toast.show(errorMessage(e), 'error'); }
  };

  return (
    <Screen
      tab refreshing={refreshing}
      onRefresh={async () => { setRefreshing(true); await hub.refresh(); setRefreshing(false); }}
    >
      <Header
        title="Dispositivos"
        subtitle={`${zigbeeCount} de ${MAX_ZIGBEE} vagas Zigbee em uso`}
        right={<IconButton icon="add" label="Adicionar dispositivo" onPress={() => router.push('/pairing')} />}
      />
      <Segmented<Filter>
        value={filter} onChange={setFilter}
        options={[{ value: 'all', label: 'Todos' }, { value: 'zigbee', label: 'Zigbee' }, { value: 'wifi', label: 'Wi-Fi' }]}
      />
      <View style={{ height: 20 }} />

      {list.length === 0 ? (
        <Empty
          icon="bulb-outline" title="Nada por aqui"
          body={filter === 'wifi' ? 'Sensores Wi-Fi aparecem sozinhos quando enviam o primeiro evento para o hub.' : 'Abra a rede Zigbee para parear uma lâmpada ou interruptor.'}
          action={filter !== 'wifi' ? <Button label="Adicionar dispositivo" small icon="add" onPress={() => router.push('/pairing')} /> : undefined}
        />
      ) : null}

      <View style={{ gap: 12 }}>
        {chunk(onoff, 2).map((pair, i) => (
          <View key={i} style={{ flexDirection: 'row', gap: 12 }}>
            {pair.map((d) => (
              <DeviceTile key={d.id} device={d} name={hub.displayName(d.id)} onToggle={toggle(d.id)} onOpen={() => router.push(`/device/${d.id}`)} />
            ))}
            {pair.length === 1 ? <View style={{ flex: 1 }} /> : null}
          </View>
        ))}
        {sensors.length ? <T v="h3" style={{ marginTop: 12 }}>Sensores</T> : null}
        {sensors.map((d) => (
          <SensorCard key={d.id} device={d} name={hub.displayName(d.id)} onOpen={() => router.push(`/device/${d.id}`)} />
        ))}
      </View>
      {onoff.length ? <T v="small" c="inkMuted" w={400} style={{ marginTop: 16 }}>Toque para ligar ou desligar. Segure para ver detalhes.</T> : null}
    </Screen>
  );
}

function chunk<A>(arr: A[], n: number): A[][] {
  const out: A[][] = [];
  for (let i = 0; i < arr.length; i += n) out.push(arr.slice(i, i + n));
  return out;
}
