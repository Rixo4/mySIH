import { CheckCircle2, XCircle, AlertTriangle } from 'lucide-react';
import { motion } from 'framer-motion';

interface ValidationCardProps {
  label: string;
  status: 'PASS' | 'FAIL' | 'UNKNOWN';
  detail?: string;
}

export function ValidationCard({ label, status, detail }: ValidationCardProps) {
  const icon = status === 'PASS' ? <CheckCircle2 className="h-5 w-5" /> : status === 'FAIL' ? <XCircle className="h-5 w-5" /> : <AlertTriangle className="h-5 w-5" />;
  const tone =
    status === 'PASS'
      ? 'border-emerald-400/20 bg-emerald-500/10 text-emerald-200'
      : status === 'FAIL'
        ? 'border-rose-400/20 bg-rose-500/10 text-rose-200'
        : 'border-amber-400/20 bg-amber-500/10 text-amber-200';

  return (
    <motion.div whileHover={{ y: -3 }} className={`glass-card flex h-full flex-col border p-5 ${tone}`}>
      <div className="flex items-center justify-between gap-4">
        <div>
          <p className="text-sm font-medium text-white">{label}</p>
          <p className="mt-1 text-xs uppercase tracking-[0.32em] text-slate-400">{status}</p>
        </div>
        {icon}
      </div>
      {detail ? <p className="mt-4 text-sm leading-6 text-slate-300 whitespace-pre-wrap">{detail}</p> : null}
    </motion.div>
  );
}
