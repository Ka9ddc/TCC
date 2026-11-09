import { useEffect, useRef, useState } from 'react';
import { PermissionsAndroid, Platform, View } from 'react-native';
import { BleErrorCode, BleManager, Device, State } from 'react-native-ble-plx';
import { useRouter } from 'expo-router';
import { useToast } from '../src/components/Toast';
import { Banner, Button, Card, Field, Header, Screen } from '../src/components/ui';
import { useHub } from '../src/state/HubProvider';
import { HubClient } from '../src/api/client';

const SERVICE = '0148454d-5552-019a-114e-402f9e1a4c7d';
const WIFI = '0348454d-5552-019a-114e-402f9e1a4c7d';
const STATUS = '0248454d-5552-019a-114e-402f9e1a4c7d';
const DEFAULT_TOKEN = 'TCC-Hub-2026-Local';

type HubStatus = {
  type?: string;
  state?: string;
  wifi_connected?: boolean;
  ip?: string | null;
  message?: string;
};

function decodeStatus(value: string): HubStatus | null {
  try {
    const text = atob(value);
    if (!text) return null;
    return JSON.parse(text) as HubStatus;
  } catch {
    return null;
  }
}

export default function BleProvisioning() {
  const router = useRouter();
  const hub = useHub();
  const toast = useToast();
  const [manager] = useState(() => new BleManager());
  const [device, setDevice] = useState<Device | null>(null);
  const [state, setState] = useState('Procurando o MeuHub...');
  const [ssid, setSsid] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const provisioningRef = useRef(false);
  const deviceRef = useRef<Device | null>(null);
  const statusSubscriptionRef = useRef<any>(null);
  const statusPollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const completedRef = useRef(false);

  const stopStatusPolling = () => {
    if (statusPollRef.current) {
      clearInterval(statusPollRef.current);
      statusPollRef.current = null;
    }
  };

  const startStatusPolling = () => {
    stopStatusPolling();

    statusPollRef.current = setInterval(async () => {
      try {
        const characteristic = await deviceRef.current?.readCharacteristicForService(
          SERVICE,
          STATUS,
        );

        if (!characteristic?.value) return;

        const status = decodeStatus(characteristic.value);

        if (!status) return;

        if (status.state === 'WIFI_CONNECTED') {
          void completeProvisioning(status);
        }

        if (status.state === 'WIFI_FAILED') {
          stopStatusPolling();

          provisioningRef.current = false;
          setBusy(false);
          setState('Falha ao conectar ao Wi‑Fi. Tente novamente.');

          toast.show('Não foi possível conectar ao Wi‑Fi.', 'error');
        }
      } catch (error) {
        console.warn('[BLE] Falha ao ler status:', error);
      }
    }, 2000);
  };

  const completeProvisioning = async (status: HubStatus) => {
    if (completedRef.current) return;
    completedRef.current = true;
    stopStatusPolling();
    statusSubscriptionRef.current?.remove();
    statusSubscriptionRef.current = null;
    provisioningRef.current = false;
    setBusy(false);
    setState('Hub conectado ao Wi‑Fi. Abrindo o painel...');

    const candidates = status.ip
      ? [`http://${status.ip}`, 'http://192.168.4.1']
      : ['http://192.168.4.1'];
    let reachableHost: string | null = null;

    for (const host of candidates) {
      try {
        await new HubClient(host, DEFAULT_TOKEN).status();
        reachableHost = host;
        break;
      } catch {
        // The phone may still be connected to the hub AP instead of the home network.
      }
    }

    if (!reachableHost) {
      completedRef.current = false;
      setState(
        status.ip
          ? `Wi‑Fi conectado em ${status.ip}, mas o celular não alcança o hub. Conecte o celular à mesma rede Wi‑Fi e tente novamente.`
          : 'Wi‑Fi conectado, mas o celular não alcança o hub. Conecte o celular à rede do hub e tente novamente.',
      );
      toast.show('O hub conectou, mas está inacessível para este celular.', 'error');
      return;
    }

    await hub.connect({ host: reachableHost, token: DEFAULT_TOKEN });
    router.replace('/');
  };

  useEffect(() => {
    let active = true;

    const prepare = async () => {
      if (Platform.OS === 'android' && Number(Platform.Version) >= 31) {
        const permissions = await PermissionsAndroid.requestMultiple([
          PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
          PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
        ]);

        if (
          Object.values(permissions).some(
            (value) => value !== PermissionsAndroid.RESULTS.GRANTED,
          )
        ) {
          setState('Permita o acesso ao Bluetooth nas configurações do celular.');
          return;
        }
      } else if (Platform.OS === 'android') {
        const permission = await PermissionsAndroid.request(
          PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
        );

        if (permission !== PermissionsAndroid.RESULTS.GRANTED) {
          setState('Permita o acesso à localização para procurar dispositivos BLE.');
          return;
        }
      }

      if ((await manager.state()) !== State.PoweredOn) {
        setState('Ative o Bluetooth do celular.');
        return;
      }

      setState('Procurando o hub por Bluetooth...');

      manager.startDeviceScan(null, null, (error, found) => {
        if (!active) return;

        if (error) {
          setState('Falha ao procurar dispositivos BLE.');
          return;
        }

        if (found?.name === 'MeuHub' || found?.localName === 'MeuHub') {
          manager.stopDeviceScan();
          setDevice(found);
          deviceRef.current = found;
          setState('Hub encontrado. Conectando...');

          found
            .connect()
            .then((connected) => connected.discoverAllServicesAndCharacteristics())
            .then(async (connected) => {
              await connected.requestMTU(512);

              connected.onDisconnected(() => {
                if (!provisioningRef.current) return;

                provisioningRef.current = false;
                statusSubscriptionRef.current?.remove();
                statusSubscriptionRef.current = null;

                setDevice(null);
                deviceRef.current = null;
                setBusy(false);
                setState('Conexão Bluetooth perdida durante o provisionamento.');
              });

              statusSubscriptionRef.current = connected.monitorCharacteristicForService(
                SERVICE,
                STATUS,
                (statusError, characteristic) => {
                  if (statusError || !characteristic?.value) return;

                  const status = decodeStatus(characteristic.value);

                  if (!status) return;

                  if (status.state === 'CREDENTIALS_RECEIVED') {
                    setState('Credenciais recebidas. Conectando ao Wi‑Fi...');
                  }

                  if (status.state === 'WIFI_CONNECTING') {
                    setState('Conectando ao Wi‑Fi...');
                  }

                  if (status.state === 'WIFI_CONNECTED') {
                    void completeProvisioning(status);
                  }

                  if (status.state === 'WIFI_FAILED') {
                    provisioningRef.current = false;
                    setBusy(false);
                    setState('Falha ao conectar ao Wi‑Fi. Tente novamente.');

                    toast.show('Não foi possível conectar ao Wi‑Fi.', 'error');
                  }
                },
              );

              if (active) {
                setDevice(connected);
                deviceRef.current = connected;
                setState('Pronto para configurar o Wi‑Fi.');
              }
            })
            .catch(() => setState('Não foi possível conectar ao hub.'));
        }
      });
    };

    prepare();

    return () => {
      active = false;
      stopStatusPolling();
      manager.stopDeviceScan();
      statusSubscriptionRef.current?.remove();
      manager.destroy();
      
    };
  }, [manager]);

const send = async () => {
  if (!device || !ssid.trim() || password.length === 0) {
    toast.show('Informe a rede e a senha do Wi‑Fi.', 'error');
    return;
  }

  setBusy(true);
  provisioningRef.current = true;

  try {
    setState('Enviando credenciais...');

    const payload = btoa(
      JSON.stringify({
        type: 'wifi_config',
        ssid: ssid.trim(),
        password,
      }),
    );

    console.log('[BLE] Enviando payload:', payload);

    await device.writeCharacteristicWithResponseForService(
      SERVICE,
      WIFI,
      payload,
    );

    console.log('[BLE] Escrita concluída com sucesso');

    startStatusPolling();
    setState('Credenciais enviadas. Aguardando confirmação do hub...');
  } catch (error) {
    console.error('[BLE] Erro ao enviar credenciais:', error);

    provisioningRef.current = false;
    setBusy(false);

    const errorCode = (error as { errorCode?: number }).errorCode;

    if (errorCode === BleErrorCode.DeviceDisconnected) {
      setState('O hub encerrou a conexão Bluetooth. Tente novamente.');
    } else {
      toast.show('Não foi possível enviar as credenciais.', 'error');
    }
  }
};

  return (
    <Screen
      footer={
        <Button
          label="Enviar credenciais"
          onPress={send}
          loading={busy}
          disabled={!device}
        />
      }
    >
      <Header title="Provisionamento Bluetooth" back />

      <Banner
        tone="info"
        icon="bluetooth-outline"
        title={state}
        body="O Bluetooth é usado para configurar o hub. Após a configuração, o hub ficará acessível pela rede Wi‑Fi local."
      />

      <Card>
        <Field
          label="Rede Wi‑Fi doméstica"
          value={ssid}
          onChangeText={setSsid}
          autoCapitalize="none"
        />

        <Field
          label="Senha"
          value={password}
          onChangeText={setPassword}
          secureTextEntry
          autoCapitalize="none"
        />
      </Card>

      <View />
    </Screen>
  );
}
