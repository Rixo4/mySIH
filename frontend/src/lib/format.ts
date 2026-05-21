export function formatDateTime(value?: string | null): string {
  if (!value) {
    return '—';
  }

  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }

  return new Intl.DateTimeFormat('en-US', {
    dateStyle: 'medium',
    timeStyle: 'short'
  }).format(date);
}

export function formatDuration(seconds?: number | null): string {
  if (seconds == null || Number.isNaN(seconds)) {
    return '—';
  }

  if (seconds < 60) {
    return `${seconds.toFixed(1)}s`;
  }

  const minutes = Math.floor(seconds / 60);
  const remaining = Math.round(seconds % 60);
  return `${minutes}m ${String(remaining).padStart(2, '0')}s`;
}

export function formatDecimal(value?: number | null, digits = 2): string {
  if (value == null || Number.isNaN(value)) {
    return '—';
  }
  return value.toFixed(digits);
}

export function getRiskTone(value?: string | null): 'safe' | 'warning' | 'danger' | 'neutral' {
  const normalized = (value ?? '').toUpperCase();
  if (normalized.includes('VALIDATED') || normalized.includes('PROCEED') || normalized.includes('PROMISING') || normalized.includes('LOW') || normalized.includes('PASS')) {
    return 'safe';
  }
  if (normalized.includes('CAUTION') || normalized.includes('MEDIUM')) {
    return 'warning';
  }
  if (normalized.includes('HIGH') || normalized.includes('FAIL') || normalized.includes('NOT RECOMMENDED') || normalized.includes('TOXIC')) {
    return 'danger';
  }
  return 'neutral';
}

export function summarizeRiskLevel(value?: string | null): string {
  if (!value) {
    return 'Unknown';
  }
  return value.toUpperCase();
}

export function humanizeLabel(value: string): string {
  return value
    .replace(/_/g, ' ')
    .replace(/\s+/g, ' ')
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}
