import React, { createContext, useContext, useMemo } from 'react';
import { Text as RNText, TextProps, useColorScheme } from 'react-native';
import { useHub } from '../state/HubProvider';

export interface Palette {
  bg: string; surface: string; surfaceAlt: string; line: string;
  ink: string; inkSoft: string; inkMuted: string; onInk: string;
  lamp: string; lampInk: string; lampSoft: string; lampText: string;
  signal: string; signalSoft: string; danger: string; dangerSoft: string;
}

export const lightPalette: Palette = {
  bg: '#EEF0F3', surface: '#FFFFFF', surfaceAlt: '#E4E8EE', line: '#DADFE7',
  ink: '#172033', inkSoft: '#4A566C', inkMuted: '#6F7C92', onInk: '#FFFFFF',
  lamp: '#FFB224', lampInk: '#3B2600', lampSoft: '#FFF1D6', lampText: '#7A4B00',
  signal: '#0A7C78', signalSoft: '#D8F1EF', danger: '#D13237', dangerSoft: '#FCE3E3',
};
export const darkPalette: Palette = {
  bg: '#0E1524', surface: '#172136', surfaceAlt: '#212D47', line: '#2A3754',
  ink: '#EEF2FA', inkSoft: '#B2BDD2', inkMuted: '#8794AE', onInk: '#0E1524',
  lamp: '#FFB224', lampInk: '#2B1B00', lampSoft: '#3A2D12', lampText: '#FFB224',
  signal: '#35C9C2', signalSoft: '#12343A', danger: '#FF6B6F', dangerSoft: '#3C1D24',
};

interface ThemeCtx { t: Palette; dark: boolean }
const Ctx = createContext<ThemeCtx>({ t: lightPalette, dark: false });
export const useTheme = () => useContext(Ctx);

export function ThemeProvider({ children }: { children: React.ReactNode }) {
  const { meta } = useHub();
  const system = useColorScheme();
  const dark = meta.themeMode === 'dark' || (meta.themeMode === 'auto' && system === 'dark');
  const value = useMemo(() => ({ t: dark ? darkPalette : lightPalette, dark }), [dark]);
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

type Weight = 400 | 500 | 600 | 700 | 800;
export type Variant = 'display' | 'title' | 'h3' | 'body' | 'small' | 'num';
const VARIANTS: Record<Variant, { size: number; w: Weight; lh: number; ls?: number }> = {
  display: { size: 34, w: 800, lh: 38, ls: -0.8 },
  title: { size: 24, w: 700, lh: 30, ls: -0.4 },
  h3: { size: 17, w: 600, lh: 22 },
  body: { size: 15, w: 400, lh: 22 },
  small: { size: 13, w: 500, lh: 18 },
  num: { size: 44, w: 800, lh: 48, ls: -1 },
};

export function T({ v = 'body', c = 'ink', w, style, ...rest }:
  TextProps & { v?: Variant; c?: keyof Palette; w?: Weight }) {
  const { t } = useTheme();
  const s = VARIANTS[v];
  return (
    <RNText
      {...rest}
      style={[{ fontFamily: `Bricolage-${w ?? s.w}`, fontSize: s.size, lineHeight: s.lh, letterSpacing: s.ls, color: t[c] }, style]}
    />
  );
}
