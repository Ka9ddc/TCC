import React, { useEffect, useState } from 'react';
import { Alert } from 'react-native';
import { useRouter } from 'expo-router';
import { Banner, Button, Field, Header, Screen } from '../src/components/ui';
import { useToast } from '../src/components/Toast';
import { useHub } from '../src/state/HubProvider';
import { errorMessage } from '../src/api/client';

export default function WifiConfig() {
  const hub = useHub();
  const router = useRouter();
  const toast = useToast();
  const [staSsid, setStaSsid] = useState('');
  const [staPass, setStaPass] = useState('');
  const [apSsid, setApSsid] = useState('');
  const [apPass, setApPass] = useState('');
  const [busy, setBusy] = useState(false);
  const [filled, setFilled] = useState(false);

  useEffect(() => {
    if (!filled && hub.config) { setStaSsid(hub.config.sta_ssid); setApSsid(hub.config.ap_ssid); setFilled(true); }
  }, [hub.config, filled]);

  const err = {
    staSsid: !staSsid.trim() ? 'Informe o nome da rede.' : staSsid.length > 32 ? 'No máximo 32 caracteres.' : null,
    staPass: staPass.length > 63 ? 'No máximo 63 caracteres.' : null,
    apSsid: !apSsid.trim() ? 'Informe o nome da rede.' : apSsid.length > 32 ? 'No máximo 32 caracteres.' : null,
    apPass: apPass.length > 0 && (apPass.length < 8 || apPass.length > 63) ? 'Use de 8 a 63 caracteres, ou deixe vazio para rede aberta.' : null,
  };
  const invalid = Object.values(err).some(Boolean);

  const apply = () => {
    if (invalid) return toast.show('Corrija os campos destacados', 'error');
    Alert.alert('Salvar configuração?', 'As mudanças só valem depois de reiniciar o hub. O celular pode precisar se reconectar à nova rede.', [
      { text: 'Cancelar', style: 'cancel' },
      {
        text: 'Salvar',
        onPress: async () => {
          setBusy(true);
          try {
            await hub.client!.setWifi({ sta_ssid: staSsid.trim(), sta_password: staPass, ap_ssid: apSsid.trim(), ap_password: apPass });
            toast.show('Salvo. Reinicie o hub para aplicar.', 'ok');
            hub.refresh();
            router.back();
          } catch (e) { toast.show(errorMessage(e), 'error'); }
          setBusy(false);
        },
      },
    ]);
  };

  return (
    <Screen footer={<Button label="Salvar no hub" onPress={apply} loading={busy} />}>
      <Header title="Wi-Fi do hub" back />
      <Banner tone="warn" icon="alert-circle-outline" title="Confira antes de salvar"
        body="Se a rede doméstica estiver errada, o hub pode não conseguir se conectar depois de reiniciar." />

      <Field label="Rede doméstica (nome)" value={staSsid} onChangeText={setStaSsid} error={err.staSsid} />
      <Field label="Rede doméstica (senha)" value={staPass} onChangeText={setStaPass} secureTextEntry error={err.staPass}
        hint="Obrigatória: o hub não devolve a senha atual." />
      <Field label="Rede do hub (nome)" value={apSsid} onChangeText={setApSsid} error={err.apSsid} />
      <Field label="Rede do hub (senha)" value={apPass} onChangeText={setApPass} secureTextEntry error={err.apPass}
        hint="Vazio deixa a rede do hub aberta, sem senha." />
    </Screen>
  );
}
