import { useEffect, useState } from 'react';
import { useParams, Link } from 'react-router-dom';
import { motion } from 'framer-motion';
import { ArrowLeft, Download, FileText } from 'lucide-react';
import { getRunDetail, getRunReport } from '../api/client';
import { MetricCard } from '../components/MetricCard';
import { ReportViewer } from '../components/ReportViewer';
import { StatusBadge } from '../components/StatusBadge';
import { formatDateTime, formatDuration, humanizeLabel } from '../lib/format';
import type { RunDetailResponse } from '../types';

export function ReportDetailPage() {
  const { runId } = useParams();
  const [detail, setDetail] = useState<RunDetailResponse | null>(null);
  const [report, setReport] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    async function load() {
      if (!runId) {
        setError('Run not found');
        setLoading(false);
        return;
      }

      try {
        setLoading(true);
        const response = await getRunDetail(runId);
        if (!active) return;
        setDetail(response);
        if (response.raw_report) {
          setReport(response.raw_report);
        } else {
          setReport(await getRunReport(runId));
        }
      } catch (err) {
        if (active) {
          setError(err instanceof Error ? err.message : 'Backend unreachable');
        }
      } finally {
        if (active) {
          setLoading(false);
        }
      }
    }

    load();
    return () => {
      active = false;
    };
  }, [runId]);

  const downloadReport = () => {
    if (!report) return;
    const blob = new Blob([report], { type: 'text/plain;charset=utf-8' });
    const url = window.URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `${detail?.run_id ?? 'silicon-patient-report'}.txt`;
    anchor.click();
    window.URL.revokeObjectURL(url);
  };

  return (
    <div className="space-y-6 pb-24 xl:pb-8">
      <div className="flex items-center justify-between gap-3">
        <Link to="/history" className="inline-flex items-center gap-2 rounded-2xl border border-white/10 bg-white/5 px-4 py-2 text-sm text-slate-200 transition hover:border-cyan-400/30 hover:bg-cyan-500/10">
          <ArrowLeft className="h-4 w-4" /> Back to History
        </Link>
        <button
          type="button"
          onClick={downloadReport}
          className="inline-flex items-center gap-2 rounded-2xl bg-gradient-to-r from-cyan-500 to-emerald-500 px-4 py-2 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01]"
        >
          <Download className="h-4 w-4" /> Download Report
        </button>
      </div>

      {loading ? <div className="glass-card p-6 text-sm text-slate-400">Loading report detail...</div> : null}
      {error ? <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-5 py-4 text-sm text-rose-200">{error}</div> : null}

      {detail ? (
        <>
          <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} className="glass-card p-6">
            <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
              <div>
                <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Run Detail</p>
                <h2 className="mt-2 text-3xl font-semibold text-white">{detail.report_type} report</h2>
                <p className="mt-2 text-sm text-slate-400">Run ID: <span className="font-mono text-cyan-200">{detail.run_id}</span></p>
              </div>
              <div className="flex flex-wrap gap-2">
                <StatusBadge label="Status" value={detail.status} />
                <StatusBadge label="Risk" value={detail.risk_level} />
              </div>
            </div>
          </motion.div>

          <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-4">
            <MetricCard label="Run Type" value={humanizeLabel(detail.report_type)} />
            <MetricCard label="Drug Name" value={detail.drug_name ?? '—'} />
            <MetricCard label="Created At" value={formatDateTime(detail.created_at)} />
            <MetricCard label="Duration" value={formatDuration(detail.duration_seconds)} />
          </div>

          <div className="grid gap-4 md:grid-cols-3">
            {Object.entries(detail.parsed_summary ?? {})
              .filter(([key]) => key !== 'visualization_data' && key !== 'visualizationData')
              .map(([key, value]) => (
                <div key={key} className="glass-card p-5">
                  <p className="text-xs uppercase tracking-[0.28em] text-cyan-300/70">{humanizeLabel(key)}</p>
                  <p className="mt-2 text-xl font-semibold text-white">{typeof value === 'string' ? value : JSON.stringify(value)}</p>
                </div>
              ))}
          </div>

          {report ? <ReportViewer title={`${humanizeLabel(detail.report_type)} Report`} report={report} /> : null}
        </>
      ) : null}

      <div className="glass-card p-5 text-sm text-slate-400">
        <div className="flex items-center gap-2 text-white">
          <FileText className="h-4 w-4 text-cyan-300" /> Audit-ready storage
        </div>
        <p className="mt-2 leading-7">This page surfaces the stored run metadata, parsed summary, and the full raw report text for review or download.</p>
      </div>
    </div>
  );
}
