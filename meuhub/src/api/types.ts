export type RuleOperator =
  | 'equals' | 'not_equals' | 'greater_than' | 'less_than'
  | 'greater_or_equal' | 'less_or_equal';

/** GET /api/devices */
export interface HubDevice {
  id: string;
  type: 'on_off' | 'illuminance' | string;
  protocol: 'zigbee' | 'wifi' | string;
  online: boolean;
  state?: boolean;
  short_address?: number;
  endpoint?: number;
  ieee_address?: string;
  value?: number;
  unit?: string;
  last_seen_seconds?: number;
  /** Sugestão de firmware: nome do evento que o sensor publica. */
  event?: string;
}

/** GET /api/status */
export interface HubStatus {
  status: string;
  uptime_seconds: number;
  events_received: number;
  rules_matched: number;
  automation_commands: number;
  uart_success_responses: number;
  uart_error_responses: number;
  uart_invalid_messages: number;
  uart_unknown_responses: number;
  uart_duplicate_responses: number;
  command_timeouts: number;
  command_latency_samples?: number;
  command_latency_last_ms?: number;
  command_latency_min_ms?: number;
  command_latency_max_ms?: number;
  command_latency_avg_ms?: number;
  pending_commands: number;
  history_entries: number;
  configured_rules: number;
  enabled_rules: number;
  time_synced?: boolean;
  timezone?: string;
  local_time?: string;
  device_discovered: boolean;
  device_online: boolean;
  wifi_sensor_discovered: boolean;
  wifi_sensor_online: boolean;
}

export interface RuleAction {
  device: string;
  action: 'on' | 'off';
  delay_seconds?: number;
  duration_seconds?: number;
}

/** GET /api/rules */
export interface HubRule {
  id: number;
  enabled: boolean;
  configured?: boolean;
  trigger_device: string;
  trigger_event: string;
  trigger_value: boolean | number;
  operator: RuleOperator;
  actions: RuleAction[];
  start_time?: string;
  end_time?: string;
}
export type RuleDraft = Omit<HubRule, 'id' | 'configured'> & { id?: number };

/** GET /api/history */
export interface HistoryEntry {
  uptime_seconds: number;
  type: 'event' | 'command' | string;
  device: string;
  name: string;
  value?: boolean | number;
}

/** GET /api/config */
export interface HubConfig {
  sta_ssid: string;
  sta_connected: boolean;
  ap_ssid: string;
  ap_connected_clients: number;
  ap_enabled: boolean;
  restart_required: boolean;
  time_synced?: boolean;
  timezone?: string;
  local_time?: string;
}

export interface CommandResult {
  status: 'success' | 'error' | 'timeout';
  request_id: number;
  device?: string;
}

export interface EventResult {
  status: string;
  source: 'wifi' | 'test';
  accepted: boolean;
  rule_matched: boolean;
}
