import type { NormalizedMarkers } from './chartUtils';
import { formatDoseLabel } from './chartUtils';
import type { TimelineSegment } from '../../types';

interface ResponseZoneTimelineProps {
  segments: TimelineSegment[];
  markers: NormalizedMarkers;
  responseModeLabel: string;
}

export function ResponseZoneTimeline({ segments, markers, responseModeLabel }: ResponseZoneTimelineProps) {
  const hasSegments = segments.length > 0;
  const noResponse = responseModeLabel.toUpperCase().includes('NO SIGNIFICANT RESPONSE');

  return (
    <div className="glass-card overflow-hidden border border-white/10 bg-[rgba(6,12,24,0.88)] shadow-panel">
      <div className="border-b border-white/10 px-6 py-5">
        <p className="text-[11px] uppercase tracking-[0.34em] text-cyan-300/75">Response-zone timeline</p>
        <h4 className="mt-2 text-xl font-semibold tracking-[-0.01em] text-white">Dose-state segmentation</h4>
        <p className="mt-2 text-sm leading-6 text-slate-400">A simple visual summary for demos and non-technical reviewers. Current mode: {responseModeLabel}.</p>
      </div>

      <div className="px-5 py-5">
        <div className="flex flex-wrap gap-2 text-xs text-slate-300">
          <span className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5">IC50: {formatDoseLabel(markers.ic50)}</span>
          <span className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5">Onset: {markers.onsetDose != null ? formatDoseLabel(markers.onsetDose) : 'Not observed'}</span>
          {markers.toxicThreshold != null ? (
            <span className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5">Toxic threshold: {formatDoseLabel(markers.toxicThreshold)}</span>
          ) : null}
        </div>

        <div className="mt-4 overflow-hidden rounded-[1.5rem] border border-white/10 bg-slate-950/70 p-2">
          {hasSegments ? (
            <div className="flex min-h-28 gap-2">
              {segments.map((segment) => {
                const span = Math.max(0.08, segment.to - segment.from);
                return (
                  <div
                    key={`${segment.state}-${segment.from}-${segment.to}`}
                    className="min-w-0 rounded-2xl px-4 py-4 text-white shadow-lg"
                    style={{ background: segment.color, flex: span }}
                  >
                    <div className="text-sm font-semibold leading-tight">{segment.label}</div>
                    <div className="mt-1 text-xs opacity-80">
                      {formatDoseLabel(segment.from)} to {formatDoseLabel(segment.to)}
                    </div>
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="flex min-h-24 items-center justify-center rounded-2xl border border-dashed border-white/10 bg-white/5 px-4 py-6 text-sm text-slate-400">
              {noResponse ? 'No validated pharmacodynamic response observed.' : 'No validated therapeutic window. Insufficient efficacy or toxicity-dominant profile in backend analysis.'}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
