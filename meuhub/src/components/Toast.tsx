import React, { createContext, useCallback, useContext, useRef, useState } from 'react';
import { Animated } from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { T, useTheme } from '../theme/theme';

type Tone = 'ok' | 'error' | 'info';
const Ctx = createContext<{ show: (msg: string, tone?: Tone) => void }>({ show: () => {} });
export const useToast = () => useContext(Ctx);

export function ToastProvider({ children }: { children: React.ReactNode }) {
  const { t } = useTheme();
  const insets = useSafeAreaInsets();
  const [msg, setMsg] = useState<{ text: string; tone: Tone } | null>(null);
  const anim = useRef(new Animated.Value(0)).current;
  const timer = useRef<ReturnType<typeof setTimeout>>();

  const show = useCallback((text: string, tone: Tone = 'info') => {
    setMsg({ text, tone });
    if (timer.current) clearTimeout(timer.current);
    Animated.timing(anim, { toValue: 1, duration: 180, useNativeDriver: true }).start();
    timer.current = setTimeout(() => {
      Animated.timing(anim, { toValue: 0, duration: 220, useNativeDriver: true }).start(() => setMsg(null));
    }, tone === 'error' ? 4200 : 2400);
  }, [anim]);

  const bg = msg?.tone === 'error' ? t.danger : t.ink;
  const fg = msg?.tone === 'error' ? '#FFFFFF' : t.onInk;
  const icon = msg?.tone === 'error' ? 'alert-circle' : msg?.tone === 'ok' ? 'checkmark-circle' : 'information-circle';

  return (
    <Ctx.Provider value={{ show }}>
      {children}
      {msg && (
        <Animated.View
          pointerEvents="none"
          style={{
            position: 'absolute', left: 20, right: 20, bottom: insets.bottom + 88,
            opacity: anim, transform: [{ translateY: anim.interpolate({ inputRange: [0, 1], outputRange: [12, 0] }) }],
            backgroundColor: bg, borderRadius: 16, paddingVertical: 12, paddingHorizontal: 14,
            flexDirection: 'row', alignItems: 'center', gap: 10,
          }}
        >
          <Ionicons name={icon as any} size={20} color={fg} />
          <T v="small" w={600} style={{ color: fg, flex: 1 }}>{msg.text}</T>
        </Animated.View>
      )}
    </Ctx.Provider>
  );
}
