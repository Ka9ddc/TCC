import React, { useEffect, useState } from 'react';
import { Alert, Pressable, View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useLocalSearchParams, useRouter } from 'expo-router';
import { Button, Card, Empty, Field, Header, Screen, Segmented } from '../../src/components/ui';
import { useToast } from '../../src/components/Toast';
import { T, useTheme } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';

const ICONS = ['moon-outline', 'sunny-outline', 'home-outline', 'film-outline', 'restaurant-outline', 'bed-outline', 'flash-outline', 'sparkles-outline'];
type Step = 'skip' | 'on' | 'off';

export default function SceneEditor() {
  const { id } = useLocalSearchParams<{ id: string }>();
  const hub = useHub();
  const { t } = useTheme();
  const router = useRouter();
  const toast = useToast();
  const existing = hub.meta.scenes.find((s) => s.id === id);
  const switches = hub.devices.filter((d) => d.type === 'on_off');

  const [name, setName] = useState(existing?.name ?? '');
  const [icon, setIcon] = useState(existing?.icon ?? ICONS[0]);
  const [steps, setSteps] = useState<Record<string, Step>>({});

  useEffect(() => {
    const init: Record<string, Step> = {};
    for (const d of switches) init[d.id] = existing?.steps.find((s) => s.device === d.id)?.action ?? 'skip';
    setSteps(init);
  }, [hub.devices.length]); // eslint-disable-line react-hooks/exhaustive-deps

  const save = () => {
    const list = Object.entries(steps).filter(([, v]) => v !== 'skip').map(([device, v]) => ({ device, action: v as 'on' | 'off' }));
    if (!name.trim()) return toast.show('Dê um nome para a cena', 'error');
    if (list.length === 0) return toast.show('Escolha ao menos uma ação', 'error');
    const scene = { id: existing?.id ?? String(Date.now()), name: name.trim(), icon, steps: list };
    hub.updateMeta((m) => ({ ...m, scenes: existing ? m.scenes.map((s) => (s.id === scene.id ? scene : s)) : [...m.scenes, scene] }));
    toast.show('Cena salva neste celular', 'ok');
    router.back();
  };

  const remove = () => Alert.alert('Apagar cena?', undefined, [
    { text: 'Cancelar', style: 'cancel' },
    { text: 'Apagar', style: 'destructive', onPress: () => { hub.updateMeta((m) => ({ ...m, scenes: m.scenes.filter((s) => s.id !== id) })); router.back(); } },
  ]);

  return (
    <Screen footer={<Button label="Salvar cena" onPress={save} />}>
      <Header title={existing ? 'Editar cena' : 'Nova cena'} subtitle="Uma cena envia vários comandos de uma vez, tocando em um botão." back />
      <Field label="Nome" value={name} onChangeText={setName} placeholder="Boa noite" autoCapitalize="sentences" />

      <T v="small" c="inkSoft" style={{ marginBottom: 8 }}>Ícone</T>
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 10, marginBottom: 24 }}>
        {ICONS.map((ic) => (
          <Pressable key={ic} onPress={() => setIcon(ic)}
            style={{ width: 48, height: 48, borderRadius: 24, alignItems: 'center', justifyContent: 'center', backgroundColor: ic === icon ? t.ink : t.surface, borderWidth: 1, borderColor: ic === icon ? t.ink : t.line }}>
            <Ionicons name={ic as any} size={22} color={ic === icon ? t.onInk : t.ink} />
          </Pressable>
        ))}
      </View>

      <T v="title" style={{ marginBottom: 12 }}>O que fazer</T>
      {switches.length === 0 ? (
        <Empty icon="bulb-outline" title="Sem dispositivos" body="Adicione um dispositivo Zigbee para montar cenas." />
      ) : (
        <View style={{ gap: 12 }}>
          {switches.map((d) => (
            <Card key={d.id}>
              <T v="h3" style={{ marginBottom: 10 }}>{hub.displayName(d.id)}</T>
              <Segmented<Step> value={steps[d.id] ?? 'skip'} onChange={(v) => setSteps((s) => ({ ...s, [d.id]: v }))}
                options={[{ value: 'skip', label: 'Manter' }, { value: 'on', label: 'Ligar' }, { value: 'off', label: 'Desligar' }]} />
            </Card>
          ))}
        </View>
      )}
      <T v="small" c="inkMuted" w={400} style={{ marginTop: 14 }}>
        Cenas rodam a partir do app. Elas não funcionam com o celular desligado; para isso, use uma automação.
      </T>
      {existing ? <View style={{ marginTop: 20 }}><Button label="Apagar cena" variant="ghost" icon="trash-outline" onPress={remove} /></View> : null}
    </Screen>
  );
}
