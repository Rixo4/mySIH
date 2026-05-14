import { ClipboardCopy } from 'lucide-react';
import { useMemo, useState } from 'react';
import { motion } from 'framer-motion';

interface ReportViewerProps {
  title?: string;
  report: string;
  className?: string;
}

function highlightLine(line: string): string {
  return line
    .replace(/PROMISING/g, '<span class="text-emerald-300 font-semibold">PROMISING</span>')
    .replace(/CAUTION/g, '<span class="text-amber-300 font-semibold">CAUTION</span>')
    .replace(/NOT RECOMMENDED/g, '<span class="text-rose-300 font-semibold">NOT RECOMMENDED</span>')
    .replace(/\bHIGH\b/g, '<span class="text-rose-300 font-semibold">HIGH</span>')
    .replace(/BIOLOGICALLY VALIDATED/g, '<span class="text-emerald-300 font-semibold">BIOLOGICALLY VALIDATED</span>')
    .replace(/\bPASS\b/g, '<span class="text-emerald-300 font-semibold">PASS</span>')
    .replace(/\bFAIL\b/g, '<span class="text-rose-300 font-semibold">FAIL</span>');
}

export function ReportViewer({ title = 'Report Viewer', report, className }: ReportViewerProps) {
  const [copied, setCopied] = useState(false);

  const lines = useMemo(() => report.split(/\r?\n/), [report]);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(report);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1400);
  };

  return (
    <motion.section initial={{ opacity: 0 }} animate={{ opacity: 1 }} className={`glass-card ${className ?? ''}`}>
      <div className="flex items-center justify-between gap-4 border-b border-white/10 px-5 py-4">
        <div>
          <p className="text-xs uppercase tracking-[0.3em] text-cyan-300/70">Structured Output</p>
          <h3 className="mt-1 text-lg font-semibold text-white">{title}</h3>
        </div>
        <button
          type="button"
          onClick={handleCopy}
          className="inline-flex items-center gap-2 rounded-xl border border-white/10 bg-white/5 px-3 py-2 text-sm text-slate-200 transition hover:border-cyan-400/30 hover:bg-cyan-500/10"
        >
          <ClipboardCopy className="h-4 w-4" />
          {copied ? 'Copied' : 'Copy'}
        </button>
      </div>

      <div className="max-h-[720px] overflow-auto p-5">
        <pre className="font-mono text-sm leading-7 text-slate-200">
          {lines.map((line, index) => {
            const isSection = /^\[.+\]$/.test(line.trim());
            const isDivider = /^[-=\s]{8,}$/.test(line.trim());
            return (
              <span key={`${index}-${line}`} className="block">
                {isDivider ? (
                  <span className="block py-1 text-cyan-400/40">{line}</span>
                ) : isSection ? (
                  <span className="inline-flex rounded-lg bg-cyan-500/10 px-2 py-1 font-semibold text-cyan-200 ring-1 ring-cyan-400/15">
                    {line}
                  </span>
                ) : (
                  <span dangerouslySetInnerHTML={{ __html: highlightLine(line || ' ') }} />
                )}
              </span>
            );
          })}
        </pre>
      </div>
    </motion.section>
  );
}
