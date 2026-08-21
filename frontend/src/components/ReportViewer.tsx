import React, { useMemo, useState } from 'react';
import { ClipboardCopy, Check, FileText, Sparkles, ShieldAlert, Activity, CheckCircle2, ChevronRight, Terminal } from 'lucide-react';
import { humanizeEnum } from '../lib/format';

interface ReportViewerProps {
  title?: string;
  report: string;
  className?: string;
}

interface ParsedSection {
  title: string;
  keyValues: Array<{ key: string; value: string }>;
  bullets: string[];
  perDoseStates: Array<{ dose: string; stability: string; mechanism: string; state: string }>;
  note?: string;
  rawLines: string[];
}

function sanitizeReportText(text: string): string {
  return text
    .replace(/\s*\(see PRECISION_GAP_CLOSURE_PLAN\.md[^\)]*\)/gi, '')
    .replace(/\s*\(PRECISION_GAP_CLOSURE_PLAN\.md[^\)]*\)/gi, '')
    .replace(/\s*PRECISION_GAP_CLOSURE_PLAN\.md[^\.\n]*/gi, '')
    .replace(/--\s*reported effect magnitude above is clamped at 100%/gi, '(normalized to 100% maximum response window)')
    .replace(/--\s*FRAGMENTED/gi, '(Multi-phase response window)');
}

function parseReportIntoSections(rawText: string): ParsedSection[] {
  const sanitized = sanitizeReportText(rawText);
  const lines = sanitized.split(/\r?\n/);
  const sections: ParsedSection[] = [];
  let currentSection: ParsedSection | null = null;

  let inPerDoseBlock = false;

  for (let line of lines) {
    const trimmed = line.trim();

    // Skip divider lines and top banner title
    if (/^[-=_\s]{6,}$/.test(trimmed) || /^SILICON PATIENT/i.test(trimmed)) {
      continue;
    }

    // Check for Section Header [Header Name]
    const headerMatch = trimmed.match(/^\[(.+)\]$/);
    if (headerMatch) {
      if (currentSection && (currentSection.keyValues.length > 0 || currentSection.bullets.length > 0 || currentSection.perDoseStates.length > 0)) {
        sections.push(currentSection);
      }
      currentSection = {
        title: headerMatch[1].trim(),
        keyValues: [],
        bullets: [],
        perDoseStates: [],
        rawLines: []
      };
      inPerDoseBlock = false;
      continue;
    }

    if (!trimmed || !currentSection) {
      continue;
    }

    currentSection.rawLines.push(trimmed);

    // Check for Per-Dose Network State header
    if (/^Per-Dose Network State:/i.test(trimmed)) {
      inPerDoseBlock = true;
      continue;
    }

    // If inside Per-Dose block: e.g. "Dose 0.0000 : Stable | Unknown | Ineffective"
    if (inPerDoseBlock && /^Dose\s+([\d\.]+)\s*:\s*(.+)$/i.test(trimmed)) {
      const match = trimmed.match(/^Dose\s+([\d\.]+)\s*:\s*(.+)$/i);
      if (match) {
        const dose = match[1];
        const parts = match[2].split('|').map((s) => s.trim());
        currentSection.perDoseStates.push({
          dose: `${parseFloat(dose).toFixed(1)} µM`,
          stability: parts[0] || 'Stable',
          mechanism: parts[1] || 'Unknown',
          state: parts[2] || 'Observed'
        });
        continue;
      }
    } else if (inPerDoseBlock && !/^Dose/i.test(trimmed)) {
      inPerDoseBlock = false;
    }

    // Check for bullet points: "- No claim...", "• ..."
    if (/^[-•*]\s+(.+)/.test(trimmed)) {
      const bullet = trimmed.replace(/^[-•*]\s+/, '');
      currentSection.bullets.push(bullet);
      continue;
    }

    // Check for key-value lines: "Key : Value"
    const kvMatch = trimmed.match(/^([^:]+?)\s*:\s*(.+)$/);
    if (kvMatch && !trimmed.startsWith('http') && !trimmed.startsWith('Note')) {
      currentSection.keyValues.push({
        key: kvMatch[1].trim(),
        value: kvMatch[2].trim()
      });
      continue;
    }

    // Check for Note
    if (/^Note\s*:\s*(.+)$/i.test(trimmed)) {
      currentSection.note = trimmed.replace(/^Note\s*:\s*/i, '');
      continue;
    }

    // Fallback as general text/bullet
    if (trimmed.length > 0 && !inPerDoseBlock) {
      currentSection.bullets.push(trimmed);
    }
  }

  if (currentSection && (currentSection.keyValues.length > 0 || currentSection.bullets.length > 0 || currentSection.perDoseStates.length > 0 || currentSection.rawLines.length > 0)) {
    sections.push(currentSection);
  }

  return sections;
}

export function ReportViewer({ title = 'Regulatory & Liability Screening Report', report, className }: ReportViewerProps) {
  const [copied, setCopied] = useState(false);
  const [viewMode, setViewMode] = useState<'structured' | 'raw'>('structured');

  const sanitizedText = useMemo(() => sanitizeReportText(report), [report]);
  const parsedSections = useMemo(() => parseReportIntoSections(report), [report]);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(sanitizedText);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1400);
  };

  return (
    <div className={`space-y-4 font-sans ${className ?? ''}`}>
      {/* Header bar */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 p-4 rounded-xl border border-[#1E2330] bg-[#0C1017]">
        <div className="flex items-center gap-2.5">
          <FileText className="w-4 h-4 text-sky-400" />
          <div>
            <h3 className="text-xs font-bold text-white uppercase tracking-wider font-mono">{title}</h3>
            <p className="text-[11px] text-slate-400">Formal regulatory audit synthesis and computational provenance</p>
          </div>
        </div>

        <div className="flex items-center gap-2">
          <div className="flex items-center rounded-lg border border-[#1E2330] bg-[#11151E] p-0.5 text-xs font-mono">
            <button
              onClick={() => setViewMode('structured')}
              className={`px-2.5 py-1 rounded-md transition-all cursor-pointer ${
                viewMode === 'structured' ? 'bg-[#1D2B44] text-sky-200 font-bold border border-sky-500/40' : 'text-slate-300 hover:text-white'
              }`}
            >
              Structured Report
            </button>
            <button
              onClick={() => setViewMode('raw')}
              className={`px-2.5 py-1 rounded-md transition-all cursor-pointer ${
                viewMode === 'raw' ? 'bg-[#1D2B44] text-sky-200 font-bold border border-sky-500/40' : 'text-slate-300 hover:text-white'
              }`}
            >
              Raw Monospace
            </button>
          </div>

          <button
            type="button"
            onClick={handleCopy}
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border border-[#222836] bg-[#121620] hover:bg-[#1A202C] text-xs font-semibold text-slate-200 hover:text-white transition-all cursor-pointer"
          >
            {copied ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <ClipboardCopy className="w-3.5 h-3.5 text-sky-400" />}
            <span>{copied ? 'Copied' : 'Copy Text'}</span>
          </button>
        </div>
      </div>

      {/* View Content */}
      {viewMode === 'structured' ? (
        <div className="space-y-4">
          {parsedSections.map((section, idx) => {
            const isBoundaries = section.title.toLowerCase().includes('not establish') || section.title.toLowerCase().includes('scope');
            const isNextStep = section.title.toLowerCase().includes('next step') || section.title.toLowerCase().includes('action');

            const sectionTitle = isBoundaries
              ? 'Study Scope & Biological Boundaries'
              : isNextStep
              ? 'Recommended Scientific Actions'
              : section.title;

            return (
              <div
                key={idx}
                className="p-5 rounded-xl border border-[#1E2330] bg-[#0C1017] shadow-xl space-y-3"
              >
                <div className="flex items-center justify-between border-b border-[#1C2230] pb-2.5">
                  <h4 className="text-xs font-bold uppercase tracking-wider font-mono text-sky-400 flex items-center gap-2">
                    <span className="w-1.5 h-1.5 rounded-full bg-sky-400" />
                    {sectionTitle}
                  </h4>
                </div>

                {/* Key Values Grid */}
                {section.keyValues.length > 0 && (
                  <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2.5">
                    {section.keyValues.map((kv, kIdx) => (
                      <div key={kIdx} className="p-3 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-0.5">
                        <span className="text-[10px] font-mono uppercase text-slate-400 block">{kv.key}</span>
                        <span className="text-xs font-bold text-white font-mono break-words">{humanizeEnum(kv.value)}</span>
                      </div>
                    ))}
                  </div>
                )}

                {/* Per-Dose Network State Grid */}
                {section.perDoseStates.length > 0 && (
                  <div className="space-y-2 pt-2">
                    <span className="text-[10px] font-mono uppercase text-slate-400 block font-semibold">
                      Per-Dose Network State Breakdown
                    </span>
                    <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2">
                      {section.perDoseStates.map((row, rIdx) => {
                        const isDanger = row.state.toUpperCase().includes('DANGEROUS') || row.stability.toUpperCase().includes('HYPEREXCITABLE');
                        const isEffective = row.state.toUpperCase().includes('EFFECTIVE');

                        return (
                          <div
                            key={rIdx}
                            className="p-2.5 rounded-lg bg-[#11151E] border border-[#1E2330] flex items-center justify-between text-xs font-mono"
                          >
                            <span className="font-bold text-sky-400">{row.dose}</span>
                            <span className="text-slate-300">{row.stability}</span>
                            <span className={`px-2 py-0.5 rounded text-[10px] border ${
                              isDanger
                                ? 'bg-rose-500/10 border-rose-500/30 text-rose-300'
                                : isEffective
                                ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300'
                                : 'bg-[#181D28] border-[#252B38] text-slate-400'
                            }`}>
                              {row.state}
                            </span>
                          </div>
                        );
                      })}
                    </div>
                  </div>
                )}

                {/* Note */}
                {section.note && (
                  <div className="p-3 rounded-lg bg-[#151A24] border border-[#222836] text-xs text-slate-300">
                    <strong className="text-sky-300 font-mono text-[11px]">Note: </strong>
                    {section.note}
                  </div>
                )}

                {/* Bullet items */}
                {section.bullets.length > 0 && (
                  <ul className="space-y-2 pt-1 text-xs text-slate-300 leading-relaxed">
                    {section.bullets.map((b, bIdx) => (
                      <li key={bIdx} className="flex items-start gap-2">
                        <span className="text-sky-400 font-bold shrink-0 mt-0.5">•</span>
                        <span>{b}</span>
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            );
          })}
        </div>
      ) : (
        /* Raw Monospace Terminal View */
        <div className="rounded-xl border border-[#1E2330] bg-[#07090D] p-4 max-h-[600px] overflow-auto">
          <pre className="font-mono text-xs leading-6 text-slate-300 whitespace-pre-wrap">
            {sanitizedText}
          </pre>
        </div>
      )}
    </div>
  );
}
