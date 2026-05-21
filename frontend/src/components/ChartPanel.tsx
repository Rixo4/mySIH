import { ResponsiveContainer, LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, AreaChart, Area } from 'recharts';
import type { ReportChartPoint } from '../types';

interface ChartPanelProps {
  data: ReportChartPoint[];
}

function MiniChart({
  title,
  color,
  data,
  dataKey,
  fill
}: {
  title: string;
  color: string;
  data: ReportChartPoint[];
  dataKey: keyof ReportChartPoint;
  fill: string;
}) {
  return (
    <div className="glass-card p-4">
      <h4 className="text-sm font-semibold text-white">{title}</h4>
      <div className="mt-4 h-56">
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={data}>
            <defs>
              <linearGradient id={title.replace(/\s+/g, '-').toLowerCase()} x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor={fill} stopOpacity={0.35} />
                <stop offset="95%" stopColor={fill} stopOpacity={0.02} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.15)" />
            <XAxis dataKey="dose" stroke="#94a3b8" tick={{ fontSize: 12 }} />
            <YAxis stroke="#94a3b8" tick={{ fontSize: 12 }} />
            <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
            <Area type="monotone" dataKey={dataKey} stroke={color} fillOpacity={1} fill={`url(#${title.replace(/\s+/g, '-').toLowerCase()})`} />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

export function ChartPanel({ data }: ChartPanelProps) {
  if (data.length === 0) {
    return (
      <div className="glass-card p-6 text-sm text-slate-400">
        Chart data will appear after the backend returns a completed dose-evaluation report.
      </div>
    );
  }

  return (
    <div className="grid gap-5 xl:grid-cols-3">
      <MiniChart title="Dose vs Effect" color="#38bdf8" fill="#38bdf8" data={data} dataKey="effect" />
      <MiniChart title="Dose vs Toxicity" color="#34d399" fill="#34d399" data={data} dataKey="toxicity" />
      <MiniChart title="Dose vs Risk" color="#f59e0b" fill="#f59e0b" data={data} dataKey="risk" />
    </div>
  );
}
