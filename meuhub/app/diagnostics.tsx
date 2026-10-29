import React, { useState } from 'react';
import { Share, View } from 'react-native';
import { Banner, Button, Card, Chip, Field, Header, Row, Screen, SectionTitle } from '../src/components/ui';
import { uartHealth } from '../src/components/HubPulse';
import { useToast } from '../src/components/Toast';
import { T, useTheme } from '../src/theme/theme';
import { useHub } from '../src/state/HubProvider';
import { errorMessage } from '../src/api/client';
import { formatUptime } from '../src/lib/format';

type ValueKind = 'on' | 'off' | 'number';

export default function Diagnostics() {
  const hub = useHub();
  const { t } = useTheme();
  const toast = useToast();
  const s = hub.status;
  const uart = uartHealth(s);
  const [device, setDevice] = useState(hub.devices[0]?.id ?? '');
  const [event, setEvent] = useState('state_changed');
  const [kind, setKind] = useState<ValueKind>('on');
  const [value, setValue] = useState('0');
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<string | null>(null);

  const send = async () => {
    if (!device.trim() || !event.trim()) return toast.show('Informe dispositivo e evento', 'error');
    const v = kind === 'number' ? Number(value.replace(',', '.')) : kind === 'on';
    if (typeof v === 'number' && Number.isNaN(v)) return toast.show('Valor numérico inválido', 'error');
    setBusy(true);
    try {
      const r = await hub.client!.testEvent(device.trim(), event.trim(), v);
      setResult(r.rule_matched ? 'Uma ou mais regras dispararam.' : 'Evento processado, nenhuma regra correspondeu.');
      hub.refresh();
    } catch (e) { setResult(errorMessage(e)); }
    setBusy(false);
  };

  const stat = (title: string, v: number | string | undefined, warn = false) => (
    <View style={{ width: '48%', backgroundColor: t.surface, borderRadius: 18, borderWidth: 1, borderColor: warn ? t.danger : t.line, padding: 14 }}>
      <T v="title" w={800} c={warn ? 'danger' : 'ink'}>{v ?? '—'}</T>
      <T v="small" c="inkSoft" w={400} style={{ lineHeight: 16 }}>{title}</T>
    </View>
  );

  return (
    <Screen>
      <Header title="Diagnóstico" subtitle="Saúde do hub e do enlace UART entre S3 e C6." back />

      {!hub.historyAvailable ? (
        <Banner tone="warn" icon="alert-circle-outline" title="/api/history não respondeu"
          body="O firmware registra 16 rotas HTTP, mas configura max_uri_handlers = 15. A última registrada (history) fica de fora. Aumente o limite para 20 e grave de novo." />
      ) : null}
      {hub.link === 'unauthorized' ? <Banner tone="danger" icon="key-outline" title="Rotas protegidas recusaram o token" /> : null}

      <Card style={{ marginBottom: 16 }}>
        <T v="small" c="inkSoft">Enlace S3 ↔ C6</T>
        <T v="num">{uart.ratio === null ? '—' : `${Math.round(uart.ratio * 100)}%`}</T>
        <T v="small" c="inkMuted" w={400}>
          {uart.ratio === null ? 'Ainda sem comandos para medir.' : 'Respostas com sucesso entre todas as respostas e timeouts.'}
        </T>
      </Card>

      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 12, justifyContent: 'space-between' }}>
        {stat('Tempo ligado', s ? formatUptime(s.uptime_seconds) : undefined)}
        {stat('Eventos recebidos', s?.events_received)}
        {stat('Regras disparadas', s?.rules_matched)}
        {stat('Comandos enviados', s?.automation_commands)}
        {stat('Respostas OK do C6', s?.uart_success_responses)}
        {stat('Respostas com erro', s?.uart_error_responses, (s?.uart_error_responses ?? 0) > 0)}
        {stat('Timeouts de comando', s?.command_timeouts, (s?.command_timeouts ?? 0) > 0)}
        {stat('Comandos pendentes', s?.pending_commands)}
        {stat('Mensagens UART inválidas', s?.uart_invalid_messages, (s?.uart_invalid_messages ?? 0) > 0)}
        {stat('Respostas sem pedido', s?.uart_unknown_responses)}
        {stat('Respostas duplicadas', s?.uart_duplicate_responses)}
        {stat('Registros no histórico', s?.history_entries)}
      </View>

      <SectionTitle title="Simulador de eventos" />
      <Card>
        <Field label="Dispositivo" value={device} onChangeText={setDevice} />
        {hub.devices.length ? (
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginTop: -6, marginBottom: 16 }}>
            {hub.devices.map((d) => <Chip key={d.id} label={hub.displayName(d.id)} active={d.id === device}
              onPress={() => { setDevice(d.id); setEvent(d.type === 'on_off' ? 'state_changed' : d.event ?? 'luminosity'); setKind(d.type === 'on_off' ? 'on' : 'number'); }} />)}
          </View>
        ) : null}
        <Field label="Evento" value={event} onChangeText={setEvent} />
        <View style={{ flexDirection: 'row', gap: 8, marginBottom: 16 }}>
          <Chip label="Ligado" active={kind === 'on'} onPress={() => setKind('on')} />
          <Chip label="Desligado" active={kind === 'off'} onPress={() => setKind('off')} />
          <Chip label="Número" active={kind === 'number'} onPress={() => setKind('number')} />
        </View>
        {kind === 'number' ? <Field label="Valor" value={value} onChangeText={setValue} keyboardType="numbers-and-punctuation" /> : null}
        <Button label="Enviar evento de teste" icon="flask-outline" onPress={send} loading={busy} />
        {result ? <T v="small" c="inkSoft" w={500} style={{ marginTop: 12 }}>{result}</T> : null}
      </Card>
      <T v="small" c="inkMuted" w={400} style={{ marginTop: 8 }}>O evento passa pelo motor de regras de verdade: se uma regra casar, os dispositivos serão acionados.</T>

      <View style={{ marginTop: 24 }}>
        <Button label="Compartilhar status (JSON)" variant="soft" icon="share-outline" disabled={!s} onPress={() => Share.share({ message: JSON.stringify(s, null, 2) })} />
      </View>
    </Screen>
  );
}
