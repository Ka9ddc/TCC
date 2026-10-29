import { HistoryEntry } from '../api/types';
import { IconName } from '../components/ui';
import { num } from './format';

export function describeHistory(h: HistoryEntry, name: (id: string) => string): { title: string; icon: IconName } {
  const who = name(h.device);
  if (h.type === 'command') {
    if (h.name === 'on') return { title: `Ligar ${who}`, icon: 'arrow-forward-circle-outline' };
    if (h.name === 'off') return { title: `Desligar ${who}`, icon: 'arrow-forward-circle-outline' };
    if (h.name === 'remove_device') return { title: `Remover ${who}`, icon: 'trash-outline' };
    if (h.name === 'permit') return { title: 'Abrir rede Zigbee', icon: 'radio-outline' };
    return { title: `${h.name} → ${who}`, icon: 'arrow-forward-circle-outline' };
  }
  if (typeof h.value === 'boolean') {
    return { title: `${who} ${h.value ? 'ligou' : 'desligou'}`, icon: 'pulse' };
  }
  if (typeof h.value === 'number') return { title: `${who}: ${num(h.value)}`, icon: 'pulse' };
  return { title: `${who} · ${h.name}`, icon: 'pulse' };
}
