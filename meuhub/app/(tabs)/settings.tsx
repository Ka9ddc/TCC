import React from 'react';
import { Alert, View } from 'react-native';
import { useRouter } from 'expo-router';
import { Banner, Card, Dot, Header, Row, Screen, Segmented, SectionTitle } from '../../src/components/ui';
import { T } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';
import { formatUptime } from '../../src/lib/format';

export default function Settings() {
  const hub = useHub();
  const router = useRouter();
  const { config, status, connection, meta } = hub;

  const leave = () => Alert.alert('Sair deste hub?', 'O endereço e o token salvos neste celular serão apagados. Nomes e cenas continuam salvos.', [
    { text: 'Cancelar', style: 'cancel' },
    { text: 'Sair', style: 'destructive', onPress: () => hub.disconnect() },
  ]);

  return (
    <Screen tab>
      <Header title="Ajustes" />

      <Card style={{ paddingVertical: 4 }}>
        <Row icon="hardware-chip-outline" title="Hub" subtitle={connection?.host.replace(/^http:\/\//, '')}
          right={<View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}><Dot on={hub.link === 'online'} /><T v="small" c="inkSoft">{hub.link === 'online' ? 'On-line' : 'Sem conexão'}</T></View>} />
        <Row icon="home-outline" title="Rede doméstica (STA)" subtitle={config ? config.sta_ssid : '—'}
          right={config ? <T v="small" c={config.sta_connected ? 'signal' : 'danger'} w={700}>{config.sta_connected ? 'Conectado' : 'Desconectado'}</T> : undefined} />
        <Row icon="wifi" title="Rede do hub (AP)" subtitle={config ? `${config.ap_ssid} · ${config.ap_connected_clients} conectado(s)` : '—'} />
        <Row icon="time-outline" title="Ligado há" subtitle={status ? formatUptime(status.uptime_seconds) : '—'} last />
      </Card>
      {config?.restart_required ? <View style={{ marginTop: 12 }}><Banner tone="warn" icon="refresh-circle-outline" title="Reinício pendente" body="Desligue e ligue o hub para aplicar a nova configuração de Wi-Fi." /></View> : null}

      <SectionTitle title="Configuração" />
      <Card style={{ paddingVertical: 4 }}>
        <Row icon="wifi" title="Wi-Fi do hub" subtitle="Rede doméstica e rede própria do hub" onPress={() => router.push('/wifi')} />
        <Row icon="shield-checkmark-outline" title="Segurança" subtitle="Token de acesso à API" onPress={() => router.push('/security')} last />
      </Card>

      <SectionTitle title="Cenas" />
      <Card style={{ paddingVertical: 4 }}>
        {meta.scenes.map((s) => (
          <Row key={s.id} icon={s.icon as any} title={s.name} subtitle={`${s.steps.length} ação(ões)`} onPress={() => router.push(`/scene/${s.id}`)} />
        ))}
        <Row icon="add-circle-outline" title="Nova cena" onPress={() => router.push('/scene/new')} last />
      </Card>

      <SectionTitle title="Ferramentas" />
      <Card style={{ paddingVertical: 4 }}>
        <Row icon="list-outline" title="Atividade" subtitle="Eventos e comandos recentes" onPress={() => router.push('/activity')} />
        <Row icon="pulse" title="Diagnóstico" subtitle="Contadores, enlace S3↔C6 e simulador de eventos" onPress={() => router.push('/diagnostics')} last />
      </Card>

      <SectionTitle title="Aparência" />
      <Segmented value={meta.themeMode} onChange={(v) => hub.updateMeta((m) => ({ ...m, themeMode: v }))}
        options={[{ value: 'auto', label: 'Automático' }, { value: 'light', label: 'Claro' }, { value: 'dark', label: 'Escuro' }]} />

      <SectionTitle title="Conexão" />
      <Card style={{ paddingVertical: 4 }}>
        <Row icon="swap-horizontal-outline" title="Trocar endereço ou token" onPress={() => router.push({ pathname: '/connect', params: { edit: '1' } })} />
        <Row icon="log-out-outline" title="Sair deste hub" danger onPress={leave} last />
      </Card>

      <T v="small" c="inkMuted" w={400} style={{ marginTop: 24, textAlign: 'center' }}>
        MeuHub · TCC de Automação Industrial{'\n'}Protocolo UART v1 · API local sem nuvem
      </T>
    </Screen>
  );
}
