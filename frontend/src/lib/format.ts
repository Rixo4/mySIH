export function formatDateTime(value?: string | null): string {
  if (!value) {
    return '—';
  }

  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }

  // Display times in Indian Standard Time for consistent IST timestamps in the UI.
  return new Intl.DateTimeFormat('en-IN', {
    dateStyle: 'medium',
    timeStyle: 'short',
    timeZone: 'Asia/Kolkata'
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
  if (normalized.includes('VALIDATED') || normalized.includes('PROCEED') || normalized.includes('PROMISING') || normalized.includes('LOW') || normalized.includes('PASS') || normalized.includes('SAFE')) {
    return 'safe';
  }
  if (normalized.includes('CAUTION') || normalized.includes('MEDIUM') || normalized.includes('MODERATE')) {
    return 'warning';
  }
  if (normalized.includes('HIGH') || normalized.includes('FAIL') || normalized.includes('NOT RECOMMENDED') || normalized.includes('TOXIC') || normalized.includes('CRITICAL') || normalized.includes('DANGER')) {
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

export function humanizeEnum(value?: string | null): string {
  if (!value) return '—';
  const clean = String(value).trim();
  if (clean === '—' || clean === '-' || clean === '' || clean.toLowerCase() === 'none' || clean.toLowerCase() === 'null') {
    return '—';
  }

  const overrides: Record<string, string> = {
    'NO_VALID_WINDOW': 'No Valid Window',
    'LIMITED_EFFECT': 'Limited Effect',
    'USER_CONFIG': 'User Drug Config',
    'user_config': 'User Drug Config',
    'DEFAULT_INTERNAL_ENGINE_CONFIG': 'Standard Benchmark Config',
    'default_internal_engine_config': 'Standard Benchmark Config',
    'SUPPRESSIVE_RESPONSE': 'Suppressive Response',
    'EXCITATORY_RESPONSE': 'Excitatory Response',
    'STABILIZING_RESPONSE': 'Stabilizing Response',
    'MIXED_RESPONSE': 'Mixed Response',
    'NO_SIGNIFICANT_RESPONSE': 'No Significant Response',
    'INEFFECTIVE_ZONE': 'Ineffective Zone',
    'THERAPEUTIC_ZONE': 'Therapeutic Zone',
    'OVER_SUPPRESSION_ZONE': 'Over-Suppression Zone',
    'SEVERE_EXCITABILITY_ZONE': 'Severe Excitability Zone',
    'SATURATED_STABILIZATION_ZONE': 'Saturated Stabilization Zone',
    'COMPLETED': 'Completed',
    'RUNNING': 'Running',
    'QUEUED': 'Queued',
    'FAILED': 'Failed',
    'LOW_RISK': 'Low Risk',
    'MODERATE_RISK': 'Moderate Risk',
    'HIGH_RISK': 'High Risk',
    'LOW': 'Low',
    'MODERATE': 'Moderate',
    'HIGH': 'High',
    'CRITICAL': 'Critical',
    'EVALUATED': 'Evaluated',
    'CAUTION': 'Caution',
    'PASS': 'Pass',
    'FAIL': 'Fail',
    'PROMISING': 'Promising',
    'NOT_RECOMMENDED': 'Not Recommended',
  };

  if (overrides[clean]) {
    return overrides[clean];
  }

  return clean
    .replace(/_ZONE$/i, ' Zone')
    .replace(/_/g, ' ')
    .toLowerCase()
    .replace(/\b[a-z]/g, (char) => char.toUpperCase());
}
