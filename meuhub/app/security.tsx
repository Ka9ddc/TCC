import React, { useState } from 'react';
import { Alert, Pressable, Share } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useRouter } from 'expo-router';
import * as Crypto from 'expo-crypto';
import { Banner, Button, Card, Field, Header, Screen } from '../src/components/ui';
import { useToast } from '../src/components/Toast';
import { T, useTheme } from '../src/theme/theme';
import { useHub } from '../src/state/HubProvider';
import { errorMessage } from '../src/api/client';

const ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789';
const generate = (len = 24) => {
  const bytes = Crypto.getRandomBytes(len);
  return Array.from(bytes, (b) => ALPHABET[b % ALPHABET.length]).join('');
};

export default function Security() {
  const hub = useHub();
  const { t } = useTheme();
  const router = useRouter();
  const toast = useToast();
  const [token, setToken] = useState('');
  const [show, setShow] = useState(false);
  const [busy, setBusy] = useState(false);

  const error =
    token.length === 0 ? null
    : token.length < 16 || token.length > 63 ? 'Use de 16 a 63 caracteres.'
    : /[^\x21-\x7e]/.test(token) ? 'Use só caracteres imprimíveis, sem espaços.'
    : null;

  const save = () => {
    if (!token || error) return toast.show(error ?? 'Digite o novo token', 'error');
    Alert.alert('Trocar o token?', 'Outros celulares que usam o token antigo perderão o acesso até receberem o novo.', [
      { text: 'Cancelar', style: 'cancel' },
      {
        text: 'Trocar',
        onPress: async () => {
          setBusy(true);
          try {
            await hub.client!.setToken(token);
            await hub.connect({ host: hub.connection!.host, token });
            toast.show('Token atualizado', 'ok');
            router.back();
          } catch (e) { toast.show(errorMessage(e), 'error'); }
          setBusy(false);
        },
      },
    ]);
  };

  return (
    <Screen footer={<Button label="Trocar token no hub" onPress={save} loading={busy} disabled={!token || !!error} />}>
      <Header title="Segurança" subtitle="O token protege a API do hub: comandos, regras e configurações." back />

      {hub.connection?.token === 'TCC-Hub-2026-Local' ? (
        <Banner tone="warn" icon="shield-outline" title="Você ainda usa o token padrão" body="Ele está no código-fonte do firmware. Gere um novo e guarde em local seguro." />
      ) : null}

      <Card style={{ marginBottom: 20 }}>
        <T v="small" c="inkSoft">Token atual neste celular</T>
        <T v="body" w={600} style={{ marginTop: 4 }}>{show ? hub.connection?.token : '•'.repeat(12)}</T>
      </Card>

      <Field
        label="Novo token" value={token} onChangeText={setToken} secureTextEntry={!show} error={error}
        hint="16 a 63 caracteres, sem espaços."
        right={<Pressable onPress={() => setShow(!show)} hitSlop={10}><Ionicons name={show ? 'eye-off-outline' : 'eye-outline'} size={20} color={t.inkMuted} /></Pressable>}
      />
      <Button label="Gerar token forte" variant="soft" icon="sparkles-outline" onPress={() => { setToken(generate()); setShow(true); }} />
      {token && !error ? (
        <Button label="Compartilhar token para guardar" variant="ghost" small icon="share-outline" style={{ marginTop: 8 }} onPress={() => Share.share({ message: token })} />
      ) : null}
    </Screen>
  );
}
