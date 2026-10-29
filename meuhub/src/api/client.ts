import {
  CommandResult, EventResult, HistoryEntry, HubConfig, HubDevice,
  HubRule, HubStatus, RuleDraft,
} from './types';

export type HubErrorKind = 'network' | 'timeout' | 'unauthorized' | 'hardware' | 'http';

export class HubError extends Error {
  constructor(public kind: HubErrorKind, message: string, public status?: number) {
    super(message);
  }
}

export function errorMessage(e: unknown): string {
  if (e instanceof Error) return e.message;
  return 'Algo deu errado';
}

export function normalizeHost(raw: string): string {
  let h = raw.trim().replace(/\/+$/, '');
  if (!/^https?:\/\//i.test(h)) h = `http://${h}`;
  return h;
}

export class HubClient {
  readonly baseUrl: string;
  constructor(host: string, private token: string) {
    this.baseUrl = normalizeHost(host);
  }

  private async request<T>(method: string, path: string, body?: unknown, timeoutMs = 6000): Promise<T> {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), timeoutMs);
    try {
      const res = await fetch(this.baseUrl + path, {
        method,
        headers: {
          Authorization: `Bearer ${this.token}`,
          ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
        },
        body: body !== undefined ? JSON.stringify(body) : undefined,
        signal: ctrl.signal,
      });
      const text = await res.text();
      let data: any = null;
      try { data = text ? JSON.parse(text) : null; } catch { data = null; }

      if (res.status === 401) throw new HubError('unauthorized', 'Token inválido ou ausente', 401);
      if (!res.ok) {
        if (res.status === 502) {
          throw new HubError('hardware', 'O rádio Zigbee (C6) recusou o comando. O dispositivo pode estar offline ou sem endpoint.', 502);
        }
        if (res.status === 504) {
          throw new HubError('timeout', 'O rádio Zigbee (C6) não respondeu a tempo.', 504);
        }
        throw new HubError('http', text.trim() || `Erro HTTP ${res.status}`, res.status);
      }
      return data as T;
    } catch (e) {
      if (e instanceof HubError) throw e;
      if ((e as any)?.name === 'AbortError') throw new HubError('timeout', 'O hub demorou demais para responder.');
      throw new HubError('network', 'Não foi possível alcançar o hub.');
    } finally {
      clearTimeout(timer);
    }
  }

  // leitura
  status() { return this.request<HubStatus>('GET', '/api/status', undefined, 4000); }
  devices() { return this.request<HubDevice[]>('GET', '/api/devices', undefined, 4000); }
  rules() { return this.request<HubRule[]>('GET', '/api/rules'); }
  history() { return this.request<HistoryEntry[]>('GET', '/api/history'); }
  config() { return this.request<HubConfig>('GET', '/api/config'); }

  // dispositivos (o hub espera a resposta do C6: até 11 s)
  command(id: string, action: 'on' | 'off') {
    return this.request<CommandResult>('POST', `/api/devices/${encodeURIComponent(id)}/command`, { action }, 13000);
  }
  removeDevice(id: string) {
    return this.request<CommandResult>('DELETE', `/api/devices/${encodeURIComponent(id)}`, undefined, 13000);
  }
  permitJoin(seconds: number) {
    return this.request<CommandResult>('POST', '/api/zigbee/permit-join', { duration_seconds: seconds }, 13000);
  }

  // automações
  createRule(rule: RuleDraft) {
    return this.request<{ status: string; id: number }>('POST', '/api/rules', rule);
  }
  updateRule(id: number, rule: RuleDraft) {
    return this.request<{ status: string }>('PUT', `/api/rules/${id}`, rule);
  }
  deleteRule(id: number) {
    return this.request<{ status: string }>('DELETE', `/api/rules/${id}`);
  }

  // configuração
  setWifi(cfg: { sta_ssid: string; sta_password: string; ap_ssid: string; ap_password: string }) {
    return this.request<{ status: string; restart_required: boolean }>('POST', '/api/config/wifi', cfg);
  }
  setToken(token: string) {
    return this.request<{ status: string }>('POST', '/api/config/auth', { token });
  }

  // simulação (aciona o motor de regras de verdade)
  testEvent(device: string, event: string, value: boolean | number) {
    return this.request<EventResult>('POST', '/api/test/events', { device, event, value });
  }
}
