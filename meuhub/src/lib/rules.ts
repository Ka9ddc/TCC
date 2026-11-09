import { HubDevice, HubRule, RuleAction, RuleOperator } from '../api/types';
import { formatDuration, num } from './format';

export const OPERATORS: { value: RuleOperator; label: string; phrase: string }[] = [
  { value: 'greater_than', label: 'acima de', phrase: 'estiver acima de' },
  { value: 'less_than', label: 'abaixo de', phrase: 'estiver abaixo de' },
  { value: 'greater_or_equal', label: 'pelo menos', phrase: 'estiver em pelo menos' },
  { value: 'less_or_equal', label: 'no máximo', phrase: 'estiver em no máximo' },
  { value: 'equals', label: 'igual a', phrase: 'for igual a' },
  { value: 'not_equals', label: 'diferente de', phrase: 'for diferente de' },
];

export const isOnOff = (d?: HubDevice) => !!d && d.type === 'on_off';

/** Para gatilhos booleanos: a regra dispara quando o dispositivo vai para "ligado"? */
export function boolTriggerIsOn(r: { operator: RuleOperator; trigger_value: boolean | number }) {
  const v = !!r.trigger_value;
  return r.operator === 'not_equals' ? !v : v;
}

export function describeTrigger(r: HubRule, name: (id: string) => string, unit?: string) {
  const who = name(r.trigger_device);
  if (typeof r.trigger_value === 'boolean') return `${who} ${boolTriggerIsOn(r) ? 'ligar' : 'desligar'}`;
  const op = OPERATORS.find((o) => o.value === r.operator)?.phrase ?? r.operator;
  return `${who} ${op} ${num(r.trigger_value)}${unit ? ` ${unit}` : ''}`;
}

export function describeTimeWindow(r: Pick<HubRule, 'start_time' | 'end_time'>): string | null {
  if (!r.start_time || !r.end_time) return null;
  return `das ${r.start_time} às ${r.end_time}`;
}

export function describeAction(a: RuleAction, name: (id: string) => string) {
  const parts = [`${a.action === 'on' ? 'ligar' : 'desligar'} ${name(a.device)}`];
  if (a.delay_seconds) parts.push(`após ${formatDuration(a.delay_seconds)}`);
  if (a.duration_seconds) parts.push(`por ${formatDuration(a.duration_seconds)}`);
  return parts.join(' ');
}

/** Um valor que satisfaz (ou não) a condição, para o botão "Testar". */
export function sampleValue(op: RuleOperator, v: number, satisfy: boolean): number {
  const up = v + 1, down = v - 1;
  switch (op) {
    case 'greater_than': return satisfy ? up : v;
    case 'less_than': return satisfy ? down : v;
    case 'greater_or_equal': return satisfy ? v : down;
    case 'less_or_equal': return satisfy ? v : up;
    case 'equals': return satisfy ? v : up;
    case 'not_equals': return satisfy ? up : v;
  }
}
