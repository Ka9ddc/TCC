import React, { useState } from 'react';
import { Alert, Pressable, Share, View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useLocalSearchParams, useRouter } from 'expo-router';
import { Button, Card, Dot, Empty, Field, Header, Row, Screen, SectionTitle } from '../../src/components/ui';
import { useToast } from '../../src/components/Toast';
import { T, useTheme } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';
import { errorMessage } from '../../src/api/client';
import { hex16, num } from '../../src/lib/format';
import { describeAction, describeTrigger } from '../../src/lib/rules';

export default function DeviceDetail() {
  const { id } = useLocalSearchParams<{ id: string }>();
  const hub = useHub();
  const { t } = useTheme();
  const router = useRouter();
  const toast = useToast();
  const device = hub.devices.find((d) => d.id === id);
  const [name, setName] = useState(hub.meta.aliases[id] ?? '');
  const [busy, setBusy] = useState(false);

  if (!device) {
    return (
      <Screen>
        <Header title="Dispositivo" back />
        <Empty icon="help-circle-outline" title="Dispositivo não encontrado" body="Ele pode ter sido removido do hub." />
      </Screen>
    );
  }

  const isSwitch = device.type === 'on_off';
  const on = device.state === true && device.online;
  const related = hub.rules.filter((r) => r.trigger_device === id || r.actions.some((a) => a.device === id));
  const unitOf = (dev: string) => hub.devices.find((d) => d.id === dev)?.unit;

  const toggle = async () => {
    setBusy(true);
    try { await hub.toggleDevice(id); } catch (e) { toast.show(errorMessage(e), 'error'); }
    setBusy(false);
  };

  const saveName = () => {
    hub.updateMeta((m) => {
      const aliases = { ...m.aliases };
      if (name.trim()) aliases[id] = name.trim(); else delete aliases[id];
      return { ...m, aliases };
    });
    toast.show('Nome salvo neste celular', 'ok');
  };

  const remove = () =>
    Alert.alert(
      'Remover do hub?',
      'O dispositivo sai da rede Zigbee e precisará ser pareado de novo. As automações que o usam deixarão de funcionar.',
      [
        { text: 'Cancelar', style: 'cancel' },
        {
          text: 'Remover', style: 'destructive',
          onPress: async () => {
            try { await hub.removeDevice(id); toast.show('Remoção enviada ao dispositivo', 'ok'); router.back(); }
            catch (e) { toast.show(errorMessage(e), 'error'); }
          },
        },
      ],
    );

  return (
    <Screen>
      <Header title={hub.displayName(id)} subtitle={`${device.protocol === 'zigbee' ? 'Zigbee' : 'Wi-Fi'} · ${device.online ? 'on-line' : 'sem sinal'}`} back />

      {isSwitch ? (
        <View style={{ alignItems: 'center', marginVertical: 12 }}>
          <Pressable
            onPress={toggle} disabled={busy || !device.online}
            style={[{
              width: 132, height: 132, borderRadius: 66, alignItems: 'center', justifyContent: 'center',
              backgroundColor: on ? t.lamp : t.surface, borderWidth: 1.5, borderColor: on ? t.lamp : t.line, opacity: device.online ? 1 : 0.5,
            }, on ? { shadowColor: t.lamp, shadowOpacity: 0.6, shadowRadius: 28, shadowOffset: { width: 0, height: 10 }, elevation: 14 } : null]}
          >
            <Ionicons name="power" size={52} color={on ? t.lampInk : t.inkMuted} />
          </Pressable>
          <T v="h3" style={{ marginTop: 16 }}>
            {!device.online ? 'Sem sinal' : device.state === undefined ? 'Estado desconhecido' : on ? 'Ligado' : 'Desligado'}
          </T>
          {device.estimated ? <T v="small" c="inkMuted" w={400}>Estado estimado pelo último comando enviado</T> : null}
        </View>
      ) : (
        <View style={{ alignItems: 'center', marginVertical: 12 }}>
          <T v="num">{device.value !== undefined ? num(device.value) : '—'}</T>
          <T v="h3" c="inkSoft">{device.unit ?? ''}</T>
        </View>
      )}

      <Card style={{ marginTop: 16 }}>
        <Field label="Nome neste celular" value={name} onChangeText={setName} placeholder={hub.displayName(id)} autoCapitalize="sentences"
          hint="O nome fica só neste celular; o hub continua usando o identificador." />
        <Button label="Salvar nome" variant="soft" small onPress={saveName} />
      </Card>

      <SectionTitle title="Informações" />
      <Card style={{ paddingVertical: 4 }}>
        <Row title="Identificador" subtitle={device.id} />
        <Row title="Tipo" subtitle={isSwitch ? 'Liga/desliga (On/Off)' : 'Sensor de luminosidade'} />
        <Row title="Conexão" subtitle={device.online ? 'On-line' : 'Sem sinal'} right={<Dot on={device.online} />} last={device.protocol !== 'zigbee'} />
        {device.protocol === 'zigbee' ? (
          <>
            <Row title="Endereço curto" subtitle={hex16(device.short_address)} />
            <Row title="Endpoint" subtitle={String(device.endpoint ?? '—')} />
            <Row title="Endereço IEEE" subtitle={device.ieee_address || '—'} last
              right={<Pressable hitSlop={10} onPress={() => Share.share({ message: device.ieee_address ?? '' })}><Ionicons name="share-outline" size={20} color={t.inkMuted} /></Pressable>} />
          </>
        ) : null}
      </Card>

      <SectionTitle title="Automações que usam este dispositivo" />
      {related.length === 0 ? (
        <Card><T v="body" c="inkSoft" style={{ marginBottom: 12 }}>Nenhuma ainda.</T>
          <Button label="Criar automação" small variant="soft" icon="add" onPress={() => router.push({ pathname: '/rule/new', params: { device: id } })} /></Card>
      ) : (
        <Card style={{ paddingVertical: 4 }}>
          {related.map((r, i) => (
            <Row key={r.id} icon="git-network-outline" title={hub.ruleName(r)}
              subtitle={`Quando ${describeTrigger(r, hub.displayName, unitOf(r.trigger_device))} → ${r.actions.map((a) => describeAction(a, hub.displayName)).join(', ')}`}
              onPress={() => router.push(`/rule/${r.id}`)} last={i === related.length - 1} />
          ))}
        </Card>
      )}

      {device.protocol === 'zigbee' ? (
        <View style={{ marginTop: 28 }}>
          <Button label="Remover do hub" variant="danger" icon="trash-outline" onPress={remove} />
        </View>
      ) : null}
    </Screen>
  );
}
