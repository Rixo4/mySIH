import React, { useEffect, useRef, useState } from 'react';

/* ─────────────────────────────────────────────
   TYPES
───────────────────────────────────────────── */
export interface NeuroNode {
  id: string;
  x: number; // normalised 0-1
  y: number; // normalised 0-1
  type: 'PYR' | 'INT' | 'CORE';
  shortLabel: string;
  fullLabel: string;
  radius: number;
  connections: string[];
  phase: number;
  layer?: string;
  calloutTarget?: {
    code: string;
    title: string;
    value: string;
    status: string;
    badge: string;
  };
}

export interface SignalParticle {
  id: number;
  connIdx: number;
  t: number;       // progress 0→1
  speed: number;
  type: 'EXCITATORY' | 'INHIBITORY';
  cascadeStep?: number;
}

export interface AnchorPositions {
  [nodeId: string]: { x: number; y: number };
}

/* ─────────────────────────────────────────────
   WELL-SPACED NEURAL MICROCIRCUIT DEFINITION (15 nodes)
───────────────────────────────────────────── */
export const NODES: NeuroNode[] = [
  { id: 'CORE',   x: 0.50, y: 0.46, type: 'CORE', shortLabel: 'CORE',   fullLabel: 'Simulation Core Hub',      radius: 28, connections: [], phase: 0, layer: 'CORTEX',
    calloutTarget: { code: 'CORE HUB', title: 'NMDA/AMPA Ratio', value: 'E/I: 0.72', status: 'NETWORK STABLE', badge: 'MODEL' }
  },
  { id: 'PYR-01', x: 0.26, y: 0.18, type: 'PYR',  shortLabel: 'PYR-01', fullLabel: 'Pyramidal L2/3 Soma',       radius: 15, connections: ['CORE','PYR-02','INT-01','PYR-09'], phase: 0.31, layer: 'L2/3' },
  { id: 'PYR-02', x: 0.50, y: 0.14, type: 'PYR',  shortLabel: 'PYR-02', fullLabel: 'Pyramidal L4 Apical',       radius: 13, connections: ['CORE','PYR-01','PYR-03','INT-02'], phase: 0.82, layer: 'L4' },
  { id: 'PYR-03', x: 0.74, y: 0.18, type: 'PYR',  shortLabel: 'PYR-03', fullLabel: 'Pyramidal L5 Trunk',        radius: 15, connections: ['CORE','PYR-02','PYR-04','INT-02'], phase: 0.45, layer: 'L5',
    calloutTarget: { code: 'PRG-14', title: 'Na⁺ Voltage-Gated', value: 'Depolarization: 42%', status: 'ACTIVE PATHWAY', badge: 'MODEL' }
  },
  { id: 'PYR-04', x: 0.84, y: 0.40, type: 'PYR',  shortLabel: 'PYR-04', fullLabel: 'Pyramidal L6a Efferent',    radius: 13, connections: ['CORE','PYR-03','PYR-05','INT-03'], phase: 1.27, layer: 'L6' },
  { id: 'PYR-05', x: 0.80, y: 0.68, type: 'PYR',  shortLabel: 'PYR-05', fullLabel: 'Pyramidal Basket Target',   radius: 15, connections: ['CORE','PYR-04','PYR-06','INT-03'], phase: 0.63, layer: 'L5' },
  { id: 'PYR-06', x: 0.62, y: 0.82, type: 'PYR',  shortLabel: 'PYR-06', fullLabel: 'Pyramidal PV Interactor',  radius: 13, connections: ['CORE','PYR-05','PYR-07','INT-04'], phase: 1.54, layer: 'L6' },
  { id: 'PYR-07', x: 0.38, y: 0.82, type: 'PYR',  shortLabel: 'PYR-07', fullLabel: 'Pyramidal VIP Target',      radius: 15, connections: ['CORE','PYR-06','PYR-08','INT-04'], phase: 0.94, layer: 'L4' },
  { id: 'PYR-08', x: 0.18, y: 0.68, type: 'PYR',  shortLabel: 'PYR-08', fullLabel: 'Pyramidal SST Interactor',  radius: 13, connections: ['CORE','PYR-07','PYR-09','INT-05'], phase: 1.83, layer: 'L2/3' },
  { id: 'PYR-09', x: 0.16, y: 0.40, type: 'PYR',  shortLabel: 'PYR-09', fullLabel: 'Pyramidal Motor Arbor',     radius: 15, connections: ['CORE','PYR-08','PYR-01','INT-01'], phase: 0.28, layer: 'L5' },
  { id: 'INT-01', x: 0.34, y: 0.30, type: 'INT',  shortLabel: 'INT-01', fullLabel: 'GABA-A PV+ Fast Spiking',   radius: 12, connections: ['CORE','PYR-01','PYR-09'], phase: 1.12, layer: 'INT',
    calloutTarget: { code: 'NEX-07', title: 'GABA-A Receptor', value: 'Cl⁻ Influx: +28%', status: 'ENHANCED INHIBITION', badge: 'MODEL' }
  },
  { id: 'INT-02', x: 0.64, y: 0.28, type: 'INT',  shortLabel: 'INT-02', fullLabel: 'SST+ Somatostatin Inhib',   radius: 12, connections: ['CORE','PYR-02','PYR-03'], phase: 0.71, layer: 'INT' },
  { id: 'INT-03', x: 0.70, y: 0.52, type: 'INT',  shortLabel: 'INT-03', fullLabel: 'VIP+ Disinhibitory Cell',   radius: 12, connections: ['CORE','PYR-04','PYR-05'], phase: 1.47, layer: 'INT',
    calloutTarget: { code: 'TX-27', title: 'NMDA Receptor', value: 'Ca²⁺ Influx: MONITORED', status: 'SYNAPTIC PLASTICITY', badge: 'MODEL' }
  },
  { id: 'INT-04', x: 0.48, y: 0.70, type: 'INT',  shortLabel: 'INT-04', fullLabel: 'Chandelier Axo-Axonic',     radius: 12, connections: ['CORE','PYR-06','PYR-07'], phase: 0.55, layer: 'INT' },
  { id: 'INT-05', x: 0.28, y: 0.52, type: 'INT',  shortLabel: 'INT-05', fullLabel: 'Calbindin+ Dendritic',      radius: 12, connections: ['CORE','PYR-08','PYR-09'], phase: 1.91, layer: 'INT' },
];

export const NODE_MAP = new Map<string, NeuroNode>(NODES.map(n => [n.id, n]));

export const CONN_LIST: { from: NeuroNode; to: NeuroNode; isExcitatory: boolean; label?: string }[] = (() => {
  const seen = new Set<string>();
  const result: { from: NeuroNode; to: NeuroNode; isExcitatory: boolean; label?: string }[] = [];
  const labelsMap: Record<string, string> = {
    'CORE::PYR-01': 'L2/3 Efferent',
    'INT-01::PYR-01': 'Perisomatic Inhib',
    'CORE::PYR-03': 'L5 Trunk Arc',
    'INT-03::PYR-05': 'Disinhibitory Feed',
  };
  for (const node of NODES) {
    for (const cid of node.connections) {
      const key = [node.id, cid].sort().join('::');
      if (!seen.has(key)) {
        seen.add(key);
        const toNode = NODE_MAP.get(cid);
        if (toNode) {
          const isExcitatory = node.type !== 'INT' && toNode.type !== 'INT';
          result.push({ from: node, to: toNode, isExcitatory, label: labelsMap[key] });
        }
      }
    }
  }
  return result;
})();

/* Bezier curve math helpers */
function getBezierCP(ax: number, ay: number, bx: number, by: number, c = 0.20) {
  const mx = (ax + bx) / 2;
  const my = (ay + by) / 2;
  const dx = bx - ax;
  const dy = by - ay;
  const len = Math.sqrt(dx * dx + dy * dy) || 1;
  return { cpx: mx + (-dy / len) * c * len, cpy: my + (dx / len) * c * len };
}

function bezierAt(ax: number, ay: number, cpx: number, cpy: number, bx: number, by: number, t: number) {
  const mt = 1 - t;
  return {
    x: mt * mt * ax + 2 * mt * t * cpx + t * t * bx,
    y: mt * mt * ay + 2 * mt * t * cpy + t * t * by,
  };
}

interface NeuroCanvasProps {
  activeRunStatus?: string;
  anchorPosRef?: React.MutableRefObject<AnchorPositions>;
  channelHighlight?: string | null;
  receptorHighlight?: string | null;
  calloutHoverNode?: string | null;
  selectedNodeId?: string | null;
  onSelectNode?: (nodeId: string | null) => void;
  isFocusMode?: boolean;
  onToggleFocusMode?: () => void;
  initProgress?: number;
}

export function NeuroCanvas({
  activeRunStatus,
  anchorPosRef,
  channelHighlight,
  receptorHighlight,
  calloutHoverNode,
  selectedNodeId,
  onSelectNode,
  isFocusMode,
  onToggleFocusMode,
  initProgress = 1.0,
}: NeuroCanvasProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const canvasRef    = useRef<HTMLCanvasElement>(null);
  const animRef      = useRef<number>(0);
  const timeRef      = useRef<number>(0);
  const lastBurstRef = useRef<number>(0);

  // Dynamic activation energy map for cascade pulse decay
  const nodeEnergyRef = useRef<Record<string, number>>({});

  // Interaction refs (NO setState in animation loop)
  const hoveredNodeRef   = useRef<string | null>(null);
  const hoveredConnRef   = useRef<number | null>(null);
  const particlesRef     = useRef<SignalParticle[]>([]);
  const pidRef           = useRef<number>(0);
  const cssWRef          = useRef<number>(0);
  const cssHRef          = useRef<number>(0);

  const [tooltip, setTooltip] = useState<{ nodeId: string | null; cx: number; cy: number }>({ nodeId: null, cx: 0, cy: 0 });

  /* Init signal particles */
  useEffect(() => {
    const particles: SignalParticle[] = [];
    CONN_LIST.forEach((conn, i) => {
      const count = conn.isExcitatory ? 2 : 1;
      for (let j = 0; j < count; j++) {
        particles.push({
          id: pidRef.current++,
          connIdx: i,
          t: Math.random(),
          speed: conn.isExcitatory ? 0.0035 + Math.random() * 0.003 : 0.002 + Math.random() * 0.002,
          type: conn.isExcitatory ? 'EXCITATORY' : 'INHIBITORY',
        });
      }
    });
    particlesRef.current = particles;
  }, []);

  /* Canvas Loop */
  useEffect(() => {
    const container = containerRef.current;
    const canvas    = canvasRef.current;
    if (!container || !canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;

    function applySize() {
      const w = container!.clientWidth;
      const h = container!.clientHeight;
      cssWRef.current = w;
      cssHRef.current = h;
      canvas!.width   = w * dpr;
      canvas!.height  = h * dpr;
      canvas!.style.width  = `${w}px`;
      canvas!.style.height = `${h}px`;
    }
    applySize();

    const ro = new ResizeObserver(applySize);
    ro.observe(container);

    function nodeAt(mx: number, my: number): NeuroNode | null {
      const W = cssWRef.current;
      const H = cssHRef.current;
      for (const node of NODES) {
        const dist = Math.hypot(mx - node.x * W, my - node.y * H);
        if (dist < node.radius + 12) return node;
      }
      return null;
    }

    function onMouseMove(e: MouseEvent) {
      const rect = canvas!.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const node = nodeAt(mx, my);
      const nid = node?.id ?? null;

      if (nid !== hoveredNodeRef.current) {
        hoveredNodeRef.current = nid;
        canvas!.style.cursor = nid ? 'pointer' : 'crosshair';
        setTooltip(nid ? { nodeId: nid, cx: mx, cy: my } : { nodeId: null, cx: 0, cy: 0 });
      }
    }

    function onClick(e: MouseEvent) {
      const rect = canvas!.getBoundingClientRect();
      const node = nodeAt(e.clientX - rect.left, e.clientY - rect.top);
      if (node) {
        const next = selectedNodeId === node.id ? null : node.id;
        if (onSelectNode) onSelectNode(next);

        // Mark node energy = 1.0 (activation pulse)
        nodeEnergyRef.current[node.id] = 1.0;

        // Trigger cascade wave
        CONN_LIST.forEach((conn, i) => {
          if (conn.from.id === node.id || conn.to.id === node.id) {
            particlesRef.current.push({
              id: pidRef.current++,
              connIdx: i,
              t: 0,
              speed: 0.007,
              type: conn.isExcitatory ? 'EXCITATORY' : 'INHIBITORY',
              cascadeStep: 1,
            });
          }
        });
      } else {
        if (onSelectNode) onSelectNode(null);
      }
    }

    function onMouseLeave() {
      hoveredNodeRef.current = null;
      canvas!.style.cursor = 'crosshair';
      setTooltip({ nodeId: null, cx: 0, cy: 0 });
    }

    canvas.addEventListener('mousemove', onMouseMove);
    canvas.addEventListener('click',     onClick);
    canvas.addEventListener('mouseleave', onMouseLeave);

    /* Main Render Loop */
    function draw() {
      if (!ctx || !canvas) return;

      timeRef.current += 0.012;
      const t = timeRef.current;
      const W = cssWRef.current;
      const H = cssHRef.current;
      if (!W || !H) { animRef.current = requestAnimationFrame(draw); return; }

      // Decay activation energy across nodes
      for (const nid in nodeEnergyRef.current) {
        nodeEnergyRef.current[nid] = Math.max(0, nodeEnergyRef.current[nid] - 0.025);
      }

      // Export Anchor Positions to parent Ref
      if (anchorPosRef) {
        const anchors: AnchorPositions = {};
        for (const n of NODES) {
          anchors[n.id] = { x: n.x * W, y: n.y * H };
        }
        anchorPosRef.current = anchors;
      }

      // Reset transform & clear
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

      const np = (n: NeuroNode) => ({ x: n.x * W, y: n.y * H });
      const sel = selectedNodeId;
      const hov = hoveredNodeRef.current || calloutHoverNode;
      const fid = sel || hov;

      // Channel / Receptor highlight filtering
      let targetNodeType: 'PYR' | 'INT' | 'ALL' | null = null;
      if (channelHighlight === 'Na' || channelHighlight === 'K') targetNodeType = 'PYR';
      if (channelHighlight === 'Ca') targetNodeType = 'ALL';

      if (receptorHighlight === 'AMPA' || receptorHighlight === 'NMDA') targetNodeType = 'PYR';
      if (receptorHighlight === 'GABA-A' || receptorHighlight === 'GABA-B') targetNodeType = 'INT';

      // Build active focus sets
      const relConns = new Set<number>();
      const relNodes = new Set<string>();
      if (fid) {
        relNodes.add(fid);
        CONN_LIST.forEach(({ from, to }, i) => {
          if (from.id === fid || to.id === fid) {
            relConns.add(i);
            relNodes.add(from.id);
            relNodes.add(to.id);
          }
        });
      }

      // ── 1. FAINT DENDRITIC TEXTURE BACKGROUND ──
      if (initProgress > 0.2) {
        ctx.save();
        ctx.strokeStyle = `rgba(99, 102, 241, ${0.018 * Math.min(1, initProgress * 2)})`;
        ctx.lineWidth = 0.6;
        for (let i = 0; i < 8; i++) {
          ctx.beginPath();
          const sx = (0.12 + i * 0.11) * W;
          const sy = (0.2 + (i % 3) * 0.25) * H;
          ctx.moveTo(sx, sy);
          ctx.bezierCurveTo(
            sx + Math.sin(t * 0.2 + i) * 60, sy - 40,
            sx - Math.cos(t * 0.2 + i) * 60, sy + 80,
            sx + 40, sy + 120
          );
          ctx.stroke();
        }
        ctx.restore();
      }

      // ── 2. ATMOSPHERIC CORAL RINGS ──
      if (initProgress > 0.4) {
        const coreNode = NODE_MAP.get('CORE')!;
        const cp = np(coreNode);
        ctx.save();
        for (let ring = 1; ring <= 4; ring++) {
          const ringRadius = coreNode.radius + ring * 38 + Math.sin(t * 0.5 + ring) * 3;
          const ringAlpha  = (0.04 - ring * 0.007) * initProgress;
          ctx.beginPath();
          ctx.arc(cp.x, cp.y, ringRadius, 0, Math.PI * 2);
          ctx.strokeStyle = `rgba(99, 102, 241, ${Math.max(0.008, ringAlpha)})`;
          ctx.lineWidth   = 0.8;
          ctx.setLineDash(ring % 2 === 0 ? [4, 8] : [2, 6]);
          ctx.stroke();
        }
        ctx.restore();
      }

      // ── 3. PATHWAYS & SYNAPTIC TERMINALS ──
      if (initProgress > 0.5) {
        const pathProgress = Math.min(1, (initProgress - 0.5) * 2.5);
        CONN_LIST.forEach(({ from, to, isExcitatory, label }, i) => {
          const fp = np(from);
          const tp = np(to);
          const { cpx, cpy } = getBezierCP(fp.x, fp.y, tp.x, tp.y, 0.22);

          let alpha = 0.22 * pathProgress;
          let lineW = 1.2;
          let isFocused = false;

          if (fid) {
            if (relConns.has(i)) {
              alpha = 0.90;
              lineW = 2.0;
              isFocused = true;
            } else {
              alpha = 0.04;
            }
          } else if (targetNodeType) {
            if (targetNodeType === 'ALL' || (from.type === targetNodeType || to.type === targetNodeType)) {
              alpha = 0.75;
              lineW = 1.8;
              isFocused = true;
            } else {
              alpha = 0.04;
            }
          }

          ctx.save();
          ctx.beginPath();
          ctx.moveTo(fp.x, fp.y);
          ctx.quadraticCurveTo(cpx, cpy, tp.x, tp.y);

          if (isExcitatory) {
            // Excitatory Pathway: Solid Indigo
            ctx.strokeStyle = `rgba(99, 102, 241, ${alpha})`;
            ctx.lineWidth = lineW;
            ctx.stroke();

            // Arrowhead terminal at target
            if (isFocused || !fid) {
              const angle = Math.atan2(tp.y - cpy, tp.x - cpx);
              const headLen = 7;
              ctx.beginPath();
              ctx.moveTo(tp.x - to.radius * Math.cos(angle), tp.y - to.radius * Math.sin(angle));
              ctx.lineTo(
                tp.x - (to.radius + headLen) * Math.cos(angle) - 3.5 * Math.sin(angle),
                tp.y - (to.radius + headLen) * Math.sin(angle) + 3.5 * Math.cos(angle)
              );
              ctx.lineTo(
                tp.x - (to.radius + headLen) * Math.cos(angle) + 3.5 * Math.sin(angle),
                tp.y - (to.radius + headLen) * Math.sin(angle) - 3.5 * Math.cos(angle)
              );
              ctx.fillStyle = `rgba(165, 180, 252, ${alpha * 0.95})`;
              ctx.fill();
            }
          } else {
            // Inhibitory Pathway: Dashed Emerald with Bouton
            ctx.setLineDash([5, 5]);
            ctx.strokeStyle = `rgba(16, 185, 129, ${alpha * 0.9})`;
            ctx.lineWidth = lineW;
            ctx.stroke();

            // Bouton open-circle terminal
            if (isFocused || !fid) {
              const angle = Math.atan2(tp.y - cpy, tp.x - cpx);
              const bx = tp.x - (to.radius + 5) * Math.cos(angle);
              const by = tp.y - (to.radius + 5) * Math.sin(angle);
              ctx.beginPath();
              ctx.arc(bx, by, 3.5, 0, Math.PI * 2);
              ctx.strokeStyle = `rgba(52, 211, 153, ${alpha})`;
              ctx.lineWidth = 1.3;
              ctx.stroke();
            }
          }

          // Pathway Label text at curve midpoint
          if (label && (isFocused || !fid)) {
            const mid = bezierAt(fp.x, fp.y, cpx, cpy, tp.x, tp.y, 0.5);
            ctx.font = '8.5px "JetBrains Mono", monospace';
            ctx.fillStyle = `rgba(148, 163, 184, ${alpha * 0.7})`;
            ctx.textAlign = 'center';
            ctx.fillText(label, mid.x, mid.y - 6);
          }
          ctx.restore();
        });
      }

      // ── 4. SIGNAL PARTICLES & CASCADE PROPAGATION ──
      if (initProgress > 0.7) {
        const particles = particlesRef.current;
        for (let idx = particles.length - 1; idx >= 0; idx--) {
          const p = particles[idx];
          if (p.connIdx >= CONN_LIST.length) continue;
          const { from, to, isExcitatory } = CONN_LIST[p.connIdx];

          if (fid && !relConns.has(p.connIdx)) {
            p.t = (p.t + p.speed) % 1;
            continue;
          }

          const fp = np(from);
          const tp = np(to);
          const { cpx, cpy } = getBezierCP(fp.x, fp.y, tp.x, tp.y, 0.22);
          const pos = bezierAt(fp.x, fp.y, cpx, cpy, tp.x, tp.y, p.t);

          ctx.save();
          if (isExcitatory) {
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, p.cascadeStep ? 3.5 : 2.5, 0, Math.PI * 2);
            ctx.fillStyle = p.cascadeStep ? 'rgba(255, 255, 255, 0.95)' : 'rgba(196, 181, 253, 0.90)';
            ctx.fill();

            const grd = ctx.createRadialGradient(pos.x, pos.y, 0, pos.x, pos.y, 6);
            grd.addColorStop(0, 'rgba(165, 180, 252, 0.65)');
            grd.addColorStop(1, 'rgba(0, 0, 0, 0)');
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, 6, 0, Math.PI * 2);
            ctx.fillStyle = grd;
            ctx.fill();
          } else {
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, 2.4, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(110, 231, 183, 0.90)';
            ctx.fill();
          }
          ctx.restore();

          p.t += p.speed;
          if (p.t >= 1) {
            // Particle hit target node -> trigger activation pulse!
            nodeEnergyRef.current[to.id] = Math.min(1.0, (nodeEnergyRef.current[to.id] || 0) + 0.6);

            if (p.cascadeStep && p.cascadeStep < 4) {
              const outgoingConns: number[] = [];
              CONN_LIST.forEach((c, ci) => {
                if (c.from.id === to.id) outgoingConns.push(ci);
              });
              if (outgoingConns.length > 0) {
                const nextConn = outgoingConns[Math.floor(Math.random() * outgoingConns.length)];
                particles.push({
                  id: pidRef.current++,
                  connIdx: nextConn,
                  t: 0,
                  speed: 0.006,
                  type: CONN_LIST[nextConn].isExcitatory ? 'EXCITATORY' : 'INHIBITORY',
                  cascadeStep: p.cascadeStep + 1,
                });
              }
              particles.splice(idx, 1);
            } else if (p.cascadeStep) {
              particles.splice(idx, 1);
            } else {
              p.t = 0;
            }
          }
        }

        // Periodic natural cascade (CORE -> PYR -> INT -> PYR)
        if (t - lastBurstRef.current > 3.5) {
          lastBurstRef.current = t;
          const coreConns: number[] = [];
          CONN_LIST.forEach((c, ci) => {
            if (c.from.id === 'CORE') coreConns.push(ci);
          });
          if (coreConns.length > 0) {
            const nextConn = coreConns[Math.floor(Math.random() * coreConns.length)];
            particlesRef.current.push({
              id: pidRef.current++,
              connIdx: nextConn,
              t: 0,
              speed: 0.006,
              type: 'EXCITATORY',
              cascadeStep: 1,
            });
          }
        }
      }

      // ── 5. SOMA NEURONS (PYR = Triangle, INT = Spiked Circle, CORE = Luminous) ──
      if (initProgress > 0.3) {
        const nodeProgress = Math.min(1, (initProgress - 0.3) * 2);
        for (const node of NODES) {
          const { x, y } = np(node);
          const isHov = hov === node.id;
          const isSel = sel === node.id;
          const isDim = fid ? !relNodes.has(node.id) : false;
          const pulse = Math.sin(t * 1.5 + node.phase) * 0.5 + 0.5;

          const energy = nodeEnergyRef.current[node.id] || 0;
          const baseRadius = node.radius * (isSel ? 1.25 : isHov ? 1.18 : 1.0) * nodeProgress;
          const activeRadius = baseRadius + energy * 3.5;

          ctx.save();

          if (node.type === 'PYR') {
            // Pyramidal Soma Triangle
            const s = activeRadius;
            const r = 79 + Math.floor(energy * 60), g = 70 + Math.floor(energy * 60), b = 229;

            if (!isDim) {
              const grd = ctx.createRadialGradient(x, y, 0, x, y, s * 2.5);
              grd.addColorStop(0, `rgba(99, 102, 241, ${isSel ? 0.55 : isHov ? 0.45 : 0.15 + energy * 0.35})`);
              grd.addColorStop(1, 'rgba(0,0,0,0)');
              ctx.beginPath();
              ctx.arc(x, y, s * 2.5, 0, Math.PI * 2);
              ctx.fillStyle = grd;
              ctx.fill();
            }

            // Dendritic Arbors
            ctx.beginPath();
            ctx.moveTo(x, y - s);
            ctx.lineTo(x, y - s - 14);
            ctx.moveTo(x - s * 0.6, y + s * 0.5);
            ctx.lineTo(x - s - 10, y + s + 8);
            ctx.moveTo(x + s * 0.6, y + s * 0.5);
            ctx.lineTo(x + s + 10, y + s + 8);
            ctx.strokeStyle = `rgba(129, 140, 248, ${isDim ? 0.1 : 0.4 + energy * 0.4})`;
            ctx.lineWidth = 1.0;
            ctx.stroke();

            // Triangular Body
            ctx.beginPath();
            ctx.moveTo(x, y - s * 1.1);
            ctx.lineTo(x + s * 0.9, y + s * 0.7);
            ctx.lineTo(x - s * 0.9, y + s * 0.7);
            ctx.closePath();
            ctx.fillStyle = `rgba(${r},${g},${b},${isDim ? 0.05 : 0.85 + energy * 0.15})`;
            ctx.fill();
            ctx.strokeStyle = `rgba(196, 181, 253, ${isDim ? 0.15 : 0.9})`;
            ctx.lineWidth = 1.5;
            ctx.stroke();

          } else if (node.type === 'INT') {
            // Interneuron Spiked Circle
            const s = activeRadius;
            const r = 16, g = 185 + Math.floor(energy * 50), b = 129;

            if (!isDim) {
              const grd = ctx.createRadialGradient(x, y, 0, x, y, s * 2.5);
              grd.addColorStop(0, `rgba(52, 211, 153, ${isSel ? 0.55 : isHov ? 0.45 : 0.15 + energy * 0.35})`);
              grd.addColorStop(1, 'rgba(0,0,0,0)');
              ctx.beginPath();
              ctx.arc(x, y, s * 2.5, 0, Math.PI * 2);
              ctx.fillStyle = grd;
              ctx.fill();
            }

            for (let sp = 0; sp < 6; sp++) {
              const ang = (sp * Math.PI) / 3;
              ctx.beginPath();
              ctx.moveTo(x + Math.cos(ang) * s, y + Math.sin(ang) * s);
              ctx.lineTo(x + Math.cos(ang) * (s + 7), y + Math.sin(ang) * (s + 7));
              ctx.strokeStyle = `rgba(52, 211, 153, ${isDim ? 0.1 : 0.5 + energy * 0.4})`;
              ctx.lineWidth = 1.0;
              ctx.stroke();
            }

            ctx.beginPath();
            ctx.arc(x, y, s, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(${r},${g},${b},${isDim ? 0.05 : 0.85 + energy * 0.15})`;
            ctx.fill();
            ctx.strokeStyle = `rgba(110, 231, 183, ${isDim ? 0.15 : 0.9})`;
            ctx.lineWidth = 1.5;
            ctx.stroke();

          } else {
            // CORE Hub
            const s = node.radius * nodeProgress;
            const grd = ctx.createRadialGradient(x, y, 0, x, y, s * 2.5);
            grd.addColorStop(0, `rgba(165, 180, 252, ${0.4 + pulse * 0.1 + energy * 0.3})`);
            grd.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.beginPath();
            ctx.arc(x, y, s * 2.5, 0, Math.PI * 2);
            ctx.fillStyle = grd;
            ctx.fill();

            ctx.beginPath();
            ctx.arc(x, y, s, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(79, 70, 229, 0.9)';
            ctx.fill();
            ctx.strokeStyle = 'rgba(224, 231, 255, 0.9)';
            ctx.lineWidth = 2.0;
            ctx.stroke();
          }

          // Inner Short Label
          if (node.type === 'CORE' || isHov || isSel) {
            ctx.font = `bold ${node.type === 'CORE' ? 10 : 8.5}px "JetBrains Mono", monospace`;
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillStyle = '#ffffff';
            ctx.fillText(node.shortLabel, x, y + (node.type === 'PYR' ? 1 : 0));
          }

          ctx.restore();
        }
      }

      animRef.current = requestAnimationFrame(draw);
    }

    animRef.current = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(animRef.current);
      canvas.removeEventListener('mousemove', onMouseMove);
      canvas.removeEventListener('click',     onClick);
      canvas.removeEventListener('mouseleave', onMouseLeave);
      ro.disconnect();
    };
  }, [channelHighlight, receptorHighlight, calloutHoverNode, selectedNodeId, initProgress]);

  const tooltipNode = tooltip.nodeId ? NODE_MAP.get(tooltip.nodeId) : null;
  const selNode     = selectedNodeId ? NODE_MAP.get(selectedNodeId) : null;
  const W = containerRef.current?.clientWidth ?? 400;

  return (
    <div ref={containerRef} className="relative w-full h-full neural-canvas-bg rounded-2xl overflow-hidden neural-glow">
      {/* Technical grid overlay */}
      <div className="absolute inset-0 bg-grid-neural opacity-100 pointer-events-none" />

      {/* Scan line effect */}
      <div className="absolute inset-0 pointer-events-none overflow-hidden opacity-30">
        <div
          className="absolute left-0 right-0 h-px scan-sweep"
          style={{ background: 'linear-gradient(90deg,transparent,rgba(99,102,241,0.4),transparent)' }}
        />
      </div>

      {/* Main Canvas */}
      <canvas ref={canvasRef} className="absolute inset-0 cursor-crosshair" />

      {/* Stage Toolbar (Top Right) */}
      <div className="absolute top-3 right-3 flex items-center gap-2 z-20">
        <button
          onClick={onToggleFocusMode}
          title={isFocusMode ? 'Exit Focus Mode [ESC]' : 'Enter Focus Mode'}
          className={`flex items-center gap-1.5 px-3 py-1.5 rounded-xl font-mono text-[10px] font-bold border transition-all cursor-pointer shadow-lg ${
            isFocusMode
              ? 'bg-cyan-500/20 border-cyan-400 text-cyan-200 shadow-cyan-500/20'
              : 'bg-slate-950/85 border-indigo-500/25 text-slate-300 hover:text-white hover:border-indigo-500/45'
          }`}
        >
          <span className="w-1.5 h-1.5 rounded-full bg-cyan-400 alert-dot" />
          <span>{isFocusMode ? 'FOCUS ACTIVE' : 'FOCUS MODE'}</span>
        </button>
      </div>

      {/* Active Run Status Pill */}
      {activeRunStatus && (activeRunStatus === 'running' || activeRunStatus === 'queued') && (
        <div className="absolute top-3 left-1/2 -translate-x-1/2 z-20">
          <div className="flex items-center gap-2 px-4 py-1.5 rounded-full bg-amber-950/90 border border-amber-500/40 backdrop-blur-md shadow-lg shadow-amber-950/50">
            <div className="w-2 h-2 rounded-full bg-amber-400 alert-dot" />
            <span className="text-[10px] font-mono font-bold text-amber-200 uppercase tracking-widest">
              SIMULATION {activeRunStatus}
            </span>
          </div>
        </div>
      )}

      {/* Bottom Center Model Label */}
      <div className="absolute bottom-3 left-1/2 -translate-x-1/2 z-10 pointer-events-none">
        <div className="px-3.5 py-1 rounded-full bg-slate-950/85 border border-indigo-500/20 text-[9px] font-mono font-bold uppercase tracking-[0.25em] text-indigo-300/90 backdrop-blur-md">
          CORTICAL MICROCIRCUIT MODEL · CONCEPTUAL VISUALIZATION
        </div>
      </div>

      {/* Bottom Left Legend */}
      <div className="absolute bottom-3 left-3 z-10 pointer-events-none flex items-center gap-4 bg-slate-950/75 border border-indigo-500/15 px-3 py-1.5 rounded-xl backdrop-blur-md">
        <div className="flex items-center gap-1.5">
          <div className="w-0 h-0 border-l-[4px] border-l-transparent border-r-[4px] border-r-transparent border-b-[8px] border-b-indigo-400" />
          <span className="text-[9px] font-mono text-slate-300">Pyramidal (Excitatory ▶)</span>
        </div>
        <div className="flex items-center gap-1.5">
          <div className="w-2.5 h-2.5 rounded-full bg-emerald-400 border border-emerald-200" />
          <span className="text-[9px] font-mono text-slate-300">Interneuron (Inhibitory ∅)</span>
        </div>
      </div>

      {/* Hover Tooltip */}
      {tooltipNode && tooltip.nodeId && (
        <div
          className="absolute z-30 pointer-events-none"
          style={{
            left:      tooltip.cx + 18,
            top:       Math.max(8, tooltip.cy - 16),
            transform: tooltip.cx > W * 0.65 ? 'translateX(-100%)' : undefined,
          }}
        >
          <div className="bg-slate-950/98 border border-indigo-500/30 rounded-xl p-3 min-w-[170px] backdrop-blur-2xl shadow-2xl">
            <div className="text-[10px] font-mono font-bold text-indigo-300 uppercase tracking-widest">
              {tooltipNode.shortLabel}
            </div>
            <div className="mt-1 text-xs text-white font-semibold">
              {tooltipNode.fullLabel}
            </div>
            <div className={`mt-1.5 text-[10px] font-mono ${tooltipNode.type === 'PYR' ? 'text-indigo-400' : tooltipNode.type === 'INT' ? 'text-emerald-400' : 'text-indigo-200'}`}>
              {tooltipNode.type === 'PYR' ? '▲ Pyramidal Soma' : tooltipNode.type === 'INT' ? '▼ Interneuron Spikes' : '◎ Central Hub'}
            </div>
            <div className="mt-0.5 text-[9px] font-mono text-slate-400">
              {tooltipNode.connections.length} synaptic pathways
            </div>
            <div className="mt-1 text-[8px] font-mono text-slate-500 uppercase">
              Click to fire burst cascade
            </div>
          </div>
        </div>
      )}

      {/* Selected Node Detailed Inspector */}
      {selNode && (
        <div className="absolute top-3 left-3 z-30">
          <div className="bg-slate-950/98 border border-indigo-400/40 rounded-xl p-4 min-w-[210px] backdrop-blur-2xl shadow-2xl">
            <div className="flex items-center gap-2 mb-2.5">
              <div className={`w-2.5 h-2.5 rounded-full ${selNode.type === 'PYR' ? 'bg-indigo-400' : selNode.type === 'INT' ? 'bg-emerald-400' : 'bg-indigo-200'}`} />
              <span className="text-xs font-mono font-bold text-white uppercase tracking-wider">
                {selNode.shortLabel}
              </span>
            </div>
            <div className="space-y-1.5 text-[10px] font-mono">
              <div className="flex justify-between">
                <span className="text-slate-400">Cell Type</span>
                <span className={selNode.type === 'PYR' ? 'text-indigo-300 font-bold' : selNode.type === 'INT' ? 'text-emerald-300 font-bold' : 'text-indigo-100'}>
                  {selNode.type === 'PYR' ? 'Excitatory' : selNode.type === 'INT' ? 'Inhibitory' : 'Hub'}
                </span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Layer</span>
                <span className="text-slate-200">{selNode.layer}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Pathways</span>
                <span className="text-white font-bold">{selNode.connections.length}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Firing State</span>
                <span className="text-emerald-400 font-bold">CASCADE ACTIVE</span>
              </div>
            </div>
            <button
              onClick={() => onSelectNode && onSelectNode(null)}
              className="mt-3 text-[9px] font-mono text-slate-400 hover:text-indigo-300 underline cursor-pointer block"
            >
              ← Clear Selection [ESC]
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
