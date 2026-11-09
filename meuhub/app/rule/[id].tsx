import React, { useEffect, useState } from 'react';
import { Alert, View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useLocalSearchParams, useRouter } from 'expo-router';
import {
  Banner, Button, Card, Chip, Field, Header, PickerModal, PresetPicker, Row, Screen, Segmented, SectionTitle,
} from '../../src/components/ui';
import { useToast } from '../../src/components/Toast';
import { T, useTheme } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';
import { errorMessage } from '../../src/api/client';
import { RuleDraft, RuleOperator } from '../../src/api/types';
import { boolTriggerIsOn, describeTimeWindow, OPERATORS, sampleValue } from '../../src/lib/rules';

interface ActionState { device: string; action: 'on' | 'off'; delay: number; duration: number }
const MAX_ACTIONS = 4; // MAX_RULE_ACTIONS no firmware
const MAX_RULES = 8;

export default function RuleEditor() {
  const { id, device: presetDevice, template } = useLocalSearchParams<{ id: string; device?: string; template?: string }>();
  const hub = useHub();
  const { t } = useTheme();
  const router = useRouter();
  const toast = useToast();

  const ruleId = id === 'new' ? undefined : Number(id);
  const existing = hub.rules.find((r) => r.id === ruleId);

  const [ready, setReady] = useState(false);
  const [name, setName] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [trigger, setTrigger] = useState('');
  const [kind, setKind] = useState<'bool' | 'number'>('bool');
  const [whenOn, setWhenOn] = useState(true);
  const [operator, setOperator] = useState<RuleOperator>('less_than');
  const [valueText, setValueText] = useState('200');
  const [eventName, setEventName] = useState('state_changed');
  const [startTime, setStartTime] = useState('');
  const [endTime, setEndTime] = useState('');
  const [actions, setActions] = useState<ActionState[]>([]);
  const [picker, setPicker] = useState<null | 'trigger' | number>(null);
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);

  const switches = hub.devices.filter((d) => d.type === 'on_off');
  const sensors = hub.devices.filter((d) => d.type !== 'on_off');

  const chooseTrigger = (devId: string) => {
    const d = hub.devices.find((x) => x.id === devId);
    setTrigger(devId);
    if (d?.type === 'on_off') { setKind('bool'); setEventName('state_changed'); }
    else { setKind('number'); setEventName(d?.event ?? 'luminosity'); }
  };

  // inicialização (regra existente, template ou em branco)
  useEffect(() => {
    if (ready) return;
    if (ruleId !== undefined) {
      if (!existing) return;
      setName(hub.meta.ruleNames[String(existing.id)] ?? '');
      setEnabled(existing.enabled);
      setTrigger(existing.trigger_device);
      setEventName(existing.trigger_event);
      if (typeof existing.trigger_value === 'boolean') {
        setKind('bool'); setWhenOn(boolTriggerIsOn(existing));
      } else {
        setKind('number'); setOperator(existing.operator); setValueText(String(existing.trigger_value));
      }
      setActions(existing.actions.map((a) => ({ device: a.device, action: a.action, delay: a.delay_seconds ?? 0, duration: a.duration_seconds ?? 0 })));
      setStartTime(existing.start_time ?? '');
      setEndTime(existing.end_time ?? '');
      setReady(true);
      return;
    }
    if (template === 'dark' && sensors[0] && switches[0]) {
      chooseTrigger(sensors[0].id);
      setOperator('less_than'); setValueText('200'); setName('Acender quando escurecer');
      setActions([{ device: switches[0].id, action: 'on', delay: 0, duration: 0 }]);
    } else if (template === 'timer' && switches[0]) {
      chooseTrigger(switches[0].id);
      setWhenOn(true); setName('Apagar sozinho');
      setActions([{ device: switches[0].id, action: 'off', delay: 600, duration: 0 }]);
    } else {
      if (presetDevice) chooseTrigger(presetDevice);
      setActions([{ device: switches[0]?.id ?? '', action: 'on', delay: 0, duration: 0 }]);
    }
    setReady(true);
  }, [ready, existing]); // eslint-disable-line react-hooks/exhaustive-deps

  const trigDev = hub.devices.find((d) => d.id === trigger);
  const numeric = Number(valueText.replace(',', '.'));

  const errors: string[] = [];
  if (!trigger) errors.push('Escolha o que dispara a automação.');
  if (kind === 'number' && (valueText.trim() === '' || Number.isNaN(numeric))) errors.push('Informe um valor numérico válido.');
  if (kind === 'number' && !eventName.trim()) errors.push('Informe o nome do evento do sensor.');
  const timePattern = /^([01]\d|2[0-3]):[0-5]\d$/;
  const hasTime = startTime !== '' || endTime !== '';
  if (hasTime && (!timePattern.test(startTime) || !timePattern.test(endTime))) {
    errors.push('Informe a janela no formato HH:MM, por exemplo 18:30.');
  }
  if (actions.length === 0 || actions.some((a) => !a.device)) errors.push('Escolha o dispositivo de cada ação.');
  if (ruleId === undefined && hub.rules.length >= MAX_RULES) errors.push('O hub já tem 8 regras. Apague uma antes de criar outra.');

  const buildDraft = (): RuleDraft => ({
    ...(ruleId !== undefined ? { id: ruleId } : {}),
    enabled,
    trigger_device: trigger,
    trigger_event: kind === 'bool' ? 'state_changed' : eventName.trim(),
    trigger_value: kind === 'bool' ? whenOn : numeric,
    operator: kind === 'bool' ? 'equals' : operator,
    actions: actions.map((a) => ({
      device: a.device, action: a.action,
      ...(a.delay > 0 ? { delay_seconds: a.delay } : {}),
      ...(a.duration > 0 ? { duration_seconds: a.duration } : {}),
    })),
    ...(startTime && endTime ? { start_time: startTime, end_time: endTime } : {}),
  });

  const save = async () => {
    if (errors.length) { toast.show(errors[0], 'error'); return; }
    setSaving(true);
    try {
      await hub.saveRule(buildDraft(), name);
      toast.show('Automação salva no hub', 'ok');
      router.back();
    } catch (e) { toast.show(errorMessage(e), 'error'); }
    setSaving(false);
  };

  const test = () => Alert.alert(
    'Testar automação?',
    'Vamos simular o gatilho no hub. As ações da versão salva dessa regra vão acontecer de verdade.',
    [
      { text: 'Cancelar', style: 'cancel' },
      {
        text: 'Testar',
        onPress: async () => {
          if (!hub.client || errors.length) { toast.show(errors[0] ?? 'Sem conexão', 'error'); return; }
          setTesting(true);
          try {
            const ev = kind === 'bool' ? 'state_changed' : eventName.trim();
            const sat = kind === 'bool' ? whenOn : sampleValue(operator, numeric, true);
            const unsat = kind === 'bool' ? !whenOn : sampleValue(operator, numeric, false);
            await hub.client.testEvent(trigger, ev, unsat); // "rearma" a regra
            const res = await hub.client.testEvent(trigger, ev, sat);
            toast.show(res.rule_matched ? 'Regra disparou' : 'Nenhuma regra correspondeu. Salve antes de testar.', res.rule_matched ? 'ok' : 'error');
            hub.refresh();
          } catch (e) { toast.show(errorMessage(e), 'error'); }
          setTesting(false);
        },
      },
    ],
  );

  const remove = () => Alert.alert('Apagar automação?', 'Ela será removida do hub.', [
    { text: 'Cancelar', style: 'cancel' },
    {
      text: 'Apagar', style: 'destructive',
      onPress: async () => {
        try { await hub.deleteRule(ruleId!); toast.show('Automação apagada', 'ok'); router.back(); }
        catch (e) { toast.show(errorMessage(e), 'error'); }
      },
    },
  ]);

  const patchAction = (i: number, p: Partial<ActionState>) => setActions((as) => as.map((a, j) => (j === i ? { ...a, ...p } : a)));

  const allDevices = hub.devices.map((d) => ({
    value: d.id, title: hub.displayName(d.id), subtitle: d.protocol === 'zigbee' ? 'Zigbee · liga/desliga' : 'Wi-Fi · sensor',
    icon: (d.type === 'on_off' ? 'bulb-outline' : 'sunny-outline') as any,
  }));
  const switchOptions = allDevices.filter((o) => switches.some((s) => s.id === o.value));

  return (
    <Screen footer={
      <>
        <Button label="Salvar automação" onPress={save} loading={saving} />
        {ruleId !== undefined ? <Button label="Testar agora" variant="soft" icon="flask-outline" onPress={test} loading={testing} /> : null}
      </>
    }>
      <Header title={ruleId === undefined ? 'Nova automação' : 'Editar automação'} back />

      <Field label="Nome (opcional)" value={name} onChangeText={setName} placeholder="Ex.: Luz da sala ao anoitecer" autoCapitalize="sentences"
        hint="O hub não guarda nomes, então ele fica só neste celular." />

      <SectionTitle title="Quando" />
      <Card>
        <Row
          icon={kind === 'bool' ? 'bulb-outline' : 'sunny-outline'}
          title={trigDev ? hub.displayName(trigDev.id) : trigger ? hub.displayName(trigger) : 'Escolher dispositivo'}
          subtitle={trigDev ? (trigDev.type === 'on_off' ? 'Liga/desliga' : 'Sensor') : 'Toque para escolher'}
          onPress={() => setPicker('trigger')} last
        />
        {kind === 'bool' ? (
          <View style={{ marginTop: 12 }}>
            <Segmented value={whenOn ? 'on' : 'off'} onChange={(v) => setWhenOn(v === 'on')}
              options={[{ value: 'on', label: 'Ligar' }, { value: 'off', label: 'Desligar' }]} />
          </View>
        ) : (
          <View style={{ marginTop: 12 }}>
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginBottom: 16 }}>
              {OPERATORS.map((o) => <Chip key={o.value} label={o.label} active={o.value === operator} onPress={() => setOperator(o.value)} />)}
            </View>
            <Field label={`Valor${trigDev?.unit ? ` (${trigDev.unit})` : ''}`} value={valueText} onChangeText={setValueText} keyboardType="numbers-and-punctuation" />
            <Field label="Nome do evento do sensor" value={eventName} onChangeText={setEventName}
              hint="Precisa ser igual ao campo “event” que o sensor envia ao hub." />
          </View>
        )}
      </Card>
      {hub.config?.time_synced !== undefined ? (
        <>
          <SectionTitle title="Janela de horário (opcional)" />
          <Card>
            <T v="small" c="inkSoft" w={400} style={{ marginBottom: 12 }}>
              A regra só será avaliada dentro deste intervalo. Use HH:MM; horários que atravessam a meia-noite são aceitos.
            </T>
            <Field label="Início" value={startTime} onChangeText={setStartTime} placeholder="18:30" keyboardType="numbers-and-punctuation" />
            <Field label="Fim" value={endTime} onChangeText={setEndTime} placeholder="23:00" keyboardType="numbers-and-punctuation" />
            {startTime && endTime && !errors.some((e) => e.includes('janela')) ? (
              <T v="small" c="signal" w={600}>Ativa {describeTimeWindow({ start_time: startTime, end_time: endTime })}</T>
            ) : null}
          </Card>
        </>
      ) : (
        <T v="small" c="inkMuted" w={400} style={{ marginTop: 16 }}>
          Janelas de horário ainda não estão disponíveis neste firmware do hub.
        </T>
      )}
      <T v="small" c="inkMuted" w={400} style={{ marginTop: 8 }}>
        A regra dispara quando a condição passa a valer e só dispara de novo depois que ela deixar de valer.
      </T>

      <SectionTitle title="Então" />
      <View style={{ gap: 12 }}>
        {actions.map((a, i) => (
          <Card key={i}>
            <Row icon="bulb-outline" title={a.device ? hub.displayName(a.device) : 'Escolher dispositivo'} subtitle="Toque para trocar" onPress={() => setPicker(i)} last />
            <View style={{ marginVertical: 12 }}>
              <Segmented value={a.action} onChange={(v) => patchAction(i, { action: v })}
                options={[{ value: 'on', label: 'Ligar' }, { value: 'off', label: 'Desligar' }]} />
            </View>
            <T v="small" c="inkSoft" style={{ marginBottom: 8 }}>Esperar antes de executar</T>
            <PresetPicker value={a.delay} presets={[0, 5, 30, 60, 300, 600, 1800]} onChange={(v) => patchAction(i, { delay: v })} />
            <T v="small" c="inkSoft" style={{ marginTop: 14, marginBottom: 8 }}>Desfazer depois de</T>
            <PresetPicker value={a.duration} presets={[0, 10, 60, 300, 600, 1800, 3600]} zeroLabel="Nunca" onChange={(v) => patchAction(i, { duration: v })} />
            {actions.length > 1 ? (
              <View style={{ marginTop: 14, alignSelf: 'flex-start' }}>
                <Button label="Remover ação" variant="ghost" small icon="close" onPress={() => setActions((as) => as.filter((_, j) => j !== i))} />
              </View>
            ) : null}
          </Card>
        ))}
        {actions.length < MAX_ACTIONS ? (
          <Button label="Adicionar ação" variant="soft" icon="add"
            onPress={() => setActions((as) => [...as, { device: switches[0]?.id ?? '', action: 'on', delay: 0, duration: 0 }])} />
        ) : null}
      </View>

      {errors.length ? <View style={{ marginTop: 20 }}><Banner tone="warn" icon="alert-circle-outline" title="Falta ajustar" body={errors.join('\n')} /></View> : null}
      {ruleId !== undefined ? (
        <View style={{ marginTop: 24 }}><Button label="Apagar automação" variant="ghost" icon="trash-outline" onPress={remove} /></View>
      ) : null}

      <PickerModal
        visible={picker === 'trigger'} title="O que dispara?" value={trigger}
        options={allDevices} onSelect={chooseTrigger} onClose={() => setPicker(null)}
      />
      <PickerModal
        visible={typeof picker === 'number'} title="Qual dispositivo?" value={typeof picker === 'number' ? actions[picker]?.device : undefined}
        options={switchOptions} onSelect={(v) => typeof picker === 'number' && patchAction(picker, { device: v })} onClose={() => setPicker(null)}
      />
    </Screen>
  );
}
