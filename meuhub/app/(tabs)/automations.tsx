import React, { useState } from 'react';
import { View } from 'react-native';
import { useRouter } from 'expo-router';
import { Button, Card, Empty, Header, IconButton, Screen, SectionTitle, Toggle } from '../../src/components/ui';
import { useToast } from '../../src/components/Toast';
import { T, useTheme } from '../../src/theme/theme';
import { useHub } from '../../src/state/HubProvider';
import { errorMessage } from '../../src/api/client';
import { describeAction, describeTrigger } from '../../src/lib/rules';

const MAX_RULES = 8; // MAX_AUTOMATION_RULES no firmware

export default function Automations() {
  const hub = useHub();
  const router = useRouter();
  const toast = useToast();
  const { t } = useTheme();
  const [refreshing, setRefreshing] = useState(false);
  const full = hub.rules.length >= MAX_RULES;
  const hasSensor = hub.devices.some((d) => d.type !== 'on_off');
  const hasSwitch = hub.devices.some((d) => d.type === 'on_off');
  const unitOf = (id: string) => hub.devices.find((d) => d.id === id)?.unit;

  const setEnabled = async (r: (typeof hub.rules)[number], v: boolean) => {
    try { await hub.setRuleEnabled(r, v); } catch (e) { toast.show(errorMessage(e), 'error'); }
  };

  return (
    <Screen tab refreshing={refreshing} onRefresh={async () => { setRefreshing(true); await hub.refresh(); setRefreshing(false); }}>
      <Header
        title="Automações"
        subtitle={`${hub.rules.length} de ${MAX_RULES} regras · rodam no hub, sem internet`}
        right={<IconButton icon="add" label="Nova automação" onPress={() => (full ? toast.show('Limite de 8 regras atingido', 'error') : router.push('/rule/new'))} />}
      />

      {hub.rules.length === 0 ? (
        <Empty
          icon="git-network-outline" title="Nenhuma automação"
          body="Automações ligam ou desligam dispositivos quando algo acontece, como a luz do ambiente cair."
          action={<Button label="Criar automação" small icon="add" onPress={() => router.push('/rule/new')} />}
        />
      ) : (
        <View style={{ gap: 12 }}>
          {hub.rules.map((r) => (
            <Card key={r.id} onPress={() => router.push(`/rule/${r.id}`)} style={{ opacity: r.enabled ? 1 : 0.7 }}>
              <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
                <View style={{ flex: 1 }}>
                  <T v="h3">{hub.ruleName(r)}</T>
                </View>
                <Toggle value={r.enabled} onValueChange={(v) => setEnabled(r, v)} />
              </View>
              <T v="body" c="inkSoft" style={{ marginTop: 8 }}>
                Quando {describeTrigger(r, hub.displayName, unitOf(r.trigger_device))}
              </T>
              <T v="body" style={{ marginTop: 2 }}>
                {r.actions.map((a) => describeAction(a, hub.displayName)).join(' e ').replace(/^./, (c) => c.toUpperCase())}
              </T>
            </Card>
          ))}
        </View>
      )}

      {!full && hasSwitch ? (
        <>
          <SectionTitle title="Ideias" />
          <View style={{ gap: 12 }}>
            {hasSensor ? (
              <Card onPress={() => router.push({ pathname: '/rule/new', params: { template: 'dark' } })}>
                <T v="h3">Acender quando escurecer</T>
                <T v="small" c="inkSoft" w={400}>Liga uma luz quando o sensor de luminosidade ficar abaixo de um valor.</T>
              </Card>
            ) : null}
            <Card onPress={() => router.push({ pathname: '/rule/new', params: { template: 'timer' } })}>
              <T v="h3">Apagar sozinho depois de um tempo</T>
              <T v="small" c="inkSoft" w={400}>Quando uma luz ligar, desliga de novo após alguns minutos.</T>
            </Card>
          </View>
        </>
      ) : null}
    </Screen>
  );
}
