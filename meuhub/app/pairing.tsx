import React, { useEffect, useRef, useState } from 'react';
import { View } from 'react-native';
import { useRouter } from 'expo-router';
import { Button, Card, Chip, Header, Row, Screen, SectionTitle } from '../src/components/ui';
import { useToast } from '../src/components/Toast';
import { T, useTheme } from '../src/theme/theme';
import { useHub } from '../src/state/HubProvider';
import { errorMessage } from '../src/api/client';

const DURATIONS = [30, 60, 120, 180]; // o firmware aceita de 1 a 180 s

export default function Pairing() {
  const hub = useHub();
  const { t } = useTheme();
  const router = useRouter();
  const toast = useToast();
  const [duration, setDuration] = useState(60);
  const [remaining, setRemaining] = useState(0);
  const [busy, setBusy] = useState(false);
  const [found, setFound] = useState<string[]>([]);
  const known = useRef<Set<string>>(new Set(hub.devices.map((d) => d.id)));
  const active = remaining > 0;

  // contagem regressiva
  useEffect(() => {
    if (!active) return;
    const timer = setInterval(() => setRemaining((r) => Math.max(0, r - 1)), 1000);
    return () => clearInterval(timer);
  }, [active]);

  // enquanto a rede está aberta, consulta o hub com mais frequência
  useEffect(() => {
    if (!active) return;
    const timer = setInterval(() => { hub.refreshFast(); }, 2000);
    return () => clearInterval(timer);
  }, [active, hub]);

  // detecta dispositivos novos
  useEffect(() => {
    for (const d of hub.devices) {
      if (!known.current.has(d.id)) {
        known.current.add(d.id);
        setFound((f) => [...f, d.id]);
        toast.show(`Novo dispositivo: ${hub.displayName(d.id)}`, 'ok');
      }
    }
  }, [hub.devices]); // eslint-disable-line react-hooks/exhaustive-deps

  const open = async () => {
    setBusy(true);
    try {
      await hub.permitJoin(duration);
      setRemaining(duration);
    } catch (e) { toast.show(errorMessage(e), 'error'); }
    setBusy(false);
  };

  return (
    <Screen footer={!active ? <Button label="Abrir rede Zigbee" icon="radio-outline" loading={busy} onPress={open} /> : (
      <Button label="Concluir" variant="soft" onPress={() => router.back()} />
    )}>
      <Header title="Adicionar dispositivo" subtitle="Abra a rede para que o hub aceite novos dispositivos Zigbee." back />

      {!active ? (
        <>
          <Card style={{ marginBottom: 20 }}>
            <T v="h3" style={{ marginBottom: 6 }}>Como parear</T>
            <T v="body" c="inkSoft">
              Abra a rede aqui e, em seguida, coloque o dispositivo em modo de pareamento. Em muitas lâmpadas isso é feito ligando e desligando o interruptor 5 vezes seguidas. Veja o manual do fabricante.
            </T>
          </Card>
          <T v="small" c="inkSoft" style={{ marginBottom: 8 }}>Manter a rede aberta por</T>
          <View style={{ flexDirection: 'row', gap: 8, flexWrap: 'wrap' }}>
            {DURATIONS.map((d) => (
              <Chip key={d} label={d < 60 ? `${d} s` : `${d / 60} min`} active={d === duration} onPress={() => setDuration(d)} />
            ))}
          </View>
        </>
      ) : (
        <Card style={{ alignItems: 'center', paddingVertical: 28 }}>
          <T v="small" c="signal" w={700}>Rede aberta, procurando…</T>
          <T v="num" style={{ marginVertical: 6 }}>{remaining}</T>
          <T v="small" c="inkMuted">segundos restantes</T>
          <View style={{ height: 6, alignSelf: 'stretch', backgroundColor: t.surfaceAlt, borderRadius: 3, marginTop: 20, overflow: 'hidden' }}>
            <View style={{ height: 6, width: `${(remaining / duration) * 100}%`, backgroundColor: t.signal }} />
          </View>
        </Card>
      )}

      {found.length ? (
        <>
          <SectionTitle title="Encontrados" />
          <Card style={{ paddingVertical: 4 }}>
            {found.map((id, i) => (
              <Row key={id} icon="bulb-outline" title={hub.displayName(id)} subtitle="Pronto para usar" last={i === found.length - 1}
                onPress={() => router.replace(`/device/${id}`)} />
            ))}
          </Card>
        </>
      ) : null}

      <T v="small" c="inkMuted" w={400} style={{ marginTop: 20 }}>
        O hub só mostra o dispositivo depois de descobrir o endpoint On/Off dele, o que pode levar alguns segundos após o pareamento.
      </T>
    </Screen>
  );
}
