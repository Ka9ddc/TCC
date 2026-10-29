import React, { useState } from 'react';
import {
  ActivityIndicator, KeyboardAvoidingView, Modal, Platform, Pressable, RefreshControl,
  ScrollView, StyleProp, Switch, TextInput, TextInputProps, View, ViewStyle,
} from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useRouter } from 'expo-router';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import * as Haptics from 'expo-haptics';
import { T, useTheme } from '../theme/theme';
import { useHub } from '../state/HubProvider';
import { formatDuration } from '../lib/format';

export type IconName = keyof typeof Ionicons.glyphMap;

/* ── Banner ─────────────────────────────────────────────── */
export function Banner({ tone = 'info', icon, title, body, action }: {
  tone?: 'info' | 'warn' | 'danger'; icon?: IconName; title: string; body?: string;
  action?: { label: string; onPress: () => void };
}) {
  const { t } = useTheme();
  const bg = tone === 'danger' ? t.dangerSoft : tone === 'warn' ? t.lampSoft : t.signalSoft;
  const fg = tone === 'danger' ? t.danger : tone === 'warn' ? t.lampText : t.signal;
  return (
    <View style={{ backgroundColor: bg, borderRadius: 18, padding: 14, marginBottom: 16, flexDirection: 'row', gap: 12 }}>
      <Ionicons name={icon ?? 'information-circle-outline'} size={22} color={fg} style={{ marginTop: 1 }} />
      <View style={{ flex: 1 }}>
        <T v="h3" style={{ color: fg }}>{title}</T>
        {body ? <T v="small" w={400} style={{ color: fg, marginTop: 2 }}>{body}</T> : null}
        {action ? (
          <Pressable onPress={action.onPress} hitSlop={8} style={{ marginTop: 8, alignSelf: 'flex-start' }}>
            <T v="small" w={700} style={{ color: fg, textDecorationLine: 'underline' }}>{action.label}</T>
          </Pressable>
        ) : null}
      </View>
    </View>
  );
}

export function LinkBanner() {
  const { link, connection, refresh } = useHub();
  const router = useRouter();
  if (link === 'offline') {
    return (
      <Banner
        tone="danger" icon="cloud-offline-outline" title="Sem conexão com o hub"
        body={`Não consegui falar com ${connection?.host}. Confira se o celular está na rede MeuHub-IoT ou na mesma rede do hub.`}
        action={{ label: 'Tentar de novo', onPress: () => { refresh(); } }}
      />
    );
  }
  if (link === 'unauthorized') {
    return (
      <Banner
        tone="danger" icon="key-outline" title="Token recusado"
        body="O hub respondeu, mas não aceitou o token salvo neste celular."
        action={{ label: 'Informar o token novamente', onPress: () => router.push({ pathname: '/connect', params: { edit: '1' } }) }}
      />
    );
  }
  return null;
}

/* ── Screen / Header ────────────────────────────────────── */
export function Screen({ children, tab = false, refreshing, onRefresh, footer }: {
  children: React.ReactNode; tab?: boolean; refreshing?: boolean; onRefresh?: () => void; footer?: React.ReactNode;
}) {
  const { t } = useTheme();
  const insets = useSafeAreaInsets();
  return (
    <KeyboardAvoidingView style={{ flex: 1, backgroundColor: t.bg }} behavior={Platform.OS === 'ios' ? 'padding' : undefined}>
      <ScrollView
        keyboardShouldPersistTaps="handled"
        showsVerticalScrollIndicator={false}
        contentContainerStyle={{
          paddingTop: insets.top + 12, paddingHorizontal: 20,
          paddingBottom: (tab ? 0 : footer ? 0 : insets.bottom) + 32,
        }}
        refreshControl={onRefresh ? <RefreshControl refreshing={!!refreshing} onRefresh={onRefresh} tintColor={t.inkMuted} /> : undefined}
      >
        <LinkBanner />
        {children}
      </ScrollView>
      {footer ? (
        <View style={{ paddingHorizontal: 20, paddingTop: 12, paddingBottom: insets.bottom + 12, backgroundColor: t.bg, borderTopWidth: 1, borderTopColor: t.line, gap: 10 }}>
          {footer}
        </View>
      ) : null}
    </KeyboardAvoidingView>
  );
}

export function Header({ title, subtitle, back, right }: {
  title: string; subtitle?: string; back?: boolean; right?: React.ReactNode;
}) {
  const router = useRouter();
  const { t } = useTheme();
  return (
    <View style={{ marginBottom: 20 }}>
      {back ? (
        <Pressable
          onPress={() => (router.canGoBack() ? router.back() : router.replace('/'))}
          hitSlop={12}
          style={{ width: 40, height: 40, borderRadius: 20, backgroundColor: t.surface, borderWidth: 1, borderColor: t.line, alignItems: 'center', justifyContent: 'center', marginBottom: 14 }}
        >
          <Ionicons name="chevron-back" size={20} color={t.ink} />
        </Pressable>
      ) : null}
      <View style={{ flexDirection: 'row', alignItems: 'flex-end', justifyContent: 'space-between', gap: 12 }}>
        <View style={{ flex: 1 }}>
          <T v="display">{title}</T>
          {subtitle ? <T v="body" c="inkSoft" style={{ marginTop: 4 }}>{subtitle}</T> : null}
        </View>
        {right}
      </View>
    </View>
  );
}

export function IconButton({ icon, onPress, label }: { icon: IconName; onPress: () => void; label: string }) {
  const { t } = useTheme();
  return (
    <Pressable
      accessibilityLabel={label} onPress={onPress} hitSlop={8}
      style={({ pressed }) => ({ width: 44, height: 44, borderRadius: 22, backgroundColor: t.surface, borderWidth: 1, borderColor: t.line, alignItems: 'center', justifyContent: 'center', opacity: pressed ? 0.7 : 1 })}
    >
      <Ionicons name={icon} size={20} color={t.ink} />
    </Pressable>
  );
}

/* ── Card / Row / Section ───────────────────────────────── */
export function Card({ children, style, onPress }: { children: React.ReactNode; style?: StyleProp<ViewStyle>; onPress?: () => void }) {
  const { t } = useTheme();
  const base: ViewStyle = { backgroundColor: t.surface, borderRadius: 22, padding: 16, borderWidth: 1, borderColor: t.line };
  if (onPress) {
    return <Pressable onPress={onPress} style={({ pressed }) => [base, style, pressed && { opacity: 0.85 }]}>{children}</Pressable>;
  }
  return <View style={[base, style]}>{children}</View>;
}

export function Row({ icon, title, subtitle, right, onPress, danger, last }: {
  icon?: IconName; title: string; subtitle?: string; right?: React.ReactNode; onPress?: () => void; danger?: boolean; last?: boolean;
}) {
  const { t } = useTheme();
  const color = danger ? t.danger : t.ink;
  const inner = (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 12, borderBottomWidth: last ? 0 : 1, borderBottomColor: t.line }}>
      {icon ? (
        <View style={{ width: 38, height: 38, borderRadius: 12, backgroundColor: danger ? t.dangerSoft : t.surfaceAlt, alignItems: 'center', justifyContent: 'center' }}>
          <Ionicons name={icon} size={19} color={color} />
        </View>
      ) : null}
      <View style={{ flex: 1 }}>
        <T v="body" w={600} style={{ color }}>{title}</T>
        {subtitle ? <T v="small" c="inkMuted" w={400}>{subtitle}</T> : null}
      </View>
      {right ?? (onPress ? <Ionicons name="chevron-forward" size={18} color={t.inkMuted} /> : null)}
    </View>
  );
  return onPress ? <Pressable onPress={onPress} style={({ pressed }) => ({ opacity: pressed ? 0.6 : 1 })}>{inner}</Pressable> : inner;
}

export function SectionTitle({ title, action, onAction }: { title: string; action?: string; onAction?: () => void }) {
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', marginTop: 28, marginBottom: 12 }}>
      <T v="title">{title}</T>
      {action ? (
        <Pressable onPress={onAction} hitSlop={10}><T v="small" c="signal" w={700}>{action}</T></Pressable>
      ) : null}
    </View>
  );
}

export function Empty({ icon, title, body, action }: { icon: IconName; title: string; body: string; action?: React.ReactNode }) {
  const { t } = useTheme();
  return (
    <View style={{ alignItems: 'center', padding: 24, borderRadius: 22, borderWidth: 1.5, borderStyle: 'dashed', borderColor: t.line, gap: 8 }}>
      <Ionicons name={icon} size={30} color={t.inkMuted} />
      <T v="h3" style={{ textAlign: 'center' }}>{title}</T>
      <T v="small" c="inkSoft" w={400} style={{ textAlign: 'center', marginBottom: 8 }}>{body}</T>
      {action}
    </View>
  );
}

export const Dot = ({ on, size = 8 }: { on: boolean; size?: number }) => {
  const { t } = useTheme();
  return <View style={{ width: size, height: size, borderRadius: size / 2, backgroundColor: on ? t.signal : t.inkMuted }} />;
};

/* ── Controles ──────────────────────────────────────────── */
export function Button({ label, onPress, variant = 'primary', icon, loading, disabled, small, style }: {
  label: string; onPress: () => void; variant?: 'primary' | 'soft' | 'danger' | 'ghost'; icon?: IconName;
  loading?: boolean; disabled?: boolean; small?: boolean; style?: StyleProp<ViewStyle>;
}) {
  const { t } = useTheme();
  const bg = variant === 'primary' ? t.ink : variant === 'danger' ? t.danger : variant === 'soft' ? t.surfaceAlt : 'transparent';
  const fg = variant === 'primary' ? t.onInk : variant === 'danger' ? '#FFFFFF' : t.ink;
  return (
    <Pressable
      disabled={disabled || loading}
      onPress={() => { Haptics.selectionAsync().catch(() => {}); onPress(); }}
      style={({ pressed }) => [{
        height: small ? 40 : 52, paddingHorizontal: small ? 16 : 22, borderRadius: 999, backgroundColor: bg,
        flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8,
        opacity: disabled ? 0.4 : pressed ? 0.85 : 1,
      }, style]}
    >
      {loading ? <ActivityIndicator color={fg} /> : (
        <>
          {icon ? <Ionicons name={icon} size={18} color={fg} /> : null}
          <T v="body" w={600} style={{ color: fg }}>{label}</T>
        </>
      )}
    </Pressable>
  );
}

export function Field({ label, hint, error, right, ...props }: TextInputProps & {
  label: string; hint?: string; error?: string | null; right?: React.ReactNode;
}) {
  const { t } = useTheme();
  const [focus, setFocus] = useState(false);
  return (
    <View style={{ marginBottom: 16 }}>
      <T v="small" c="inkSoft" style={{ marginBottom: 6 }}>{label}</T>
      <View style={{
        flexDirection: 'row', alignItems: 'center', backgroundColor: t.surface, borderRadius: 14, height: 52,
        borderWidth: 1.5, borderColor: error ? t.danger : focus ? t.ink : t.line, paddingHorizontal: 14,
      }}>
        <TextInput
          autoCapitalize="none" autoCorrect={false} placeholderTextColor={t.inkMuted}
          {...props}
          onFocus={(e) => { setFocus(true); props.onFocus?.(e); }}
          onBlur={(e) => { setFocus(false); props.onBlur?.(e); }}
          style={{ flex: 1, color: t.ink, fontFamily: 'Bricolage-500', fontSize: 16 }}
        />
        {right}
      </View>
      {error ? <T v="small" c="danger" w={500} style={{ marginTop: 6 }}>{error}</T>
        : hint ? <T v="small" c="inkMuted" w={400} style={{ marginTop: 6 }}>{hint}</T> : null}
    </View>
  );
}

export function Chip({ label, active, onPress, icon }: { label: string; active?: boolean; onPress: () => void; icon?: IconName }) {
  const { t } = useTheme();
  return (
    <Pressable
      onPress={onPress}
      style={({ pressed }) => ({
        height: 38, paddingHorizontal: 14, borderRadius: 999, flexDirection: 'row', alignItems: 'center', gap: 6,
        backgroundColor: active ? t.ink : t.surface, borderWidth: 1, borderColor: active ? t.ink : t.line, opacity: pressed ? 0.8 : 1,
      })}
    >
      {icon ? <Ionicons name={icon} size={16} color={active ? t.onInk : t.ink} /> : null}
      <T v="small" w={600} style={{ color: active ? t.onInk : t.ink }}>{label}</T>
    </Pressable>
  );
}

export function Segmented<V extends string>({ options, value, onChange }: {
  options: { value: V; label: string }[]; value: V; onChange: (v: V) => void;
}) {
  const { t } = useTheme();
  return (
    <View style={{ flexDirection: 'row', backgroundColor: t.surfaceAlt, borderRadius: 14, padding: 4 }}>
      {options.map((o) => {
        const a = o.value === value;
        return (
          <Pressable key={o.value} onPress={() => onChange(o.value)}
            style={{ flex: 1, height: 40, borderRadius: 11, alignItems: 'center', justifyContent: 'center', backgroundColor: a ? t.surface : 'transparent' }}>
            <T v="small" w={a ? 700 : 500} c={a ? 'ink' : 'inkSoft'}>{o.label}</T>
          </Pressable>
        );
      })}
    </View>
  );
}

export function Toggle({ value, onValueChange }: { value: boolean; onValueChange: (v: boolean) => void }) {
  const { t } = useTheme();
  return <Switch value={value} onValueChange={onValueChange} trackColor={{ false: t.line, true: t.signal }} thumbColor="#FFFFFF" />;
}

/** Escolha de tempo por atalhos (0 = nenhum). */
export function PresetPicker({ value, presets, onChange, zeroLabel = 'Nenhum' }: {
  value: number; presets: number[]; onChange: (v: number) => void; zeroLabel?: string;
}) {
  const list = presets.includes(value) ? presets : [...presets, value].sort((a, b) => a - b);
  return (
    <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
      {list.map((p) => (
        <Chip key={p} label={p === 0 ? zeroLabel : formatDuration(p)} active={p === value} onPress={() => onChange(p)} />
      ))}
    </ScrollView>
  );
}

export function PickerModal<V extends string>({ visible, title, options, value, onSelect, onClose }: {
  visible: boolean; title: string; value?: V; onSelect: (v: V) => void; onClose: () => void;
  options: { value: V; title: string; subtitle?: string; icon?: IconName }[];
}) {
  const { t } = useTheme();
  const insets = useSafeAreaInsets();
  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <View style={{ flex: 1, justifyContent: 'flex-end' }}>
        <Pressable style={{ position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, backgroundColor: 'rgba(8,12,22,0.5)' }} onPress={onClose} />
        <View style={{ backgroundColor: t.surface, borderTopLeftRadius: 28, borderTopRightRadius: 28, padding: 20, paddingBottom: insets.bottom + 20, maxHeight: '70%' }}>
          <T v="title" style={{ marginBottom: 8 }}>{title}</T>
          {options.length === 0 ? <T v="body" c="inkSoft">Nenhuma opção disponível.</T> : null}
          <ScrollView>
            {options.map((o, i) => (
              <Row
                key={o.value} icon={o.icon} title={o.title} subtitle={o.subtitle} last={i === options.length - 1}
                onPress={() => { onSelect(o.value); onClose(); }}
                right={o.value === value ? <Ionicons name="checkmark-circle" size={22} color={t.signal} /> : undefined}
              />
            ))}
          </ScrollView>
        </View>
      </View>
    </Modal>
  );
}
