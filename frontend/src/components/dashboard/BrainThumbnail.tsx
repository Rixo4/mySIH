import React, { useState, useEffect, useRef } from 'react';
import { Eye, EyeOff, Layers } from 'lucide-react';

interface BrainThumbnailProps {
  activeLayer?: 'L2_3' | 'L5' | 'INTER' | 'THAL';
  onLayerClick?: (layer: 'L2_3' | 'L5' | 'INTER' | 'THAL') => void;
}

const LAYERS = [
  { id: 'L2_3',  label: 'Layer 2/3',   color: '#38bdf8' },
  { id: 'L5',    label: 'Layer 5',     color: '#e879f9', isDefaultActive: true },
  { id: 'INTER', label: 'Interneurons', color: '#10b981' },
] as const;

export function BrainThumbnail({
  activeLayer: propActiveLayer,
  onLayerClick,
}: BrainThumbnailProps) {
  const [selectedLayer, setSelectedLayer] = useState<'L2_3' | 'L5' | 'INTER' | 'THAL'>(
    propActiveLayer || 'L5'
  );
  const [visibleLayers, setVisibleLayers] = useState<Record<string, boolean>>({
    L2_3: true,
    L5: true,
    INTER: true,
    THAL: true,
  });

  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  const handleSelect = (id: 'L2_3' | 'L5' | 'INTER' | 'THAL') => {
    setSelectedLayer(id);
    onLayerClick?.(id);
  };

  const toggleVisibility = (id: string, e: React.MouseEvent) => {
    e.stopPropagation();
    setVisibleLayers((prev) => ({ ...prev, [id]: !prev[id] }));
  };

  // ── High-Fidelity Anatomical Rendering Loop ──
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    let animId: number;
    let time = 0;

    // Biological Neural Nodes accurately placed on human brain anatomy
    const nodes = [
      // ── Layer 2/3 (Superficial Frontal & Parietal Cortex - Cyan) ──
      { id: 'l2_1', layer: 'L2_3', x: 42, y: 56, col: '#00f5d4', r: 2.8, name: 'Prefrontal L2' },
      { id: 'l2_2', layer: 'L2_3', x: 50, y: 38, col: '#38bdf8', r: 3.0, name: 'Frontal Pole' },
      { id: 'l2_3', layer: 'L2_3', x: 68, y: 26, col: '#00f5d4', r: 2.9, name: 'Superior Frontal' },
      { id: 'l2_4', layer: 'L2_3', x: 88, y: 24, col: '#38bdf8', r: 3.2, name: 'Precentral Gyrus' },
      { id: 'l2_5', layer: 'L2_3', x: 108, y: 30, col: '#22d3ee', r: 3.0, name: 'Postcentral Gyrus' },
      { id: 'l2_6', layer: 'L2_3', x: 126, y: 44, col: '#38bdf8', r: 2.8, name: 'Superior Parietal' },

      // ── Layer 5 (Deep Pyramidal Motor/Sensory Cortex - Magenta/Fuchsia Flare) ──
      { id: 'l5_1', layer: 'L5', x: 118, y: 38, col: '#e879f9', r: 4.5, isFlare: true, name: 'Primary Motor L5' },
      { id: 'l5_2', layer: 'L5', x: 134, y: 56, col: '#d946ef', r: 3.4, name: 'Parietal L5' },
      { id: 'l5_3', layer: 'L5', x: 130, y: 72, col: '#c084fc', r: 3.0, name: 'Occipital L5' },
      { id: 'l5_4', layer: 'L5', x: 104, y: 52, col: '#e879f9', r: 3.2, name: 'Somatosensory L5' },
      { id: 'l5_5', layer: 'L5', x: 84, y: 46, col: '#d946ef', r: 3.1, name: 'Deep Pyramidal' },

      // ── Interneurons (Distributed Cortical Inhibitory Circuit - Emerald) ──
      { id: 'in_1', layer: 'INTER', x: 56, y: 58, col: '#10b981', r: 2.7, name: 'Broca Interneuron' },
      { id: 'in_2', layer: 'INTER', x: 74, y: 48, col: '#34d399', r: 2.8, name: 'Cortical Basket Cell' },
      { id: 'in_3', layer: 'INTER', x: 92, y: 64, col: '#10b981', r: 2.9, name: 'Temporal Interneuron' },
      { id: 'in_4', layer: 'INTER', x: 114, y: 70, col: '#059669', r: 2.6, name: 'Auditory Interneuron' },
      { id: 'in_5', layer: 'INTER', x: 70, y: 76, col: '#10b981', r: 2.7, name: 'Temporal Pole INT' },

      // ── Thalamus & Subcortical Systems (Core & Brainstem - Indigo) ──
      { id: 'th_1', layer: 'THAL', x: 86, y: 68, col: '#818cf8', r: 3.6, name: 'Thalamic Relay' },
      { id: 'th_2', layer: 'THAL', x: 96, y: 78, col: '#6366f1', r: 3.2, name: 'Basal Nucleus' },
      { id: 'th_3', layer: 'THAL', x: 110, y: 92, col: '#a855f7', r: 3.0, name: 'Cerebellar Purkinje' },
      { id: 'th_4', layer: 'THAL', x: 122, y: 96, col: '#818cf8', r: 2.8, name: 'Cerebellar Core' },
      { id: 'th_5', layer: 'THAL', x: 90, y: 104, col: '#38bdf8', r: 2.6, name: 'Brainstem / Pons' },
    ];

    // Anatomical gyral / sulcal flow streamlines
    const gyriTracts = [
      // Superior Sagittal Margin Tract
      [[38, 64], [36, 48], [44, 34], [62, 22], [88, 20], [114, 26], [132, 40], [140, 58], [138, 76]],
      // Central Sulcus & Precentral Tract
      [[88, 20], [86, 36], [84, 52], [86, 68]],
      // Postcentral Sulcus
      [[108, 24], [104, 40], [100, 56], [98, 70]],
      // Superior Frontal Gyrus
      [[44, 34], [58, 40], [74, 44], [86, 46]],
      // Middle & Inferior Frontal Gyri
      [[38, 52], [52, 54], [68, 56]],
      // Temporal Lobe Superior & Middle Gyri
      [[56, 76], [74, 76], [94, 76], [112, 76]],
      // Cerebellar Horizontal Folia Tracts
      [[100, 88], [114, 88], [126, 90]],
      [[96, 96], [110, 96], [124, 98]],
      // Brainstem Descending Motor Pathway
      [[86, 78], [88, 92], [90, 108]],
    ];

    const render = () => {
      time += 0.035;
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      const w = canvas.width;
      const h = canvas.height;
      const cx = w * 0.54;
      const cy = h * 0.52;

      // ── 1. Rotating Polar Coordinate Radar Grid Backdrop ──
      ctx.save();
      ctx.strokeStyle = '#1e3a5f';
      ctx.lineWidth = 0.65;
      ctx.setLineDash([2, 4]);

      // Concentric range rings
      [56, 42, 28, 14].forEach((radius) => {
        ctx.beginPath();
        ctx.arc(cx, cy, radius, 0, Math.PI * 2);
        ctx.stroke();
      });

      // 12-Spoke Radial Lines
      for (let i = 0; i < 12; i++) {
        const angle = (i * Math.PI) / 6 + time * 0.04;
        const x2 = cx + Math.cos(angle) * 58;
        const y2 = cy + Math.sin(angle) * 58;
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(x2, y2);
        ctx.stroke();
      }
      ctx.restore();

      // ── 2. Precise Anatomical Human Brain Lateral Contour ──
      ctx.save();
      const brainGrad = ctx.createLinearGradient(30, 20, 145, 105);
      brainGrad.addColorStop(0.0, '#00f5d4'); // Frontal - Cyan
      brainGrad.addColorStop(0.35, '#38bdf8'); // Precentral - Azure
      brainGrad.addColorStop(0.70, '#a855f7'); // Parietal - Purple
      brainGrad.addColorStop(1.0, '#d946ef'); // Occipital - Magenta

      ctx.strokeStyle = brainGrad;
      ctx.lineWidth = 1.8;
      ctx.shadowColor = '#38bdf8';
      ctx.shadowBlur = 10;

      // Anatomical cerebral silhouette:
      // Frontal pole -> Parietal arch -> Occipital pole -> Cerebellar curve -> Brainstem -> Temporal notch -> Anterior
      ctx.beginPath();
      ctx.moveTo(38, 64);
      // Frontal lobe upward curvature
      ctx.bezierCurveTo(34, 48, 42, 32, 60, 22);
      // Superior parietal arch
      ctx.bezierCurveTo(80, 14, 110, 18, 128, 30);
      // Occipital lobe posterior curvature
      ctx.bezierCurveTo(142, 40, 146, 60, 138, 76);
      // Sub-occipital notch into Cerebellum
      ctx.bezierCurveTo(134, 82, 132, 86, 134, 94);
      // Cerebellum rounded posterior lobule
      ctx.bezierCurveTo(134, 104, 120, 106, 108, 104);
      // Brainstem inferior stalk
      ctx.lineTo(96, 108);
      ctx.lineTo(84, 108);
      ctx.lineTo(84, 94);
      // Temporal lobe inferior margin & forward curl
      ctx.bezierCurveTo(76, 92, 60, 90, 56, 80);
      // Sylvian notch & Frontal base
      ctx.bezierCurveTo(52, 70, 42, 74, 38, 64);
      ctx.closePath();
      ctx.stroke();

      // Subtle translucent brain mass fill
      ctx.fillStyle = 'rgba(56, 189, 248, 0.04)';
      ctx.fill();
      ctx.restore();

      // ── 3. Anatomical Sulci & Gyri Streamline Mesh ──
      ctx.save();
      gyriTracts.forEach((pts, idx) => {
        ctx.beginPath();
        ctx.moveTo(pts[0][0], pts[0][1]);
        for (let k = 1; k < pts.length; k++) {
          ctx.lineTo(pts[k][0], pts[k][1]);
        }

        // Color gradient based on X coordinate
        const midX = pts[Math.floor(pts.length / 2)][0];
        const isSelectedTract =
          (selectedLayer === 'L2_3' && midX < 80) ||
          (selectedLayer === 'L5' && midX > 100) ||
          (selectedLayer === 'INTER' && midX >= 60 && midX <= 100) ||
          (selectedLayer === 'THAL' && idx >= gyriTracts.length - 4);

        ctx.strokeStyle = isSelectedTract
          ? 'rgba(56, 189, 248, 0.75)'
          : 'rgba(56, 189, 248, 0.35)';
        ctx.lineWidth = isSelectedTract ? 1.2 : 0.8;
        ctx.stroke();
      });
      ctx.restore();

      // ── 4. Interconnected Synaptic Neural Lattice Lines ──
      ctx.save();
      for (let i = 0; i < nodes.length; i++) {
        for (let j = i + 1; j < nodes.length; j++) {
          const n1 = nodes[i];
          const n2 = nodes[j];
          const isN1Visible = visibleLayers[n1.layer] ?? true;
          const isN2Visible = visibleLayers[n2.layer] ?? true;

          if (!isN1Visible || !isN2Visible) continue;

          const dx = n1.x - n2.x;
          const dy = n1.y - n2.y;
          const dist = Math.sqrt(dx * dx + dy * dy);

          if (dist < 36) {
            const isN1Active = selectedLayer === n1.layer;
            const isN2Active = selectedLayer === n2.layer;
            const lineAlpha = isN1Active || isN2Active ? 0.7 : 0.22;

            ctx.strokeStyle = isN1Active || isN2Active ? '#38bdf8' : 'rgba(100, 116, 139, 0.35)';
            ctx.lineWidth = isN1Active || isN2Active ? 1.1 : 0.6;

            ctx.beginPath();
            ctx.moveTo(n1.x, n1.y);
            ctx.lineTo(n2.x, n2.y);
            ctx.globalAlpha = lineAlpha;
            ctx.stroke();
          }
        }
      }
      ctx.restore();

      // ── 5. Neural Synapse Nodes with Dynamic Selection Pulsing ──
      nodes.forEach((node) => {
        const isVisible = visibleLayers[node.layer] ?? true;
        if (!isVisible) return;

        const isActive = selectedLayer === node.layer;
        const pulse = Math.sin(time * 3 + node.x * 0.5) * 0.6;
        const radius = node.r + (isActive ? 1.4 : 0) + pulse * 0.3;

        ctx.save();
        ctx.beginPath();
        ctx.arc(node.x, node.y, Math.max(1, radius), 0, Math.PI * 2);
        ctx.fillStyle = node.col;

        if (isActive) {
          ctx.shadowColor = node.col;
          ctx.shadowBlur = 12;
          ctx.globalAlpha = 1.0;
        } else {
          ctx.globalAlpha = 0.55;
        }

        ctx.fill();
        ctx.restore();

        // ── 6. Radiant Layer 5 Starburst Flare ──
        if (node.isFlare && (selectedLayer === 'L5' || visibleLayers['L5'])) {
          ctx.save();
          ctx.translate(node.x, node.y);

          // Radial Halo
          const flareGrad = ctx.createRadialGradient(0, 0, 1, 0, 0, 18);
          flareGrad.addColorStop(0, 'rgba(255, 255, 255, 0.95)');
          flareGrad.addColorStop(0.25, 'rgba(232, 121, 249, 0.85)');
          flareGrad.addColorStop(0.65, 'rgba(217, 70, 239, 0.30)');
          flareGrad.addColorStop(1, 'transparent');

          ctx.fillStyle = flareGrad;
          ctx.beginPath();
          ctx.arc(0, 0, 18, 0, Math.PI * 2);
          ctx.fill();

          // 4-Point Star Rays
          ctx.strokeStyle = '#ffffff';
          ctx.lineWidth = 1.4;
          ctx.shadowColor = '#e879f9';
          ctx.shadowBlur = 8;

          ctx.beginPath();
          ctx.moveTo(0, -20); ctx.lineTo(0, 20);
          ctx.moveTo(-20, 0); ctx.lineTo(20, 0);
          ctx.stroke();

          ctx.restore();
        }
      });

      animId = requestAnimationFrame(render);
    };

    render();

    return () => {
      cancelAnimationFrame(animId);
    };
  }, [selectedLayer, visibleLayers]);

  return (
    <div
      className="relative flex items-center justify-between h-full select-none gap-2 overflow-hidden"
      style={{
        background: '#0B0D10',
        border: '1px solid #1D2127',
        borderRadius: 6,
        padding: '4px 8px',
      }}
    >
      {/* ── Left Side: High-Fidelity Anatomical Brain Schematic Viewport ── */}
      <div className="relative flex-1 flex items-center justify-center h-full min-h-0 overflow-hidden max-w-[50%]">
        <canvas
          ref={canvasRef}
          width={150}
          height={110}
          className="w-full h-full object-contain pointer-events-none"
          style={{ maxHeight: '100%' }}
        />
      </div>

      {/* ── Right Side: VIEW LAYERS List ── */}
      <div className="flex flex-col justify-between w-36 h-full py-0 border-l border-[#1D2127] pl-2">
        <div className="flex items-center justify-between text-[9px] font-mono text-slate-300 mb-0.5">
          <span className="font-bold uppercase tracking-widest text-[#D4D8DE] text-[9px]">VIEW LAYERS</span>
          <Layers className="w-2.5 h-2.5 text-slate-400" />
        </div>

        <div className="flex flex-col gap-0.5 flex-1 justify-between">
          {LAYERS.map((layer) => {
            const isSelected = selectedLayer === layer.id;
            const isVisible = visibleLayers[layer.id] ?? true;

            return (
              <div
                key={layer.id}
                onClick={() => handleSelect(layer.id as 'L2_3' | 'L5' | 'INTER')}
                className={`flex items-center justify-between px-1.5 py-0.5 rounded text-[9px] font-mono transition-all cursor-pointer ${
                  isSelected
                    ? 'text-white font-bold'
                    : 'text-slate-300 hover:text-white hover:bg-[#161A20]'
                }`}
                style={
                  isSelected
                    ? {
                        background: '#1d4ed8',
                        boxShadow: '0 0 8px rgba(29, 78, 216, 0.7)',
                        border: '1px solid rgba(96, 165, 250, 0.6)',
                      }
                    : {}
                }
              >
                <div className="flex items-center gap-1 truncate">
                  <span
                    className="w-1.5 h-1.5 rounded-full shrink-0"
                    style={{
                      background: layer.color,
                      boxShadow: isSelected ? '0 0 5px #ffffff' : `0 0 3px ${layer.color}`,
                    }}
                  />
                  <span className="truncate">{layer.label}</span>
                </div>

                <button
                  type="button"
                  onClick={(e) => toggleVisibility(layer.id, e)}
                  className="text-slate-400 hover:text-slate-200 transition-colors p-0.5"
                >
                  {isVisible ? (
                    <Eye className="w-2.5 h-2.5 text-slate-300" />
                  ) : (
                    <EyeOff className="w-2.5 h-2.5 text-slate-600" />
                  )}
                </button>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}

