import React, { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { AppState } from 'react-native';
import * as Haptics from 'expo-haptics';
import { HubClient, HubError } from '../api/client';
import {
  HistoryEntry, HubConfig, HubDevice, HubRule, HubStatus, RuleDraft,
} from '../api/types';
import { prettify } from '../lib/format';
import {
  clearConnection, Connection, DEFAULT_META, loadConnection, loadMeta, Meta,
  saveConnection, saveMeta, Scene,
} from './storage';

export type UiDevice = HubDevice & { estimated?: boolean };
export type Link = 'setup' | 'connecting' | 'online' | 'offline' | 'unauthorized';

interface HubCtx {
  ready: boolean;
  connection: Connection | null;
  client: HubClient | null;
  link: Link;
  status: HubStatus | null;
  devices: UiDevice[];
  rules: HubRule[];
  history: HistoryEntry[];
  historyAvailable: boolean;
  config: HubConfig | null;
  meta: Meta;
  lastSync: number | null;

  connect: (c: Connection) => Promise<void>;
  disconnect: () => Promise<void>;
  refresh: () => Promise<void>;
  refreshFast: () => Promise<void>;

  sendCommand: (id: string, action: 'on' | 'off') => Promise<void>;
  toggleDevice: (id: string) => Promise<void>;
  removeDevice: (id: string) => Promise<void>;
  permitJoin: (seconds: number) => Promise<void>;

  saveRule: (draft: RuleDraft, name?: string) => Promise<number>;
  setRuleEnabled: (rule: HubRule, enabled: boolean) => Promise<void>;
  deleteRule: (id: number) => Promise<void>;

  updateMeta: (fn: (m: Meta) => Meta) => void;
  displayName: (id: string) => string;
  ruleName: (r: HubRule) => string;
  runScene: (s: Pick<Scene, 'steps'>) => Promise<{ ok: number; fail: number }>;
}

const Ctx = createContext<HubCtx>(null as unknown as HubCtx);
export const useHub = () => useContext(Ctx);

const haptic = (kind: 'ok' | 'err') => {
  const p = kind === 'ok'
    ? Haptics.notificationAsync(Haptics.NotificationFeedbackType.Success)
    : Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error);
  p.catch(() => {});
};

export function HubProvider({ children }: { children: React.ReactNode }) {
  const [ready, setReady] = useState(false);
  const [connection, setConnection] = useState<Connection | null>(null);
  const [meta, setMeta] = useState<Meta>(DEFAULT_META);
  const [status, setStatus] = useState<HubStatus | null>(null);
  const [devices, setDevices] = useState<UiDevice[]>([]);
  const [rules, setRules] = useState<HubRule[]>([]);
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [historyAvailable, setHistoryAvailable] = useState(true);
  const [config, setConfig] = useState<HubConfig | null>(null);
  const [reachable, setReachable] = useState<'unknown' | 'yes' | 'no'>('unknown');
  const [authError, setAuthError] = useState(false);
  const [lastSync, setLastSync] = useState<number | null>(null);

  const failures = useRef(0);
  const busy = useRef(0);
  const commanded = useRef<Record<string, { state: boolean; at: number }>>({});

  const client = useMemo(
    () => (connection ? new HubClient(connection.host, connection.token) : null),
    [connection],
  );

  // carga inicial
  useEffect(() => {
    (async () => {
      const [c, m] = await Promise.all([loadConnection(), loadMeta()]);
      setMeta(m);
      setConnection(c);
      setReady(true);
    })();
  }, []);

  // trocou de hub → limpa o que era do anterior
  useEffect(() => {
    setStatus(null); setDevices([]); setRules([]); setHistory([]); setConfig(null);
    setReachable('unknown'); setAuthError(false); setHistoryAvailable(true);
    failures.current = 0; commanded.current = {};
  }, [client]);

  const fast = useCallback(async () => {
    if (!client) return;
    try {
      const [s, d] = await Promise.all([client.status(), client.devices()]);
      failures.current = 0;
      const list = Array.isArray(d) ? d : [];
      const merged: UiDevice[] = list.map((dev) => {
        if (dev.type !== 'on_off') return dev;
        const c = commanded.current[dev.id];
        if (!c) return dev;
        if (dev.state === c.state) { delete commanded.current[dev.id]; return dev; }
        if (Date.now() - c.at < 15000 || dev.state === undefined) {
          return { ...dev, state: c.state, estimated: true };
        }
        delete commanded.current[dev.id];
        return dev;
      });
      setStatus(s); setDevices(merged); setLastSync(Date.now()); setReachable('yes');
    } catch {
      failures.current += 1;
      if (failures.current >= 3) setReachable('no');
    }
  }, [client]);

  const slow = useCallback(async () => {
    if (!client) return;
    const [r, h, c] = await Promise.allSettled([client.rules(), client.history(), client.config()]);
    let auth = false;
    const check = (x: PromiseRejectedResult) => {
      if (x.reason instanceof HubError && x.reason.kind === 'unauthorized') auth = true;
    };
    if (r.status === 'fulfilled') setRules(Array.isArray(r.value) ? r.value : []); else check(r);
    if (h.status === 'fulfilled') {
      setHistory(Array.isArray(h.value) ? h.value : []);
      setHistoryAvailable(true);
    } else {
      check(h);
      if (h.reason instanceof HubError && h.reason.status === 404) setHistoryAvailable(false);
    }
    if (c.status === 'fulfilled') setConfig(c.value); else check(c);
    setAuthError(auth);
  }, [client]);

  // polling: status+devices a cada 3 s, resto a cada ~12 s
  useEffect(() => {
    if (!client) return;
    let tick = 0;
    let inflight = false;
    let active = AppState.currentState === 'active';
    const run = async () => {
      if (inflight || busy.current > 0 || !active) return;
      inflight = true;
      try {
        await fast();
        if (tick % 4 === 0) await slow();
        tick++;
      } finally { inflight = false; }
    };
    run();
    const timer = setInterval(run, 3000);
    const sub = AppState.addEventListener('change', (s) => {
      active = s === 'active';
      if (active) run();
    });
    return () => { clearInterval(timer); sub.remove(); };
  }, [client, fast, slow]);

  const link: Link = !connection ? 'setup'
    : reachable === 'no' ? 'offline'
    : reachable === 'unknown' ? 'connecting'
    : authError ? 'unauthorized' : 'online';

  const connect = useCallback(async (c: Connection) => {
    await saveConnection(c);
    setConnection(c);
  }, []);

  const disconnect = useCallback(async () => {
    await clearConnection();
    setConnection(null);
  }, []);

  const refresh = useCallback(async () => { await fast(); await slow(); }, [fast, slow]);

  const sendCommand = useCallback(async (id: string, action: 'on' | 'off') => {
    if (!client) throw new Error('Sem conexão com o hub');
    busy.current++;
    const next = action === 'on';
    try {
      await client.command(id, action);
      commanded.current[id] = { state: next, at: Date.now() };
      setDevices((ds) => ds.map((d) => (d.id === id ? { ...d, state: next, estimated: true } : d)));
      haptic('ok');
    } catch (e) {
      haptic('err');
      throw e;
    } finally {
      busy.current--;
      setTimeout(fast, 700);
    }
  }, [client, fast]);

  const toggleDevice = useCallback(async (id: string) => {
    const cur = devices.find((d) => d.id === id)?.state ?? false;
    await sendCommand(id, cur ? 'off' : 'on');
  }, [devices, sendCommand]);

  const removeDevice = useCallback(async (id: string) => {
    if (!client) throw new Error('Sem conexão com o hub');
    busy.current++;
    try { await client.removeDevice(id); }
    finally { busy.current--; setTimeout(() => { fast(); slow(); }, 1500); }
  }, [client, fast, slow]);

  const permitJoin = useCallback(async (seconds: number) => {
    if (!client) throw new Error('Sem conexão com o hub');
    busy.current++;
    try { await client.permitJoin(seconds); }
    finally { busy.current--; }
  }, [client]);

  const updateMeta = useCallback((fn: (m: Meta) => Meta) => {
    setMeta((m) => { const n = fn(m); saveMeta(n).catch(() => {}); return n; });
  }, []);

  const saveRule = useCallback(async (draft: RuleDraft, name?: string) => {
    if (!client) throw new Error('Sem conexão com o hub');
    let id = draft.id;
    if (id) await client.updateRule(id, draft);
    else id = (await client.createRule(draft)).id;
    if (name !== undefined) {
      const key = String(id);
      updateMeta((m) => {
        const ruleNames = { ...m.ruleNames };
        if (name.trim()) ruleNames[key] = name.trim(); else delete ruleNames[key];
        return { ...m, ruleNames };
      });
    }
    slow();
    return id as number;
  }, [client, slow, updateMeta]);

  const setRuleEnabled = useCallback(async (rule: HubRule, enabled: boolean) => {
    if (!client) throw new Error('Sem conexão com o hub');
    const { id, configured, ...rest } = rule;
    setRules((rs) => rs.map((r) => (r.id === id ? { ...r, enabled } : r)));
    try { await client.updateRule(id, { ...rest, id, enabled }); }
    catch (e) { slow(); throw e; }
    slow();
  }, [client, slow]);

  const deleteRule = useCallback(async (id: number) => {
    if (!client) throw new Error('Sem conexão com o hub');
    await client.deleteRule(id);
    updateMeta((m) => { const ruleNames = { ...m.ruleNames }; delete ruleNames[String(id)]; return { ...m, ruleNames }; });
    slow();
  }, [client, slow, updateMeta]);

  const displayName = useCallback((id: string) => meta.aliases[id] ?? prettify(id), [meta.aliases]);
  const ruleName = useCallback((r: HubRule) => meta.ruleNames[String(r.id)] ?? `Automação ${r.id}`, [meta.ruleNames]);

  const runScene = useCallback(async (s: Pick<Scene, 'steps'>) => {
    let ok = 0, fail = 0;
    for (const step of s.steps) {
      try { await sendCommand(step.device, step.action); ok++; } catch { fail++; }
    }
    return { ok, fail };
  }, [sendCommand]);

  const value: HubCtx = {
    ready, connection, client, link, status, devices, rules, history, historyAvailable, config, meta, lastSync,
    connect, disconnect, refresh, refreshFast: fast,
    sendCommand, toggleDevice, removeDevice, permitJoin,
    saveRule, setRuleEnabled, deleteRule,
    updateMeta, displayName, ruleName, runScene,
  };
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}
