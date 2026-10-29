import React, { useState } from 'react';
import { Pressable, View } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useLocalSearchParams, useRouter } from 'expo-router';
import { Banner, Button, Card, Chip, Field, Screen } from '../src/components/ui';
import { T, useTheme } from '../src/theme/theme';
import { useHub } from '../src/state/HubProvider';
import { HubClient, HubError, normalizeHost } from '../src/api/client';

const DEFAULT_TOKEN = 'TCC-Hub-2026-Local';

export default function Connect() {
  const { t } = useTheme();
  const router = useRouter();
  const { edit } = useLocalSearchParams<{ edit?: string }>();
  const { connection, connect } = useHub();
  const [host, setHost] = useState(connection?.host.replace(/^http:\/\//, '') ?? '192.168.4.1');
  const [token, setToken] = useState(connection?.token ?? DEFAULT_TOKEN);
  const [show, setShow] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<{ field: 'host' | 'token'; msg: string } | null>(null);

  const submit = async () => {
    setError(null);
    if (!host.trim()) return setError({ field: 'host', msg: 'Informe o endereço do hub.' });
    if (!token.trim()) return setError({ field: 'token', msg: 'Informe o token de acesso.' });
    setBusy(true);
    try {
      const c = new HubClient(host, token.trim());
      await c.status();
      await c.config(); // rota protegida: valida o token
      await connect({ host: normalizeHost(host), token: token.trim() });
      router.replace('/');
    } catch (e) {
      if (e instanceof HubError && e.kind === 'unauthorized') {
        setError({ field: 'token', msg: 'O hub não aceitou este token.' });
      } else {
        setError({ field: 'host', msg: `Não encontrei o hub em ${host.trim()}. Confira o endereço e se o celular está na mesma rede.` });
      }
    } finally { setBusy(false); }
  };

  return (
    <Screen footer={<Button label={connection ? 'Salvar e reconectar' : 'Conectar ao hub'} icon="link-outline" onPress={submit} loading={busy} />}>
      {connection && edit !== '1' ? (
        <Pressable onPress={() => router.back()} hitSlop={12} style={{ marginBottom: 14 }}>
          <Ionicons name="chevron-back" size={24} color={t.ink} />
        </Pressable>
      ) : null}

      <View style={{ marginTop: 24, marginBottom: 28 }}>
        <View style={{ width: 64, height: 64, borderRadius: 32, backgroundColor: t.lamp, alignItems: 'center', justifyContent: 'center', marginBottom: 20, shadowColor: t.lamp, shadowOpacity: 0.5, shadowRadius: 20, shadowOffset: { width: 0, height: 8 }, elevation: 10 }}>
          <Ionicons name="bulb" size={32} color={t.lampInk} />
        </View>
        <T v="display">MeuHub</T>
        <T v="body" c="inkSoft" style={{ marginTop: 6 }}>Seu hub de automação, funcionando dentro de casa e sem nuvem.</T>
      </View>

      <Card style={{ marginBottom: 20 }}>
        <T v="h3" style={{ marginBottom: 4 }}>Antes de conectar</T>
        <T v="small" c="inkSoft" w={400}>
          Conecte o celular à rede Wi-Fi do hub (MeuHub-IoT) ou à mesma rede doméstica em que o hub está. Na rede do hub, o endereço padrão é 192.168.4.1.
        </T>
      </Card>

      <Field
        label="Endereço do hub" value={host} onChangeText={setHost} placeholder="192.168.4.1"
        keyboardType="url" error={error?.field === 'host' ? error.msg : null}
      />
      <View style={{ flexDirection: 'row', gap: 8, marginTop: -6, marginBottom: 20 }}>
        <Chip label="Rede do hub" icon="wifi" active={host === '192.168.4.1'} onPress={() => setHost('192.168.4.1')} />
      </View>

      <Field
        label="Token de acesso" value={token} onChangeText={setToken} secureTextEntry={!show}
        error={error?.field === 'token' ? error.msg : null}
        hint="O firmware vem com um token padrão. Troque-o em Ajustes › Segurança."
        right={<Pressable onPress={() => setShow(!show)} hitSlop={10}><Ionicons name={show ? 'eye-off-outline' : 'eye-outline'} size={20} color={t.inkMuted} /></Pressable>}
      />

      {token === DEFAULT_TOKEN ? (
        <Banner tone="warn" icon="shield-outline" title="Token padrão em uso" body="Qualquer pessoa que conheça o código-fonte pode controlar o hub. Depois de conectar, defina um token próprio." />
      ) : null}
    </Screen>
  );
}
