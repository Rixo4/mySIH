import type {
  DrugEvaluationVisualizationData,
  DoseResultPoint,
  RasterSpikePoint,
  ReportChartPoint,
  TimelineSegment,
  VoltageTracePoint
} from '../types';

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export function formatRangeLabel(start?: number | null, end?: number | null): string {
  if (start == null || end == null) {
    return '—';
  }
  return `${start.toFixed(2)} to ${end.toFixed(2)}`;
}

export function getVisualizationData(
  response: DrugEvaluationVisualizationData | null | undefined,
  fallback: DrugEvaluationVisualizationData
): DrugEvaluationVisualizationData {
  return response ?? fallback;
}

export function buildFallbackVisualizationData(
  chartData: ReportChartPoint[],
  payload: { dose_range: { min: number; max: number; step: number } },
  responseMode: string,
  summary: {
    toxicThreshold?: string | null;
    therapeuticRange?: string | null;
    stabilizationRange?: string | null;
  }
): DrugEvaluationVisualizationData {
  const minDose = payload.dose_range.min;
  const maxDose = payload.dose_range.max;
  const step = payload.dose_range.step > 0 ? payload.dose_range.step : 1;
  const normalizedMode = (responseMode || '').trim().toUpperCase();
  const lowResponse = normalizedMode === 'NO_SIGNIFICANT_RESPONSE';
  const doseResults: DoseResultPoint[] = chartData.map((point, index) => ({
    dose: point.dose,
    effect: point.effect,
    firing_rate: lowResponse ? clamp(18.0 - point.effect * 0.08 - index * 0.03, 15.5, 20.0) : Math.max(4, responseMode === 'SUPPRESSIVE_RESPONSE' ? 22 - point.dose * 1.1 : 14 + point.dose * 0.9),
    sync: lowResponse ? clamp(0.28 - point.effect / 500, 0.22, 0.31) : clamp(0.34 - point.risk / 700, 0.03, 1),
    nii: lowResponse ? clamp(0.11 + point.effect / 700, 0.08, 0.16) : clamp(point.risk / 180, 0.02, 1),
    seizure_score: point.risk,
    toxicity_score: point.toxicity,
    variance: lowResponse ? Math.max(0.08, point.toxicity / 180) : Math.max(0.1, point.toxicity / 100),
    response_mode: normalizedMode || 'NO_SIGNIFICANT_RESPONSE',
    biological_state: 'LIMITED_EFFECT'
  }));

  const toxicThreshold = Number.parseFloat(summary.toxicThreshold ?? '') || maxDose * 0.85;

  const voltageTrace: VoltageTracePoint[] = Array.from({ length: 180 }, (_, index) => ({
    time: index * 0.75,
    voltage: lowResponse
      ? -68 + Math.sin(index / 7) * 3.2
      : -68 + Math.sin(index / 4) * (responseMode === 'EXCITATORY_RESPONSE' ? 18 : 12)
  }));

  const rasterSpikes: RasterSpikePoint[] = Array.from({ length: 84 }, (_, index) => ({
    neuron_id: (index % 14) + 1,
    spike_time: index * (lowResponse ? 16 : responseMode === 'EXCITATORY_RESPONSE' ? 8 : 12)
  }));

  const classificationTimeline: TimelineSegment[] = [];

  return {
    dose_results: doseResults,
    voltage_trace: voltageTrace,
    raster_spikes: rasterSpikes,
    classification_timeline: classificationTimeline,
    reference_points: {
      toxic_threshold: toxicThreshold,
      therapeutic_min: undefined,
      therapeutic_max: undefined,
      ic50: doseResults[0]?.ic50_na,
      active_zone: 'NO_VALID_WINDOW',
      has_valid_therapeutic_window: false
    }
  };
}

export function downloadCsv(rows: DoseResultPoint[], filename: string) {
  const headers = ['dose', 'effect', 'firing_rate', 'sync', 'nii', 'seizure_score', 'toxicity_score', 'variance', 'response_mode', 'biological_state'];
  const csv = [headers.join(',')]
    .concat(
      rows.map((row) =>
        headers
          .map((header) => {
            const value = row[header as keyof DoseResultPoint];
            return typeof value === 'string' ? `"${value.replaceAll('"', '""')}"` : String(value ?? '');
          })
          .join(',')
      )
    )
    .join('\n');

  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8' });
  const url = window.URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  window.URL.revokeObjectURL(url);
}

function createMinimalPdf(text: string): string {
  const lines = text.split(/\r?\n/).slice(0, 180);
  const escapedLines = lines.map((line) => line.replace(/\\/g, '\\\\').replace(/\(/g, '\\(').replace(/\)/g, '\\)'));
  const content: string[] = ['BT', '/F1 10 Tf', '72 760 Td'];

  escapedLines.forEach((line, index) => {
    if (index > 0) {
      content.push('T*');
    }
    content.push(`(${line}) Tj`);
  });

  content.push('ET');
  const stream = content.join('\n');
  const objects = [
    '1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n',
    '2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n',
    '3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n',
    '4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n',
    `5 0 obj\n<< /Length ${stream.length} >>\nstream\n${stream}\nendstream\nendobj\n`
  ];

  const header = '%PDF-1.4\n';
  let offset = header.length;
  const offsets = ['0000000000 65535 f \n'];
  for (const object of objects) {
    offsets.push(`${String(offset).padStart(10, '0')} 00000 n \n`);
    offset += object.length;
  }

  const xrefStart = offset;
  const xref = `xref\n0 6\n${offsets.join('')}trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n${xrefStart}\n%%EOF`;
  return header + objects.join('') + xref;
}

export function downloadPdf(text: string, filename: string) {
  const pdf = createMinimalPdf(text);
  const blob = new Blob([pdf], { type: 'application/pdf' });
  const url = window.URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  window.URL.revokeObjectURL(url);
}

export function downloadTextReport(text: string, filename: string) {
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = window.URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  window.URL.revokeObjectURL(url);
}

export async function downloadSvgAsPng(element: HTMLElement, filename: string) {
  const svg = element.querySelector('svg');
  if (!svg) {
    return;
  }

  const serializer = new XMLSerializer();
  const svgText = serializer.serializeToString(svg);
  const svgBlob = new Blob([svgText], { type: 'image/svg+xml;charset=utf-8' });
  const svgUrl = window.URL.createObjectURL(svgBlob);
  const image = new Image();
  const canvas = document.createElement('canvas');
  canvas.width = Math.max(1, element.clientWidth * 2);
  canvas.height = Math.max(1, element.clientHeight * 2);

  await new Promise<void>((resolve, reject) => {
    image.onload = () => {
      const context = canvas.getContext('2d');
      if (!context) {
        reject(new Error('Canvas unavailable'));
        return;
      }

      context.fillStyle = '#07111f';
      context.fillRect(0, 0, canvas.width, canvas.height);
      context.drawImage(image, 0, 0, canvas.width, canvas.height);
      canvas.toBlob((png) => {
        if (!png) {
          reject(new Error('PNG export failed'));
          return;
        }

        const pngUrl = window.URL.createObjectURL(png);
        const anchor = document.createElement('a');
        anchor.href = pngUrl;
        anchor.download = filename;
        anchor.click();
        window.URL.revokeObjectURL(pngUrl);
        resolve();
      }, 'image/png');
    };
    image.onerror = () => reject(new Error('SVG export failed'));
    image.src = svgUrl;
  });

  window.URL.revokeObjectURL(svgUrl);
}