export interface ValidationSection {
  name: string;
  status: 'PASS' | 'FAIL' | 'UNKNOWN';
  details: string;
}

function escapeRegex(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

export function parseValidationSections(report: string | null | undefined): ValidationSection[] {
  if (!report) {
    return [];
  }

  const sectionNames = [
    'Baseline Activity',
    'E/I Balance',
    'Dose Response',
    'Temporal Evolution',
    'Calcium Block',
    'Synaptic Disruption'
  ];

  return sectionNames.map((name) => {
    const escapedName = escapeRegex(name);
    const regex = new RegExp(`\\[${escapedName}[^\\]]*\\]([\\s\\S]*?)(?=\\n\\s*\\[[^\\]]+\\]|$)`, 'i');
    const match = report.match(regex);
    const chunk = match?.[1] ?? '';
    const statusMatch = chunk.match(/Status\s*:\s*(PASS|FAIL)/i);
    return {
      name,
      status: (statusMatch?.[1]?.toUpperCase() as 'PASS' | 'FAIL') ?? 'UNKNOWN',
      details: chunk.trim()
    };
  });
}
