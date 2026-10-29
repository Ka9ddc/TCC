import React, { useState } from 'react';
import { Banner, Card, Empty, Header, Row, Screen } from '../src/components/ui';
import { T } from '../src/theme/theme';
import { useHub } from '../src/state/HubProvider';
import { agoFromUptime } from '../src/lib/format';
import { describeHistory } from '../src/lib/history';

export default function Activity() {
  const hub = useHub();
  const [refreshing, setRefreshing] = useState(false);
  const uptime = hub.status?.uptime_seconds ?? 0;
  const items = [...hub.history].reverse();

  return (
    <Screen refreshing={refreshing} onRefresh={async () => { setRefreshing(true); await hub.refresh(); setRefreshing(false); }}>
      <Header title="Atividade" subtitle="Eventos recebidos e comandos enviados pelo hub." back />
      {!hub.historyAvailable ? (
        <Banner tone="warn" icon="alert-circle-outline" title="Histórico indisponível"
          body="A rota /api/history não respondeu. Provável causa: limite de rotas HTTP do firmware (max_uri_handlers). Veja Ajustes › Diagnóstico." />
      ) : items.length === 0 ? (
        <Empty icon="list-outline" title="Sem atividade" body="Quando dispositivos mudarem de estado ou uma automação rodar, aparece aqui." />
      ) : (
        <Card style={{ paddingVertical: 4 }}>
          {items.map((h, i) => {
            const d = describeHistory(h, hub.displayName);
            return <Row key={i} icon={d.icon} title={d.title} subtitle={`${h.type === 'command' ? 'Comando' : 'Evento'} · ${agoFromUptime(h.uptime_seconds, uptime)}`} last={i === items.length - 1} />;
          })}
        </Card>
      )}
      <T v="small" c="inkMuted" w={400} style={{ marginTop: 16 }}>
        O hub guarda só os 16 registros mais recentes, na memória. Ao reiniciar, o histórico zera.
      </T>
    </Screen>
  );
}
