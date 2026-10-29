export function prettify(id: string) {
  const s = id.replace(/[_-]+/g, ' ').trim();
  return s ? s.charAt(0).toUpperCase() + s.slice(1) : id;
}

export function formatDuration(sec: number) {
  if (sec < 60) return `${sec} s`;
  if (sec < 3600) {
    const m = Math.floor(sec / 60), s = sec % 60;
    return s ? `${m} min ${s} s` : `${m} min`;
  }
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
  return m ? `${h} h ${m} min` : `${h} h`;
}

export function formatUptime(sec: number) {
  const d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600), m = Math.floor((sec % 3600) / 60);
  if (d > 0) return `${d} d ${h} h`;
  if (h > 0) return `${h} h ${m} min`;
  if (m > 0) return `${m} min`;
  return `${Math.floor(sec)} s`;
}

/** O firmware só tem "segundos desde o boot"; convertemos para "há X" usando o uptime atual. */
export function agoFromUptime(entry: number, now: number) {
  const d = Math.max(0, now - entry);
  if (d < 5) return 'agora';
  if (d < 60) return `há ${Math.floor(d)} s`;
  if (d < 3600) return `há ${Math.floor(d / 60)} min`;
  if (d < 86400) return `há ${Math.floor(d / 3600)} h`;
  return `há ${Math.floor(d / 86400)} d`;
}

export const hex16 = (n?: number) => (n === undefined ? '—' : '0x' + n.toString(16).padStart(4, '0'));

export const num = (v: number) => String(Math.round(v * 10) / 10).replace('.', ',');
