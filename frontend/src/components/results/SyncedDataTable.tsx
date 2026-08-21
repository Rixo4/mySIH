import React, { useState } from 'react';
import { ArrowUpDown, Table as TableIcon, Filter } from 'lucide-react';
import type { DoseResultPoint } from '../../types';
import { humanizeEnum } from '../../lib/format';

interface SyncedDataTableProps {
  data: DoseResultPoint[];
  selectedDose: number | null;
  onSelectDose: (dose: number) => void;
}

export const SyncedDataTable: React.FC<SyncedDataTableProps> = ({
  data,
  selectedDose,
  onSelectDose,
}) => {
  const [sortField, setSortField] = useState<keyof DoseResultPoint>('dose');
  const [sortAsc, setSortAsc] = useState(true);
  const [filterText, setFilterText] = useState('');

  const handleSort = (field: keyof DoseResultPoint) => {
    if (sortField === field) {
      setSortAsc(!sortAsc);
    } else {
      setSortField(field);
      setSortAsc(true);
    }
  };

  const filteredData = data.filter((item) => {
    if (!filterText) return true;
    const term = filterText.toLowerCase();
    return (
      String(item.dose).includes(term) ||
      (item.biological_state && item.biological_state.toLowerCase().includes(term)) ||
      (item.response_mode && item.response_mode.toLowerCase().includes(term))
    );
  });

  const sortedData = [...filteredData].sort((a, b) => {
    const valA = a[sortField] ?? 0;
    const valB = b[sortField] ?? 0;
    if (valA < valB) return sortAsc ? -1 : 1;
    if (valA > valB) return sortAsc ? 1 : -1;
    return 0;
  });

  return (
    <div className="p-5 rounded-xl border border-[#1E2330] bg-[#0C1017] shadow-xl space-y-4 font-sans">
      {/* Header controls */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
        <div className="flex items-center gap-2">
          <TableIcon className="w-4 h-4 text-sky-400" />
          <h3 className="text-xs font-bold text-slate-100 uppercase tracking-wider font-mono">
            Synchronized Dose Evaluation Data Matrix
          </h3>
        </div>

        <div className="flex items-center gap-2">
          <div className="relative">
            <Filter className="w-3.5 h-3.5 text-slate-500 absolute left-3 top-2.5" />
            <input
              type="text"
              value={filterText}
              onChange={(e) => setFilterText(e.target.value)}
              placeholder="Filter dose results..."
              className="pl-8 pr-3 py-1.5 rounded-lg bg-[#11151E] border border-[#1E2330] text-xs text-slate-200 placeholder-slate-500 focus:outline-none focus:border-sky-500/50"
            />
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="overflow-x-auto rounded-lg border border-[#1E2330]">
        <table className="w-full text-left text-xs text-slate-300">
          <thead className="bg-[#11151E] font-mono text-[10px] uppercase text-slate-400 border-b border-[#1E2330]">
            <tr>
              <th
                onClick={() => handleSort('dose')}
                className="p-3 cursor-pointer hover:text-sky-400 transition-colors"
              >
                <div className="flex items-center gap-1">
                  <span>Dose (µM)</span>
                  <ArrowUpDown className="w-3 h-3" />
                </div>
              </th>
              <th
                onClick={() => handleSort('firing_rate')}
                className="p-3 cursor-pointer hover:text-sky-400 transition-colors"
              >
                <div className="flex items-center gap-1">
                  <span>Firing Rate (Hz)</span>
                  <ArrowUpDown className="w-3 h-3" />
                </div>
              </th>
              <th
                onClick={() => handleSort('sync')}
                className="p-3 cursor-pointer hover:text-sky-400 transition-colors"
              >
                <div className="flex items-center gap-1">
                  <span>Sync Index</span>
                  <ArrowUpDown className="w-3 h-3" />
                </div>
              </th>
              <th
                onClick={() => handleSort('nii')}
                className="p-3 cursor-pointer hover:text-sky-400 transition-colors"
              >
                <div className="flex items-center gap-1">
                  <span>Instability (NII)</span>
                  <ArrowUpDown className="w-3 h-3" />
                </div>
              </th>
              <th
                onClick={() => handleSort('toxicity_score')}
                className="p-3 cursor-pointer hover:text-sky-400 transition-colors"
              >
                <div className="flex items-center gap-1">
                  <span>Toxicity</span>
                  <ArrowUpDown className="w-3 h-3" />
                </div>
              </th>
              <th className="p-3">Biological State</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-[#1E2330]/60 font-mono">
            {sortedData.map((row) => {
              const isSelected = selectedDose === row.dose;
              const humanizedState = humanizeEnum(row.biological_state || 'Observed');
              const isDangerous = (row.biological_state || '').toUpperCase().includes('DANGEROUS') || (row.biological_state || '').toUpperCase().includes('HYPEREXCITABLE');
              const isEffective = (row.biological_state || '').toUpperCase().includes('EFFECTIVE') || (row.biological_state || '').toUpperCase().includes('STABILIZING');

              return (
                <tr
                  key={row.dose}
                  onClick={() => onSelectDose(row.dose)}
                  className={`cursor-pointer transition-colors ${
                    isSelected
                      ? 'bg-[#13233D] text-sky-200 ring-1 ring-inset ring-sky-500/40 font-semibold'
                      : 'hover:bg-[#11151E]'
                  }`}
                >
                  <td className="p-3 text-sky-400 font-bold">{row.dose.toFixed(1)} µM</td>
                  <td className="p-3">{row.firing_rate?.toFixed(1) ?? '—'}</td>
                  <td className="p-3">{row.sync?.toFixed(2) ?? '—'}</td>
                  <td className="p-3">{row.nii?.toFixed(3) ?? '—'}</td>
                  <td className="p-3">{row.toxicity_score?.toFixed(2) ?? '—'}</td>
                  <td className="p-3">
                    <span className={`px-2.5 py-0.5 rounded text-[10px] border font-sans font-medium ${
                      isDangerous
                        ? 'bg-rose-500/10 border-rose-500/30 text-rose-300'
                        : isEffective
                        ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300'
                        : 'bg-[#181D28] border-[#252B38] text-slate-300'
                    }`}>
                      {humanizedState}
                    </span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      <div className="text-[11px] text-slate-400 flex items-center justify-between">
        <span>Click any row to synchronize chart active dose points.</span>
        <span className="font-mono text-slate-500">Showing {sortedData.length} concentration steps</span>
      </div>
    </div>
  );
};
