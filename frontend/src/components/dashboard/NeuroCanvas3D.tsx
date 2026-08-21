import React, { useEffect, useRef, useState, useCallback } from 'react';
import * as THREE from 'three';
import {
  RotateCcw,
  RefreshCw,
  ZoomIn,
  ZoomOut,
  Crosshair,
  Settings,
  Brain,
  Dna,
  Plus,
  Cpu,
  Zap,
  Sliders,
  FlaskConical,
  LineChart,
} from 'lucide-react';

export interface HelixSegmentData {
  id: string;
  name: string;
  type: 'PYR' | 'INT' | 'CORE';
  layer: string;
  position: THREE.Vector3;
  depolarization: number;
  activity: number;
  statusTag: string;
  statusTone: 'safe' | 'warning' | 'danger';
  colorHex: string;
}

export interface NeuroCanvas3DProps {
  isRunning?: boolean;
  selectedNeuronId?: string | null;
  onSelectNeuron?: (neuron: HelixSegmentData | null) => void;
  activeSubsystem?: string;
  onSelectSubsystem?: (tabId: string) => void;
  channelHighlight?: string | null;
  receptorHighlight?: string | null;
  isSimulating?: boolean;
  hasActiveData?: boolean;
  firingRateHz?: number | null;
  niiScore?: number | null;
  gabaOccupancy?: number | null;
  nmdaOccupancy?: number | null;
  riskLevel?: string | null;
}

const SUBSYSTEM_TABS = [
  { id: 'critical',     label: 'Critical L5',   icon: Cpu          },
  { id: 'channels',     label: 'Ion Channels',  icon: Zap          },
  { id: 'receptors',    label: 'Receptors',     icon: Sliders      },
  { id: 'microcircuit', label: 'Microcircuit',  icon: FlaskConical },
  { id: 'network',      label: '1000+ Neurons', icon: LineChart    },
];

// ── Ring / Halo Sprite Texture Generator (Exact Pic 2 Concentric Halo Nodes) ──
function createNodeHaloTexture(colorRgba: string): THREE.CanvasTexture {
  const size = 128;
  const cv = document.createElement('canvas');
  cv.width = cv.height = size;
  const ctx = cv.getContext('2d')!;
  const r = size / 2;

  // Outer soft glow
  const g = ctx.createRadialGradient(r, r, 0, r, r, r);
  g.addColorStop(0.0, 'rgba(255, 255, 255, 1.0)');
  g.addColorStop(0.20, colorRgba);
  g.addColorStop(0.50, colorRgba.replace(/[\d\.]+\)$/, '0.35)'));
  g.addColorStop(0.85, colorRgba.replace(/[\d\.]+\)$/, '0.08)'));
  g.addColorStop(1.0, 'rgba(0, 0, 0, 0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, size, size);

  // Concentric thin bright ring (Pic 2 characteristic)
  ctx.beginPath();
  ctx.arc(r, r, r * 0.42, 0, Math.PI * 2);
  ctx.strokeStyle = colorRgba;
  ctx.lineWidth = 2.5;
  ctx.stroke();

  return new THREE.CanvasTexture(cv);
}

// ── Sharp Core Bead Texture ──
function createCoreBeadTexture(): THREE.CanvasTexture {
  const size = 64;
  const cv = document.createElement('canvas');
  cv.width = cv.height = size;
  const ctx = cv.getContext('2d')!;
  const r = size / 2;
  const g = ctx.createRadialGradient(r, r, 0, r, r, r);
  g.addColorStop(0.0, 'rgba(255, 255, 255, 1.0)');
  g.addColorStop(0.40, 'rgba(255, 255, 255, 0.9)');
  g.addColorStop(0.70, 'rgba(255, 255, 255, 0.25)');
  g.addColorStop(1.0, 'rgba(255, 255, 255, 0.0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, size, size);
  return new THREE.CanvasTexture(cv);
}

function projectToScreen(
  worldPos: THREE.Vector3,
  camera: THREE.PerspectiveCamera,
  w: number,
  h: number
): { x: number; y: number } {
  const v = worldPos.clone().project(camera);
  return { x: (v.x * 0.5 + 0.5) * w, y: (-v.y * 0.5 + 0.5) * h };
}

interface HudNodeDef {
  id: string;
  codeTag: string;
  title: string;
  subtitle: string;
  badge: { label: string; tone: 'safe' | 'warning' | 'danger' | 'info' };
  worldPos: [number, number, number];
  offset: { dx: number; dy: number };
}

const HUD_NODES: HudNodeDef[] = [
  {
    id: 'pyr-l5',
    codeTag: 'L5-PYR',
    title: 'PYRAMIDAL NEURON',
    subtitle: 'Layer 5 · Apical Integration · 68.3 Hz',
    badge: { label: 'DEEP INTEGRATION', tone: 'info' },
    worldPos: [-4.2, 3.4, 0.2],
    offset: { dx: -180, dy: -50 },
  },
  {
    id: 'l2-pyr',
    codeTag: 'NEX-07',
    title: 'L2/3 PYRAMIDAL SOMA',
    subtitle: 'Mem. Potential: -62.1 mV · Spines Active',
    badge: { label: 'EXCITATORY SYNC', tone: 'safe' },
    worldPos: [4.2, 3.6, -0.2],
    offset: { dx: 45, dy: -45 },
  },
  {
    id: 'fast-int',
    codeTag: 'PRG-14',
    title: 'FAST-SPIKING INTERNEURON',
    subtitle: 'Parvalbumin PV+ · Firing: 23.1 Hz',
    badge: { label: 'GABA BUFFERED', tone: 'safe' },
    worldPos: [-4.5, 0.4, 0.2],
    offset: { dx: -180, dy: -20 },
  },
  {
    id: 'somato-int',
    codeTag: 'TX-27',
    title: 'SOMATOSTATIN+ CELL',
    subtitle: 'Inhibitory Feedback · Activity: 23.1 Hz',
    badge: { label: 'STABLE FEEDBACK', tone: 'safe' },
    worldPos: [-3.4, -3.2, -0.3],
    offset: { dx: -180, dy: 20 },
  },
  {
    id: 'core-hub',
    codeTag: 'HUB-01',
    title: 'MICROCIRCUIT CORE',
    subtitle: 'E/I: 0.72 · Global Phase Sync: 0.84',
    badge: { label: 'CONVERGENT', tone: 'safe' },
    worldPos: [0, 0, 0],
    offset: { dx: 50, dy: -20 },
  },
  {
    id: 'l5-out',
    codeTag: 'OUT-5B',
    title: 'L5 PYRAMIDAL OUTPUT',
    subtitle: 'Firing Rate: 24.1 Hz · Burst CV: 15%',
    badge: { label: 'STABLE OUTPUT', tone: 'safe' },
    worldPos: [3.8, -3.2, 0.2],
    offset: { dx: 45, dy: 20 },
  },
];

interface SubsystemModeDef {
  id: string;
  name: string;
  badge: string;
  scaleLabel: string;
  description: string;
  metrics: { label: string; value: string; color: string }[];
  camPos: [number, number, number];
  targetNodeIds: string[];
  colorTheme: string;
  glowIntensity: number;
  pulseSpeedMultiplier: number;
  populationSwarm: boolean;
  synapticRipples: boolean;
}

const SUBSYSTEM_MODES: Record<string, SubsystemModeDef> = {
  cortex: {
    id: 'cortex',
    name: 'CRITICAL L5 PYRAMIDAL LAYER',
    badge: 'LAYER 5 DEEP BURST',
    scaleLabel: '100 µm · Single Multicompartment Unit',
    description: 'Deep somatic integration & high-frequency apical burst output firing across cortical columns.',
    metrics: [
      { label: 'Somatic Burst', value: '68.3 Hz', color: '#38BDF8' },
      { label: 'Apical Potential', value: '-58.4 mV', color: '#00F5D4' },
      { label: 'AP Backpropagation', value: '1.2 m/s', color: '#A855F7' },
      { label: 'L5 Out State', value: 'STABLE', color: '#22C55E' },
    ],
    camPos: [-2.0, 1.2, 8.2],
    targetNodeIds: ['pyr-l5', 'l5-out'],
    colorTheme: '#00F5D4',
    glowIntensity: 36,
    pulseSpeedMultiplier: 1.8,
    populationSwarm: false,
    synapticRipples: false,
  },
  critical: {
    id: 'cortex',
    name: 'CRITICAL L5 PYRAMIDAL LAYER',
    badge: 'LAYER 5 DEEP BURST',
    scaleLabel: '100 µm · Single Multicompartment Unit',
    description: 'Deep somatic integration & high-frequency apical burst output firing across cortical columns.',
    metrics: [
      { label: 'Somatic Burst', value: '68.3 Hz', color: '#38BDF8' },
      { label: 'Apical Potential', value: '-58.4 mV', color: '#00F5D4' },
      { label: 'AP Backpropagation', value: '1.2 m/s', color: '#A855F7' },
      { label: 'L5 Out State', value: 'STABLE', color: '#22C55E' },
    ],
    camPos: [-2.0, 1.2, 8.2],
    targetNodeIds: ['pyr-l5', 'l5-out'],
    colorTheme: '#00F5D4',
    glowIntensity: 36,
    pulseSpeedMultiplier: 1.8,
    populationSwarm: false,
    synapticRipples: false,
  },
  channels: {
    id: 'channels',
    name: 'VOLTAGE-GATED ION CHANNEL FLUX',
    badge: 'IONIC GATING PIPELINE',
    scaleLabel: '10 nm · Channel Pores & Subunits',
    description: 'Real-time Na+ influx, K+ rectifier repolarization efflux, and dendritic Ca2+ spikes.',
    metrics: [
      { label: 'gNa Conductance', value: '120 mS/cm²', color: '#38BDF8' },
      { label: 'gK Rectifier', value: '36 mS/cm²', color: '#C084FC' },
      { label: 'gCa Spikes', value: '1.5 mS/cm²', color: '#FACC15' },
      { label: 'Channel Block', value: '64% Bound', color: '#00F5D4' },
    ],
    camPos: [0.0, 0.0, 10.2],
    targetNodeIds: ['l2-pyr', 'fast-int'],
    colorTheme: '#38BDF8',
    glowIntensity: 22,
    pulseSpeedMultiplier: 3.6,
    populationSwarm: false,
    synapticRipples: false,
  },
  receptors: {
    id: 'receptors',
    name: 'SYNAPTIC RECEPTOR DENSITY & E/I',
    badge: 'POSTSYNAPTIC DENSITY',
    scaleLabel: '20 nm · Synaptic Spines & Cleft',
    description: 'Postsynaptic NMDA, GABA-A, and AMPA receptor kinetics governing excitation/inhibition equilibrium.',
    metrics: [
      { label: 'NMDA Binding', value: '85% Active', color: '#00F5D4' },
      { label: 'GABA-A Influx', value: '72% Buffered', color: '#D946EF' },
      { label: 'E/I Synaptic Ratio', value: '0.72 (Norm)', color: '#22C55E' },
      { label: 'Spines Tracked', value: '348 Spines', color: '#38BDF8' },
    ],
    camPos: [1.8, 1.2, 9.0],
    targetNodeIds: ['somato-int', 'l2-pyr'],
    colorTheme: '#D946EF',
    glowIntensity: 26,
    pulseSpeedMultiplier: 1.2,
    populationSwarm: false,
    synapticRipples: true,
  },
  microcircuit: {
    id: 'microcircuit',
    name: 'CANONICAL CORTICAL COLUMN',
    badge: 'MICROCIRCUIT ARCHITECTURE',
    scaleLabel: '500 µm · Canonical Columnar Layering',
    description: 'Integrated cortical column: L2/3 Pyr ↔ L5 Pyr ↔ Fast-Spiking PV+ ↔ Somatostatin SST+ loop.',
    metrics: [
      { label: 'Feedforward Excitation', value: 'Intact (0.84)', color: '#38BDF8' },
      { label: 'Feedback Inhibition', value: 'Buffered', color: '#D946EF' },
      { label: 'Column Synchrony', value: '0.84 Sync', color: '#22C55E' },
      { label: 'Circuit Phase', value: 'CONVERGENT', color: '#00F5D4' },
    ],
    camPos: [0.0, 0.0, 13.5],
    targetNodeIds: ['pyr-l5', 'l2-pyr', 'fast-int', 'somato-int', 'l5-out'],
    colorTheme: '#6366F1',
    glowIntensity: 18,
    pulseSpeedMultiplier: 1.0,
    populationSwarm: false,
    synapticRipples: false,
  },
  ode: {
    id: 'ode',
    name: 'POPULATION NEURAL NETWORK (1,000+ ODE UNITS)',
    badge: 'MACRO POPULATION SWARM',
    scaleLabel: '5.0 mm · Macroscopic Cortical Tissue',
    description: '1,024 ODE-coupled compartmental neural units with macroscopic traveling phase synchrony waves.',
    metrics: [
      { label: 'Population Size', value: '1,024 Units', color: '#38BDF8' },
      { label: 'Global Phase Coherence', value: '0.84 Sync', color: '#22C55E' },
      { label: 'Mean Field Potential', value: '-64.2 mV', color: '#00F5D4' },
      { label: 'Instability Index (NII)', value: '0.040 (Safe)', color: '#FACC15' },
    ],
    camPos: [0.0, 0.0, 24.0],
    targetNodeIds: ['pyr-l5', 'l5-out'],
    colorTheme: '#A855F7',
    glowIntensity: 12,
    pulseSpeedMultiplier: 0.8,
    populationSwarm: true,
    synapticRipples: false,
  },
  network: {
    id: 'ode',
    name: 'POPULATION NEURAL NETWORK (1,000+ ODE UNITS)',
    badge: 'MACRO POPULATION SWARM',
    scaleLabel: '5.0 mm · Macroscopic Cortical Tissue',
    description: '1,024 ODE-coupled compartmental neural units with macroscopic traveling phase synchrony waves.',
    metrics: [
      { label: 'Population Size', value: '1,024 Units', color: '#38BDF8' },
      { label: 'Global Phase Coherence', value: '0.84 Sync', color: '#22C55E' },
      { label: 'Mean Field Potential', value: '-64.2 mV', color: '#00F5D4' },
      { label: 'Instability Index (NII)', value: '0.040 (Safe)', color: '#FACC15' },
    ],
    camPos: [0.0, 0.0, 24.0],
    targetNodeIds: ['pyr-l5', 'l5-out'],
    colorTheme: '#A855F7',
    glowIntensity: 12,
    pulseSpeedMultiplier: 0.8,
    populationSwarm: true,
    synapticRipples: false,
  },
};

export function NeuroCanvas3D(props: NeuroCanvas3DProps) {
  const {
    isRunning: _isRunning = true,
    hasActiveData = true,
    activeSubsystem = 'microcircuit',
    onSelectSubsystem,
    firingRateHz = null,
    niiScore = null,
    gabaOccupancy = null,
    nmdaOccupancy = null,
    riskLevel = null,
  } = props;

  const currentModeInfo = SUBSYSTEM_MODES[activeSubsystem] || SUBSYSTEM_MODES.microcircuit;

  const handleSubsystemSelect = (tabId: string) => {
    onSelectSubsystem?.(tabId);
  };

  const containerRef = useRef<HTMLDivElement>(null);
  const targetCamPosRef = useRef<[number, number, number]>(currentModeInfo.camPos);
  const activeSubsystemRef = useRef<string>(activeSubsystem);
  const [selectedBottomTab, setSelectedBottomTab] = useState<'soma' | 'interneuron'>('soma');
  const [activeTool, setActiveTool] = useState<string>('rotate');

  useEffect(() => {
    activeSubsystemRef.current = activeSubsystem;
    const info = SUBSYSTEM_MODES[activeSubsystem] || SUBSYSTEM_MODES.microcircuit;
    targetCamPosRef.current = info.camPos;
  }, [activeSubsystem]);

  const [calloutPositions, setCalloutPositions] = useState<
    Record<string, { ax: number; ay: number; cx: number; cy: number }>
  >({});

  const reprojectCallouts = useCallback(
    (camera: THREE.PerspectiveCamera, w: number, h: number) => {
      const next: Record<string, { ax: number; ay: number; cx: number; cy: number }> = {};
      for (const node of HUD_NODES) {
        const s = projectToScreen(new THREE.Vector3(...node.worldPos), camera, w, h);
        const isLeft = node.offset.dx < 0 || s.x < w * 0.5;

        // Clean perimeter spacing with bounded rows
        let cx = 0, cy = 0;
        if (node.id === 'pyr-l5') {
          cx = Math.max(38, Math.min(w * 0.22, s.x - 170));
          cy = Math.max(44, Math.min(h * 0.26, s.y - 45));
        } else if (node.id === 'fast-int') {
          cx = Math.max(38, Math.min(w * 0.22, s.x - 170));
          cy = Math.max(h * 0.35, Math.min(h * 0.55, s.y - 20));
        } else if (node.id === 'somato-int') {
          cx = Math.max(38, Math.min(w * 0.24, s.x - 170));
          cy = Math.max(h * 0.62, Math.min(h - 96, s.y + 10));
        } else if (node.id === 'l2-pyr') {
          cx = Math.min(w - 185, Math.max(w * 0.72, s.x + 35));
          cy = Math.max(44, Math.min(h * 0.26, s.y - 45));
        } else if (node.id === 'core-hub') {
          cx = Math.min(w - 185, Math.max(w * 0.70, s.x + 40));
          cy = Math.max(h * 0.35, Math.min(h * 0.55, s.y - 15));
        } else if (node.id === 'l5-out') {
          cx = Math.min(w - 185, Math.max(w * 0.72, s.x + 35));
          cy = Math.max(h * 0.62, Math.min(h - 96, s.y + 10));
        } else {
          cx = isLeft ? Math.max(38, s.x + node.offset.dx) : Math.min(w - 185, s.x + node.offset.dx);
          cy = Math.max(44, Math.min(h - 96, s.y + node.offset.dy));
        }

        next[node.id] = { ax: s.x, ay: s.y, cx, cy };
      }
      setCalloutPositions(next);
    },
    []
  );

  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    let cW = container.clientWidth || window.innerWidth;
    let cH = container.clientHeight || window.innerHeight;

    // ── Three.js Scene Setup ──
    const scene = new THREE.Scene();
    scene.fog = new THREE.FogExp2(0x02040a, 0.012);

    const camera = new THREE.PerspectiveCamera(45, cW / cH, 0.1, 500);
    camera.position.set(targetCamPosRef.current[0], targetCamPosRef.current[1], targetCamPosRef.current[2]);

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(cW, cH);
    renderer.setClearColor(0x000000, 0);
    container.appendChild(renderer.domElement);

    // ── Textures ──
    const cyanHaloTex = createNodeHaloTexture('rgba(0, 245, 212, 1.0)');
    const purpleHaloTex = createNodeHaloTexture('rgba(217, 70, 239, 1.0)');
    const beadTex = createCoreBeadTexture();

    // ── Lights ──
    scene.add(new THREE.AmbientLight(0x050c18, 2.0));

    const coreLight = new THREE.PointLight(0xd946ef, 14, 25);
    coreLight.position.set(0, 0, 1.5);
    scene.add(coreLight);

    const cyanLightTop = new THREE.PointLight(0x00f5d4, 10, 30);
    cyanLightTop.position.set(4, 4, 3);
    scene.add(cyanLightTop);

    const cyanLightBottom = new THREE.PointLight(0x38bdf8, 10, 30);
    cyanLightBottom.position.set(-4, -4, 3);
    scene.add(cyanLightBottom);

    // ══════════════════════════════════════════════════════════════════════
    // ── 1. Volumetric Bioluminescent Starfield & Large Bokeh Field ──
    // ══════════════════════════════════════════════════════════════════════
    const STAR_COUNT = 3200;
    const starPos = new Float32Array(STAR_COUNT * 3);
    const starCols = new Float32Array(STAR_COUNT * 3);

    for (let i = 0; i < STAR_COUNT; i++) {
      const r = 2.0 + Math.pow(Math.random(), 0.70) * 85.0;
      const theta = Math.random() * Math.PI * 2;
      const phi = (Math.random() - 0.5) * Math.PI * 0.95;

      starPos[i * 3]     = Math.cos(theta) * Math.cos(phi) * r;
      starPos[i * 3 + 1] = Math.sin(phi) * r * 0.85;
      starPos[i * 3 + 2] = Math.sin(theta) * Math.cos(phi) * r * 0.8 - 4.0 + (Math.random() - 0.5) * 35.0;

      const rand = Math.random();
      let col: THREE.Color;
      if (rand < 0.40) col = new THREE.Color(0x00f5d4); // Cyan
      else if (rand < 0.65) col = new THREE.Color(0x38bdf8); // Sky blue
      else if (rand < 0.80) col = new THREE.Color(0x818cf8); // Indigo
      else if (rand < 0.93) col = new THREE.Color(0xd946ef); // Magenta
      else col = new THREE.Color(0xfbbf24); // Warm Gold

      const brightness = 0.25 + Math.random() * 0.75;
      starCols[i * 3]     = col.r * brightness;
      starCols[i * 3 + 1] = col.g * brightness;
      starCols[i * 3 + 2] = col.b * brightness;
    }

    const starGeo = new THREE.BufferGeometry();
    starGeo.setAttribute('position', new THREE.BufferAttribute(starPos, 3));
    starGeo.setAttribute('color', new THREE.BufferAttribute(starCols, 3));

    const starfield = new THREE.Points(
      starGeo,
      new THREE.PointsMaterial({
        size: 0.16,
        map: beadTex,
        vertexColors: true,
        transparent: true,
        opacity: 0.65,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
        sizeAttenuation: true,
      })
    );
    scene.add(starfield);

    // Large floating glowing bokeh orbs
    const BOKEH_COUNT = 280;
    const bokehPos = new Float32Array(BOKEH_COUNT * 3);
    const bokehCols = new Float32Array(BOKEH_COUNT * 3);
    for (let i = 0; i < BOKEH_COUNT; i++) {
      const r = 4.0 + Math.pow(Math.random(), 0.8) * 65.0;
      const theta = Math.random() * Math.PI * 2;
      const phi = (Math.random() - 0.5) * Math.PI * 0.9;
      bokehPos[i * 3]     = Math.cos(theta) * Math.cos(phi) * r;
      bokehPos[i * 3 + 1] = Math.sin(phi) * r * 0.75;
      bokehPos[i * 3 + 2] = Math.sin(theta) * Math.cos(phi) * r * 0.7 - 6.0 + (Math.random() - 0.5) * 20.0;
      const c = Math.random() < 0.6 ? new THREE.Color(0x00f5d4) : new THREE.Color(0xd946ef);
      bokehCols[i * 3]     = c.r * 0.45;
      bokehCols[i * 3 + 1] = c.g * 0.45;
      bokehCols[i * 3 + 2] = c.b * 0.45;
    }
    const bokehGeo = new THREE.BufferGeometry();
    bokehGeo.setAttribute('position', new THREE.BufferAttribute(bokehPos, 3));
    bokehGeo.setAttribute('color', new THREE.BufferAttribute(bokehCols, 3));
    const largeBokehCloud = new THREE.Points(
      bokehGeo,
      new THREE.PointsMaterial({
        size: 0.48,
        map: cyanHaloTex,
        vertexColors: true,
        transparent: true,
        opacity: 0.40,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
        sizeAttenuation: true,
      })
    );
    scene.add(largeBokehCloud);

    // ── Distant Synaptogenesis Constellation Web (Background Neural Field) ──
    const distantWebGroup = new THREE.Group();
    scene.add(distantWebGroup);

    const SATELLITE_COUNT = 64;
    const satPositions: THREE.Vector3[] = [];
    for (let i = 0; i < SATELLITE_COUNT; i++) {
      const rad = 12.0 + Math.random() * 45.0;
      const ang = (i / SATELLITE_COUNT) * Math.PI * 2 + (Math.random() - 0.5) * 0.35;
      const z = -14.0 - Math.random() * 30.0;
      satPositions.push(new THREE.Vector3(Math.cos(ang) * rad, Math.sin(ang) * rad * 0.85, z));
    }

    const webLinesPos: number[] = [];
    const webLinesCols: number[] = [];
    for (let i = 0; i < SATELLITE_COUNT; i++) {
      for (let j = i + 1; j < SATELLITE_COUNT; j++) {
        const d = satPositions[i].distanceTo(satPositions[j]);
        if (d < 18.0) {
          webLinesPos.push(satPositions[i].x, satPositions[i].y, satPositions[i].z);
          webLinesPos.push(satPositions[j].x, satPositions[j].y, satPositions[j].z);
          const c = new THREE.Color(Math.random() < 0.6 ? 0x0284c7 : 0x6366f1);
          webLinesCols.push(c.r * 0.22, c.g * 0.22, c.b * 0.22);
          webLinesCols.push(c.r * 0.22, c.g * 0.22, c.b * 0.22);
        }
      }
    }

    const webGeo = new THREE.BufferGeometry();
    webGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(webLinesPos), 3));
    webGeo.setAttribute('color', new THREE.BufferAttribute(new Float32Array(webLinesCols), 3));
    distantWebGroup.add(
      new THREE.LineSegments(
        webGeo,
        new THREE.LineBasicMaterial({
          vertexColors: true,
          transparent: true,
          opacity: 0.40,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        })
      )
    );

    // ── Holographic Biophysical Coordinate Reticle Rings (Sci-Fi Deep Background) ──
    const holoGridGroup = new THREE.Group();
    scene.add(holoGridGroup);
    const ringRadii = [8, 16, 28, 42, 60];
    for (const rr of ringRadii) {
      const segs = 64;
      const pts: THREE.Vector3[] = [];
      for (let s = 0; s <= segs; s++) {
        const theta = (s / segs) * Math.PI * 2;
        pts.push(new THREE.Vector3(Math.cos(theta) * rr, Math.sin(theta) * rr * 0.85, -16));
      }
      const ringGeo = new THREE.BufferGeometry().setFromPoints(pts);
      holoGridGroup.add(
        new THREE.Line(
          ringGeo,
          new THREE.LineBasicMaterial({
            color: 0x0369a1,
            transparent: true,
            opacity: 0.18,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
          })
        )
      );
    }

    // ══════════════════════════════════════════════════════════════════════
    // ── 2. Master Biological Neuron Group (Pic 2 Exact Structure) ──
    // ══════════════════════════════════════════════════════════════════════
    const masterNeuronGroup = new THREE.Group();
    scene.add(masterNeuronGroup);

    // Shared storage for line segments and beaded nodes
    const allSegments: { start: THREE.Vector3; end: THREE.Vector3; color: THREE.Color }[] = [];
    const allBeads: { pos: THREE.Vector3; color: THREE.Color; size: number; hasHalo?: boolean; isPurple?: boolean }[] = [];

    // Helper: generate organic beaded multi-segmented branch
    const buildBeadedBranch = (
      pStart: THREE.Vector3,
      pEnd: THREE.Vector3,
      options: {
        steps?: number;
        curvature?: number;
        forkProb?: number;
        depth?: number;
      } = {}
    ) => {
      const steps = options.steps ?? 14;
      const curvature = options.curvature ?? 0.55;
      const forkProb = options.forkProb ?? 0.45;
      const depth = options.depth ?? 0;

      const dir = new THREE.Vector3().subVectors(pEnd, pStart);
      const totalLen = dir.length();
      const unitDir = dir.clone().normalize();

      const up = Math.abs(unitDir.z) < 0.9 ? new THREE.Vector3(0, 0, 1) : new THREE.Vector3(1, 0, 0);
      const perp1 = new THREE.Vector3().crossVectors(unitDir, up).normalize();
      const perp2 = new THREE.Vector3().crossVectors(unitDir, perp1).normalize();

      const sign = Math.random() > 0.5 ? 1 : -1;
      const cp1 = pStart.clone()
        .add(unitDir.clone().multiplyScalar(totalLen * 0.35))
        .add(perp1.clone().multiplyScalar(sign * (0.3 + Math.random() * curvature)))
        .add(perp2.clone().multiplyScalar((Math.random() - 0.5) * 0.3));

      const cp2 = pStart.clone()
        .add(unitDir.clone().multiplyScalar(totalLen * 0.70))
        .add(perp1.clone().multiplyScalar(-sign * (0.2 + Math.random() * (curvature * 0.7))))
        .add(perp2.clone().multiplyScalar((Math.random() - 0.5) * 0.3));

      let prevPt = pStart.clone();

      for (let s = 1; s <= steps; s++) {
        const t = s / steps;
        const u = 1 - t;
        const curPt = new THREE.Vector3(
          u * u * u * pStart.x + 3 * u * u * t * cp1.x + 3 * u * t * t * cp2.x + t * t * t * pEnd.x,
          u * u * u * pStart.y + 3 * u * u * t * cp1.y + 3 * u * t * t * cp2.y + t * t * t * pEnd.y,
          u * u * u * pStart.z + 3 * u * u * t * cp1.z + 3 * u * t * t * cp2.z + t * t * t * pEnd.z
        );

        // Distance from center (0,0,0) governs the purple -> cyan gradient
        const dist = curPt.length();
        let segColor: THREE.Color;
        let isPurple = false;

        if (dist < 1.7) {
          // Pure Neon Purple / Violet
          segColor = new THREE.Color(0xd946ef).lerp(new THREE.Color(0xa855f7), dist / 1.7);
          isPurple = true;
        } else if (dist < 2.9) {
          // Purple to Cyan transition
          const blend = (dist - 1.7) / 1.2;
          segColor = new THREE.Color(0xa855f7).lerp(new THREE.Color(0x00f5d4), blend);
        } else {
          // Pure Electric Cyan / Teal
          segColor = new THREE.Color(0x00f5d4).lerp(new THREE.Color(0x38bdf8), Math.min(1, (dist - 2.9) / 1.5));
        }

        allSegments.push({ start: prevPt, end: curPt, color: segColor });

        // Major halo node check (concentric halo orbs matching Pic 2)
        const hasHalo = (s === Math.floor(steps * 0.45) || s === Math.floor(steps * 0.80)) && Math.random() < 0.70;
        const size = hasHalo
          ? 0.18 + Math.random() * 0.06
          : 0.06 + (1 - t * 0.3) * 0.05;

        allBeads.push({
          pos: curPt.clone(),
          color: segColor,
          size,
          hasHalo,
          isPurple,
        });

        // Lateral branching fork
        if (depth < 2 && Math.random() < forkProb && s > 3 && s < steps - 2) {
          const branchAngle = (Math.random() > 0.5 ? 0.60 : -0.60) + (Math.random() - 0.5) * 0.2;
          const branchLen = totalLen * (0.35 + Math.random() * 0.35);
          const forkDir = unitDir.clone().applyAxisAngle(up, branchAngle);
          const forkTarget = curPt.clone().add(forkDir.multiplyScalar(branchLen)).add(new THREE.Vector3((Math.random() - 0.5) * 0.3, (Math.random() - 0.5) * 0.3, (Math.random() - 0.5) * 0.3));

          buildBeadedBranch(curPt, forkTarget, {
            steps: 7,
            curvature: 0.35,
            forkProb: 0.20,
            depth: depth + 1,
          });
        }

        prevPt = curPt;
      }
    };

    // ── A. CENTRAL BIOLOGICAL SOMA RING (Intricate fibrous coronet matching Pic 2) ──
    const SOMA_RING_POINTS = 16;
    const somaRingPts: THREE.Vector3[] = [];
    for (let i = 0; i < SOMA_RING_POINTS; i++) {
      const angle = (i / SOMA_RING_POINTS) * Math.PI * 2;
      const r = 0.72 + (Math.sin(angle * 4.0) * 0.12) + (Math.cos(angle * 3.0) * 0.08);
      somaRingPts.push(new THREE.Vector3(Math.cos(angle) * r, Math.sin(angle) * r, (Math.random() - 0.5) * 0.25));
    }

    // Interconnect soma ring perimeter with fibrous purple segments
    for (let i = 0; i < SOMA_RING_POINTS; i++) {
      const nextIdx = (i + 1) % SOMA_RING_POINTS;
      const p1 = somaRingPts[i];
      const p2 = somaRingPts[nextIdx];
      const pMid = new THREE.Vector3().lerpVectors(p1, p2, 0.5).add(new THREE.Vector3((Math.random() - 0.5) * 0.1, (Math.random() - 0.5) * 0.1, (Math.random() - 0.5) * 0.1));

      const col = new THREE.Color(0xd946ef);
      allSegments.push({ start: p1, end: pMid, color: col });
      allSegments.push({ start: pMid, end: p2, color: col });

      // Internal cross-filaments across soma core
      if (i % 2 === 0) {
        const crossIdx = (i + 5) % SOMA_RING_POINTS;
        const pCross = somaRingPts[crossIdx];
        const centerOffset = new THREE.Vector3((Math.random() - 0.5) * 0.2, (Math.random() - 0.5) * 0.2, (Math.random() - 0.5) * 0.2);
        allSegments.push({ start: p1, end: centerOffset, color: new THREE.Color(0xc084fc) });
        allSegments.push({ start: centerOffset, end: pCross, color: new THREE.Color(0xc084fc) });
      }

      // Soma surface nodes
      allBeads.push({
        pos: p1.clone(),
        color: new THREE.Color(0xf0abfc),
        size: 0.10 + Math.random() * 0.05,
        hasHalo: i % 3 === 0,
        isPurple: true,
      });
    }

    // ── B. 9 PRIMARY BEADED DENDRITIC TRUNKS RADIATING FROM SOMA (Exact Pic 2 Layout) ──
    const PRIMARY_TARGETS = [
      new THREE.Vector3(-4.2,  3.4,  0.2), // -> Pyramidal Neuron
      new THREE.Vector3(-1.5,  4.6, -0.4), // -> Top-left reach
      new THREE.Vector3( 1.4,  4.5, -0.3), // -> Top reach
      new THREE.Vector3( 4.2,  3.6, -0.2), // -> L2/3 Pyramidal Soma
      new THREE.Vector3( 4.6,  0.4,  0.3), // -> Mid-right reach
      new THREE.Vector3( 3.8, -3.2,  0.2), // -> L5 Pyramidal Output
      new THREE.Vector3( 0.4, -4.5, -0.4), // -> Bottom reach
      new THREE.Vector3(-3.4, -3.2, -0.3), // -> Somatostatin+ Interneuron
      new THREE.Vector3(-4.5,  0.4,  0.2), // -> Fast-Spiking Int
    ];

    PRIMARY_TARGETS.forEach((target, idx) => {
      // Find nearest soma ring point
      let bestSomaPt = somaRingPts[0];
      let minD = 999;
      somaRingPts.forEach((pt) => {
        const d = pt.distanceTo(target);
        if (d < minD) {
          minD = d;
          bestSomaPt = pt;
        }
      });

      // Main trunk
      buildBeadedBranch(bestSomaPt, target, {
        steps: 16,
        curvature: 0.70,
        forkProb: 0.65,
        depth: 0,
      });

      // Intertwined secondary neurite
      const subTarget = target.clone().add(
        new THREE.Vector3((Math.random() - 0.5) * 1.5, (Math.random() - 0.5) * 1.5, (Math.random() - 0.5) * 0.8)
      );
      const altSomaPt = somaRingPts[(idx * 2 + 1) % SOMA_RING_POINTS];
      buildBeadedBranch(altSomaPt, subTarget, {
        steps: 12,
        curvature: 0.55,
        forkProb: 0.40,
        depth: 0,
      });
    });

    // ── C. 5 SATELLITE MULTIPOLAR NEURONS (Cyan Star-shaped cells at outer hubs) ──
    const SATELLITE_HUBS = [
      { pos: new THREE.Vector3(-4.2,  3.4,  0.2), color: 0x00f5d4, size: 0.32 },
      { pos: new THREE.Vector3( 4.2,  3.6, -0.2), color: 0x38bdf8, size: 0.34 },
      { pos: new THREE.Vector3(-4.5,  0.4,  0.2), color: 0x22d3ee, size: 0.30 },
      { pos: new THREE.Vector3(-3.4, -3.2, -0.3), color: 0x00f5d4, size: 0.30 },
      { pos: new THREE.Vector3( 3.8, -3.2,  0.2), color: 0xc084fc, size: 0.32 }, // Bottom-right violet/cyan
    ];

    SATELLITE_HUBS.forEach((hub) => {
      // Mini satellite soma ring
      const SAT_PTS = 8;
      const satPts: THREE.Vector3[] = [];
      for (let s = 0; s < SAT_PTS; s++) {
        const ang = (s / SAT_PTS) * Math.PI * 2;
        const r = hub.size * 0.9 + (Math.sin(ang * 3) * 0.05);
        satPts.push(hub.pos.clone().add(new THREE.Vector3(Math.cos(ang) * r, Math.sin(ang) * r, (Math.random() - 0.5) * 0.15)));
      }

      for (let s = 0; s < SAT_PTS; s++) {
        const next = (s + 1) % SAT_PTS;
        allSegments.push({ start: satPts[s], end: satPts[next], color: new THREE.Color(hub.color) });
        allBeads.push({
          pos: satPts[s].clone(),
          color: new THREE.Color(hub.color),
          size: 0.08,
          hasHalo: s % 2 === 0,
        });

        // Radiating satellite dendrites
        const rayAngle = (s / SAT_PTS) * Math.PI * 2 + (Math.random() - 0.5) * 0.3;
        const rayLen = 0.8 + Math.random() * 1.1;
        const rayEnd = hub.pos.clone().add(new THREE.Vector3(Math.cos(rayAngle) * rayLen, Math.sin(rayAngle) * rayLen, (Math.random() - 0.5) * 0.5));
        buildBeadedBranch(satPts[s], rayEnd, {
          steps: 6,
          curvature: 0.30,
          forkProb: 0.20,
          depth: 1,
        });
      }
    });

    // ── D. RENDER GLOWING FILAMENT STEM LINES ──
    const linePos: number[] = [];
    const lineCols: number[] = [];
    allSegments.forEach((seg) => {
      linePos.push(seg.start.x, seg.start.y, seg.start.z);
      linePos.push(seg.end.x, seg.end.y, seg.end.z);
      lineCols.push(seg.color.r, seg.color.g, seg.color.b);
      lineCols.push(seg.color.r, seg.color.g, seg.color.b);
    });

    const stemGeo = new THREE.BufferGeometry();
    stemGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(linePos), 3));
    stemGeo.setAttribute('color', new THREE.BufferAttribute(new Float32Array(lineCols), 3));

    // Core sharp glowing line
    masterNeuronGroup.add(
      new THREE.LineSegments(
        stemGeo,
        new THREE.LineBasicMaterial({
          vertexColors: true,
          transparent: true,
          opacity: 0.95,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        })
      )
    );

    // Subtle bloom pass
    masterNeuronGroup.add(
      new THREE.LineSegments(
        stemGeo,
        new THREE.LineBasicMaterial({
          vertexColors: true,
          transparent: true,
          opacity: 0.40,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        })
      )
    );

    // ── E. RENDER ALL BEADED NODES & HALO ORBS (Pic 2 Characteristic) ──
    const beadPos = new Float32Array(allBeads.length * 3);
    const beadCols = new Float32Array(allBeads.length * 3);

    allBeads.forEach((bead, i) => {
      beadPos[i * 3]     = bead.pos.x;
      beadPos[i * 3 + 1] = bead.pos.y;
      beadPos[i * 3 + 2] = bead.pos.z;
      beadCols[i * 3]     = bead.color.r;
      beadCols[i * 3 + 1] = bead.color.g;
      beadCols[i * 3 + 2] = bead.color.b;

      // Prominent concentric halo rings on key nodes (matching Pic 2)
      if (bead.hasHalo) {
        const haloSprite = new THREE.Sprite(
          new THREE.SpriteMaterial({
            map: bead.isPurple ? purpleHaloTex : cyanHaloTex,
            color: 0xffffff,
            transparent: true,
            opacity: 0.85,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
          })
        );
        haloSprite.position.copy(bead.pos);
        haloSprite.scale.set(bead.size * 2.8, bead.size * 2.8, 1);
        masterNeuronGroup.add(haloSprite);
      }
    });

    const beadGeo = new THREE.BufferGeometry();
    beadGeo.setAttribute('position', new THREE.BufferAttribute(beadPos, 3));
    beadGeo.setAttribute('color', new THREE.BufferAttribute(beadCols, 3));

    const beadPoints = new THREE.Points(
      beadGeo,
      new THREE.PointsMaterial({
        size: 0.24,
        map: beadTex,
        vertexColors: true,
        transparent: true,
        opacity: 0.95,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
        sizeAttenuation: true,
      })
    );
    masterNeuronGroup.add(beadPoints);

    // ── F. STREAMING ACTION POTENTIAL ENERGY PULSES (50 Clean Packets) ──
    const PULSE_COUNT = 50;
    const pulseProgress = new Float32Array(PULSE_COUNT).map(() => Math.random());
    const pulseSpeed = new Float32Array(PULSE_COUNT).map(() => 0.008 + Math.random() * 0.015);
    const pulseSegIdx = new Int32Array(PULSE_COUNT).map(() => Math.floor(Math.random() * allSegments.length));

    const pulsePos = new Float32Array(PULSE_COUNT * 3);
    const pulseGeo = new THREE.BufferGeometry();
    pulseGeo.setAttribute('position', new THREE.BufferAttribute(pulsePos, 3));
    const pulseMat = new THREE.PointsMaterial({
      size: 0.26,
      map: cyanHaloTex,
      color: 0x38bdf8,
      transparent: true,
      opacity: 0.95,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    const pulseCloud = new THREE.Points(pulseGeo, pulseMat);
    masterNeuronGroup.add(pulseCloud);

    // ── Mouse & Wheel Handlers ──
    let targetCamX = 0, targetCamY = 0;
    const mouseVec = new THREE.Vector2(0, 0);

    const onMouseMove = (e: MouseEvent) => {
      const rect = container.getBoundingClientRect();
      mouseVec.x = ((e.clientX - rect.left) / cW) * 2 - 1;
      mouseVec.y = -(((e.clientY - rect.top) / cH) * 2 - 1);
      targetCamX = mouseVec.x * 0.5;
      targetCamY = mouseVec.y * 0.4;
    };

    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      targetCamPosRef.current = [
        targetCamPosRef.current[0],
        targetCamPosRef.current[1],
        Math.max(6, Math.min(30, targetCamPosRef.current[2] + e.deltaY * 0.012)),
      ];
    };

    const onResize = () => {
      cW = container.clientWidth || window.innerWidth;
      cH = container.clientHeight || window.innerHeight;
      camera.aspect = cW / cH;
      camera.updateProjectionMatrix();
      renderer.setSize(cW, cH);
    };

    container.addEventListener('mousemove', onMouseMove);
    container.addEventListener('wheel', onWheel, { passive: false });
    window.addEventListener('resize', onResize);

    // ── Animation Loop ──
    let raf: number, frame = 0;
    const _tmpVec = new THREE.Vector3();

    const animate = () => {
      raf = requestAnimationFrame(animate);
      frame++;
      const t = Date.now() * 0.001;

      // Bioluminescent Starfield & Holographic Rings subtle drift
      starfield.rotation.z = t * 0.004;
      starfield.rotation.y = t * 0.002;
      largeBokehCloud.rotation.z = -t * 0.003;
      distantWebGroup.rotation.z = -t * 0.002;
      holoGridGroup.rotation.z = t * 0.001;

      // Organic soma breathing
      const sPulse = 1 + Math.sin(t * 2.2) * 0.03;
      masterNeuronGroup.scale.setScalar(sPulse);

      // Dynamic light intensity and color according to active biophysical mode
      const mode = SUBSYSTEM_MODES[activeSubsystemRef.current] || SUBSYSTEM_MODES.microcircuit;
      coreLight.intensity += (mode.glowIntensity - coreLight.intensity) * 0.08;
      if (mode.id === 'cortex') {
        coreLight.color.setHex(0x00f5d4);
      } else if (mode.id === 'channels') {
        coreLight.color.setHex(0x38bdf8);
      } else if (mode.id === 'receptors') {
        coreLight.color.setHex(0xd946ef);
      } else if (mode.id === 'ode') {
        coreLight.color.setHex(0xa855f7);
      } else {
        coreLight.color.setHex(0xd946ef);
      }

      // Stream Action Potential light pulses with mode speed multiplier
      for (let p = 0; p < PULSE_COUNT; p++) {
        pulseProgress[p] += pulseSpeed[p] * mode.pulseSpeedMultiplier;
        if (pulseProgress[p] > 1) {
          pulseProgress[p] = 0;
          pulseSegIdx[p] = Math.floor(Math.random() * allSegments.length);
        }
        const seg = allSegments[pulseSegIdx[p]];
        if (seg) {
          _tmpVec.lerpVectors(seg.start, seg.end, pulseProgress[p]);
          pulsePos[p * 3]     = _tmpVec.x;
          pulsePos[p * 3 + 1] = _tmpVec.y;
          pulsePos[p * 3 + 2] = _tmpVec.z;
        }
      }
      pulseGeo.attributes.position.needsUpdate = true;

      // Smooth Camera Lerp
      camera.position.x += (targetCamPosRef.current[0] + targetCamX - camera.position.x) * 0.06;
      camera.position.y += (targetCamPosRef.current[1] + targetCamY - camera.position.y) * 0.06;
      camera.position.z += (targetCamPosRef.current[2] - camera.position.z) * 0.06;
      camera.lookAt(0, 0, 0);

      renderer.render(scene, camera);

      if (frame % 2 === 0) {
        reprojectCallouts(camera, cW, cH);
      }
    };

    animate();

    return () => {
      cancelAnimationFrame(raf);
      container.removeEventListener('mousemove', onMouseMove);
      container.removeEventListener('wheel', onWheel);
      window.removeEventListener('resize', onResize);
      if (renderer.domElement && container.contains(renderer.domElement)) {
        container.removeChild(renderer.domElement);
      }
      renderer.dispose();
    };
  }, [reprojectCallouts]);

  return (
    <div className="relative w-full h-full overflow-hidden select-none" ref={containerRef}>
      {/* ── Radial Vignette ── */}
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          zIndex: 1,
          background: 'radial-gradient(ellipse 75% 70% at 50% 50%, transparent 0%, rgba(5,8,14,0.30) 60%, rgba(2,4,8,0.92) 100%)',
        }}
      />

      {/* ══════════════════════════════════════════════════════════
          1. TOP SLEEK GLASS HUD RIBBON (Zero overlap, full width)
         ══════════════════════════════════════════════════════════ */}
      <div
        className="absolute top-0 left-0 right-0 z-30 flex items-center justify-between px-3.5 pointer-events-none"
        style={{
          height: 32,
          background: 'rgba(8, 14, 24, 0.90)',
          backdropFilter: 'blur(16px)',
          borderBottom: '1px solid rgba(30, 41, 59, 0.7)',
          boxShadow: '0 4px 16px rgba(0, 0, 0, 0.5)',
        }}
      >
        {/* Left: Layer Title & Badge */}
        <div className="flex items-center gap-2 shrink-0">
          <span
            className="w-2 h-2 rounded-full animate-pulse shrink-0"
            style={{ background: currentModeInfo.colorTheme, boxShadow: `0 0 8px ${currentModeInfo.colorTheme}` }}
          />
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 800, letterSpacing: '0.06em', color: '#FFFFFF', textTransform: 'uppercase', whiteSpace: 'nowrap' }}>
            {currentModeInfo.name}
          </span>
          <span
            className="px-1.5 py-0.5 rounded text-[8px] font-mono font-bold uppercase tracking-wider shrink-0"
            style={{ background: `${currentModeInfo.colorTheme}22`, color: currentModeInfo.colorTheme, border: `1px solid ${currentModeInfo.colorTheme}55` }}
          >
            {currentModeInfo.badge}
          </span>
        </div>

        {/* Center: Mechanism Subtitle */}
        <span
          className="hidden lg:inline-block truncate px-3 text-[8.5px] font-mono text-slate-400 max-w-[380px]"
          style={{ fontFamily: 'Inter, sans-serif' }}
        >
          {currentModeInfo.description}
        </span>

        {/* Right: Scale Indicator */}
        <div className="flex items-center gap-1.5 shrink-0">
          <span className="text-[8px] font-mono text-slate-500 uppercase tracking-wider">SCALE:</span>
          <span className="text-[8.5px] font-mono font-bold" style={{ color: currentModeInfo.colorTheme }}>
            {currentModeInfo.scaleLabel}
          </span>
        </div>
      </div>

      {/* ══════════════════════════════════════════════════════════
          2. BOTTOM SLEEK TELEMETRY STRIP (Inline metrics, zero overlap)
         ══════════════════════════════════════════════════════════ */}
      <div
        className="absolute bottom-0 left-0 right-0 z-30 flex items-center justify-between px-3.5 pointer-events-none"
        style={{
          height: 28,
          background: 'rgba(6, 11, 20, 0.90)',
          backdropFilter: 'blur(16px)',
          borderTop: '1px solid rgba(30, 41, 59, 0.7)',
          boxShadow: '0 -4px 16px rgba(0, 0, 0, 0.5)',
        }}
      >
        <div className="flex items-center gap-1.5 shrink-0">
          <span className="text-[8px] font-mono font-bold text-slate-400 uppercase tracking-wider">
            LAYER TELEMETRY:
          </span>
        </div>

        {/* 4 Inline Metrics */}
        <div className="flex items-center gap-3 overflow-hidden">
          {currentModeInfo.metrics.map((m, idx) => (
            <div key={idx} className="flex items-center gap-1 shrink-0">
              <span className="text-[7.5px] font-mono text-slate-500 uppercase tracking-tight">
                {m.label}:
              </span>
              <span className="text-[9px] font-mono font-bold" style={{ color: hasActiveData ? m.color : '#6B7280' }}>
                {hasActiveData ? m.value : '—'}
              </span>
              {idx < currentModeInfo.metrics.length - 1 && (
                <span className="text-slate-700 text-xs ml-1">·</span>
              )}
            </div>
          ))}
        </div>

        <div className="flex items-center gap-1.5 shrink-0">
          <span className="text-[8px] font-mono text-slate-500">{hasActiveData ? '1,024 ODE UNITS' : 'STANDBY'}</span>
        </div>
      </div>

      {/* ══════════════════════════════════════════════════════════
          3. LEFT EDGE FLOATING TOOLBAR DOCK
         ══════════════════════════════════════════════════════════ */}
      <div
        className="absolute left-2.5 top-1/2 -translate-y-1/2 z-30 flex flex-col items-center gap-1.5 p-1 rounded-full"
        style={{
          background: 'rgba(8, 14, 26, 0.85)',
          backdropFilter: 'blur(12px)',
          border: '1px solid rgba(30, 41, 59, 0.7)',
          boxShadow: '0 4px 20px rgba(0, 0, 0, 0.5)',
        }}
      >
        {[
          { id: 'rotate', icon: RotateCcw, label: 'Reset View' },
          { id: 'refresh', icon: RefreshCw, label: 'Refresh Network' },
          { id: 'zoom_in', icon: ZoomIn, label: 'Zoom In' },
          { id: 'zoom_out', icon: ZoomOut, label: 'Zoom Out' },
          { id: 'target', icon: Crosshair, label: 'Focus Core' },
          { id: 'settings', icon: Settings, label: 'Settings' },
        ].map((tool) => {
          const Icon = tool.icon;
          const isActive = activeTool === tool.id;
          return (
            <button
              key={tool.id}
              onPointerDown={(e) => {
                e.stopPropagation();
                setActiveTool(tool.id);
                if (tool.id === 'rotate' || tool.id === 'target') {
                  const info = SUBSYSTEM_MODES[activeSubsystem] || SUBSYSTEM_MODES.microcircuit;
                  targetCamPosRef.current = info.camPos;
                } else if (tool.id === 'zoom_in') {
                  targetCamPosRef.current = [targetCamPosRef.current[0], targetCamPosRef.current[1], Math.max(6, targetCamPosRef.current[2] - 3)];
                } else if (tool.id === 'zoom_out') {
                  targetCamPosRef.current = [targetCamPosRef.current[0], targetCamPosRef.current[1], Math.min(32, targetCamPosRef.current[2] + 3)];
                }
              }}
              onClick={(e) => {
                e.stopPropagation();
                setActiveTool(tool.id);
              }}
              className={`p-1.5 rounded-full transition-all cursor-pointer ${
                isActive
                  ? 'text-cyan-300 bg-sky-500/20 shadow-[0_0_8px_rgba(56,189,248,0.4)]'
                  : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/50'
              }`}
              title={tool.label}
            >
              <Icon className="w-3.5 h-3.5" />
            </button>
          );
        })}
      </div>

      {/* ══════════════════════════════════════════════════════════
          4. SCI-FI HUD LEADER LINES & CODE TAGS (Image 2 Style)
         ══════════════════════════════════════════════════════════ */}
      <svg className="absolute inset-0 w-full h-full pointer-events-none z-10 overflow-visible">
        {HUD_NODES.map((node) => {
          const pos = calloutPositions[node.id];
          const isTargeted = currentModeInfo.targetNodeIds.includes(node.id);
          if (!pos || !isTargeted) return null;

          const isLeft = node.offset.dx < 0 || pos.cx < 300;
          const cardW = 180;
          const cardEdgeX = isLeft ? pos.cx + cardW : pos.cx;
          const cardEdgeY = pos.cy + 18;

          // Compute elbow vertex
          const elbowX = isLeft ? cardEdgeX + 22 : cardEdgeX - 22;
          const elbowY = cardEdgeY;

          const themeCol = currentModeInfo.colorTheme;

          return (
            <g key={node.id} style={{ opacity: 1.0, transition: 'opacity 0.3s ease' }}>
              {/* Target Node Concentric Rings (Pic 2 style) */}
              <circle
                cx={pos.ax}
                cy={pos.ay}
                r={9}
                fill="none"
                stroke={themeCol}
                strokeWidth={0.9}
                strokeDasharray="2,3"
                opacity={0.55}
              />
              <circle
                cx={pos.ax}
                cy={pos.ay}
                r={4.5}
                fill="none"
                stroke={themeCol}
                strokeWidth={1.3}
                opacity={0.85}
              />
              <circle
                cx={pos.ax}
                cy={pos.ay}
                r={2.0}
                fill="#FFFFFF"
                style={{ filter: `drop-shadow(0 0 5px ${themeCol})` }}
              />

              {/* Angled Leader Line (Target -> Elbow -> Card) */}
              <path
                d={`M ${pos.ax} ${pos.ay} L ${elbowX} ${elbowY} L ${cardEdgeX} ${cardEdgeY}`}
                fill="none"
                stroke={themeCol}
                strokeWidth={1.1}
                opacity={0.80}
              />

              {/* Sci-Fi Code Tag Badge on the Elbow (Pic 2 style: PRG-14, NEX-07, etc.) */}
              <g transform={`translate(${elbowX - 18}, ${elbowY - 18})`}>
                <rect
                  x={0}
                  y={0}
                  width={36}
                  height={13}
                  rx={2}
                  fill="rgba(5, 10, 18, 0.95)"
                  stroke={themeCol}
                  strokeWidth={1}
                />
                <text
                  x={18}
                  y={9.5}
                  textAnchor="middle"
                  fill="#E2E8F0"
                  fontSize="7.5"
                  fontFamily="monospace"
                  fontWeight="bold"
                  letterSpacing="0.05em"
                >
                  {node.codeTag}
                </text>
              </g>
            </g>
          );
        })}
      </svg>

      {/* ══════════════════════════════════════════════════════════
          5. SCI-FI HUD CALLOUT CARDS WITH CORNER BRACKETS (Image 2)
         ══════════════════════════════════════════════════════════ */}
      <div className="absolute inset-0 pointer-events-none z-20">
        {HUD_NODES.map((node) => {
          const pos = calloutPositions[node.id];
          const isTargeted = currentModeInfo.targetNodeIds.includes(node.id);
          if (!pos || !isTargeted) return null;

          const themeCol = currentModeInfo.colorTheme;

          return (
            <div
              key={node.id}
              className="absolute pointer-events-auto transition-all duration-300 relative group"
              style={{
                left: pos.cx,
                top: pos.cy,
                background: 'rgba(6, 12, 22, 0.82)',
                backdropFilter: 'blur(12px)',
                borderRadius: 3,
                padding: '6px 10px',
                minWidth: 155,
                maxWidth: 185,
                border: '1px solid rgba(255, 255, 255, 0.08)',
                boxShadow: '0 4px 20px rgba(0, 0, 0, 0.7)',
              }}
            >
              {/* Sci-Fi Reticle Corner Brackets (Pic 2 aesthetic) */}
              <span
                className="absolute -top-[1px] -left-[1px] w-2 h-2 border-t-[1.5px] border-l-[1.5px]"
                style={{ borderColor: themeCol }}
              />
              <span
                className="absolute -top-[1px] -right-[1px] w-2 h-2 border-t-[1.5px] border-r-[1.5px]"
                style={{ borderColor: themeCol }}
              />
              <span
                className="absolute -bottom-[1px] -left-[1px] w-2 h-2 border-b-[1.5px] border-l-[1.5px]"
                style={{ borderColor: themeCol }}
              />
              <span
                className="absolute -bottom-[1px] -right-[1px] w-2 h-2 border-b-[1.5px] border-r-[1.5px]"
                style={{ borderColor: themeCol }}
              />

              {/* Title (Bold uppercase, high tracking) */}
              <div
                className="font-sans text-[10px] font-extrabold tracking-wider text-slate-100 uppercase"
                style={{ letterSpacing: '0.06em' }}
              >
                {node.title}
              </div>

              {/* Subtitle Telemetry Line */}
              <div className="font-sans text-[8.5px] text-slate-400 leading-tight mt-0.5 mb-1.5">
                {hasActiveData ? node.subtitle : 'Offline · No telemetry stream'}
              </div>

              {/* Sci-Fi Status Chip */}
              {node.badge && (
                <div>
                  <span
                    className="inline-flex items-center gap-1 font-mono text-[8px] font-bold uppercase tracking-wider px-2 py-0.5 rounded"
                    style={{
                      background: !hasActiveData ? 'rgba(107, 114, 128, 0.15)' : (node.badge.tone === 'safe' ? 'rgba(16, 185, 129, 0.15)' : 'rgba(56, 189, 248, 0.15)'),
                      border: !hasActiveData ? '1px solid #374151' : (node.badge.tone === 'safe' ? '1px solid rgba(16, 185, 129, 0.5)' : `1px solid ${themeCol}88`),
                      color: !hasActiveData ? '#9CA3AF' : (node.badge.tone === 'safe' ? '#34d399' : themeCol),
                    }}
                  >
                    <span
                      className="w-1.5 h-1.5 rounded-full"
                      style={{
                        background: !hasActiveData ? '#6B7280' : (node.badge.tone === 'safe' ? '#10b981' : themeCol),
                        boxShadow: hasActiveData ? `0 0 4px ${node.badge.tone === 'safe' ? '#10b981' : themeCol}` : 'none',
                      }}
                    />
                    {hasActiveData ? node.badge.label : 'STANDBY'}
                  </span>
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}
