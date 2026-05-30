import { motion } from 'framer-motion';
import { Link } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';
import {
  ArrowRight,
  Beaker,
  BrainCircuit,
  CheckCircle2,
  CircleDot,
  Cpu,
  Database,
  FlaskConical,
  Globe,
  Mail,
  Mic,
  Network,
  Rocket,
  ShieldCheck,
  Sparkles,
  SquareActivity,
  Telescope
} from 'lucide-react';
import {
  Area,
  AreaChart,
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';

const responseSeries = [
  { label: 'Baseline', risk: 82, signal: 28 },
  { label: 'Low dose', risk: 67, signal: 42 },
  { label: 'Mid dose', risk: 44, signal: 63 },
  { label: 'High dose', risk: 31, signal: 79 },
  { label: 'Recovery', risk: 39, signal: 68 }
];

const workflowSteps = [
  {
    title: 'Model setup',
    description: 'Select a drug, patient profile, and protocol to generate a reproducible simulation run.'
  },
  {
    title: 'Engine execution',
    description: 'The C++ neuro engine evaluates dose response, network synchrony, and seizure-risk trajectories.'
  },
  {
    title: 'Scoring and reporting',
    description: 'The backend ranks the simulation output and stores text, charts, and reports for later use.'
  },
  {
    title: 'Share results',
    description: 'Users can inspect the run history, download artifacts, and share a scientific report.'
  }
];

const architecture = [
  { label: 'Frontend', detail: 'Vercel-hosted landing page and product entry point.' },
  { label: 'API', detail: 'FastAPI backend on EC2 handles auth, orchestration, and run metadata.' },
  { label: 'Database', detail: 'PostgreSQL on RDS stores users, jobs, and results.' },
  { label: 'Queue', detail: 'Redis buffers simulation tasks and worker coordination.' },
  { label: 'GPU worker', detail: 'g5.xlarge worker runs the heavy C++ neuro engine.' },
  { label: 'Storage', detail: 'S3 stores reports, graphs, and generated artifacts.' }
];

const scienceCards = [
  {
    title: 'Dose-response curves',
    icon: Beaker,
    text: 'We compare response curves across dosage bands to see where efficacy rises without pushing the system into a risky state.'
  },
  {
    title: 'Network synchrony',
    icon: Network,
    text: 'The engine tracks how neurons synchronize over time, which helps identify destabilizing patterns before they become dominant.'
  },
  {
    title: 'Seizure-risk scoring',
    icon: ShieldCheck,
    text: 'Each run is summarized into a risk score with trajectory charts so researchers can compare scenarios quickly.'
  }
];

const metrics = [
  { label: 'Simulation layers', value: '6' },
  { label: 'GPU path', value: 'g5.xlarge' },
  { label: 'Artifact store', value: 'S3' },
  { label: 'Deployment', value: 'Vercel + AWS' }
];

function SectionHeading({ eyebrow, title, blurb }: { eyebrow: string; title: string; blurb: string }) {
  return (
    <div className="max-w-3xl">
      <p className="text-xs font-semibold uppercase tracking-[0.35em] text-cyan-300/70">{eyebrow}</p>
      <h2 className="mt-3 text-3xl font-semibold text-white sm:text-4xl">{title}</h2>
      <p className="mt-4 text-base leading-7 text-slate-300">{blurb}</p>
    </div>
  );
}

function ScreenshotFrame() {
  return (
    <div className="rounded-[2rem] border border-white/10 bg-slate-950/80 p-4 shadow-panel">
      <div className="flex items-center justify-between border-b border-white/10 pb-3">
        <div>
          <p className="text-xs uppercase tracking-[0.28em] text-cyan-300/70">Simulation console</p>
          <p className="mt-1 text-sm text-slate-300">A snapshot of the workflow the public site explains</p>
        </div>
        <span className="rounded-full border border-emerald-400/20 bg-emerald-500/10 px-3 py-1 text-[11px] font-semibold uppercase tracking-[0.25em] text-emerald-200">
          Active build
        </span>
      </div>

      <div className="mt-4 grid gap-3 sm:grid-cols-2">
        <div className="rounded-3xl border border-white/10 bg-white/5 p-4">
          <p className="text-xs uppercase tracking-[0.3em] text-slate-400">Workflow</p>
          <div className="mt-4 space-y-3">
            {['Input prep', 'Engine run', 'Scoring', 'Report'].map((item, index) => (
              <div key={item} className="flex items-center gap-3">
                <div className="flex h-8 w-8 items-center justify-center rounded-xl bg-cyan-500/15 text-sm font-semibold text-cyan-100">0{index + 1}</div>
                <div className="flex-1">
                  <div className="h-2 rounded-full bg-white/10">
                    <div className="h-2 rounded-full bg-gradient-to-r from-cyan-400 to-emerald-400" style={{ width: `${35 + index * 18}%` }} />
                  </div>
                  <p className="mt-2 text-xs text-slate-400">{item}</p>
                </div>
              </div>
            ))}
          </div>
        </div>

        <div className="rounded-3xl border border-white/10 bg-white/5 p-4">
          <p className="text-xs uppercase tracking-[0.3em] text-slate-400">Outputs</p>
          <div className="mt-4 space-y-3 text-sm text-slate-300">
            <div className="rounded-2xl bg-slate-900/80 p-3 ring-1 ring-white/5">
              <div className="flex items-center justify-between">
                <span>Risk trend</span>
                <span className="text-emerald-300">Down 41%</span>
              </div>
              <div className="mt-3 h-20 rounded-2xl bg-gradient-to-br from-cyan-400/10 via-transparent to-emerald-400/10" />
            </div>
            <div className="rounded-2xl bg-slate-900/80 p-3 ring-1 ring-white/5">
              <div className="flex items-center justify-between">
                <span>Run status</span>
                <span className="text-cyan-300">Queued → complete</span>
              </div>
              <div className="mt-3 flex items-center gap-2 text-xs text-slate-400">
                <CheckCircle2 className="h-4 w-4 text-emerald-300" />
                Artifacts stored and ready to use
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export function LandingPage() {
  const { accessToken } = useAuth();
  return (
    <div className="relative min-h-screen overflow-hidden bg-midnight-950 text-slate-100">
      <div className="pointer-events-none absolute inset-0 bg-radial-shell" />
      <div className="pointer-events-none absolute left-[-10%] top-24 h-80 w-80 rounded-full bg-cyan-500/10 blur-3xl" />
      <div className="pointer-events-none absolute right-[-8%] top-40 h-80 w-80 rounded-full bg-emerald-500/10 blur-3xl" />

      <header className="relative z-10 border-b border-white/10 bg-midnight-950/70 backdrop-blur-xl">
        <div className="mx-auto flex max-w-7xl items-center justify-between px-4 py-4 sm:px-6 lg:px-8">
          <div className="flex items-center gap-3">
            <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-gradient-to-br from-cyan-400 to-emerald-400 text-midnight-950 shadow-glow">
              <FlaskConical className="h-6 w-6" />
            </div>
            <div>
              <p className="text-sm font-semibold text-white">Silicon Patient</p>
              <p className="text-xs uppercase tracking-[0.3em] text-slate-400">Neural drug testing</p>
            </div>
          </div>

          <nav className="hidden items-center gap-6 text-sm text-slate-300 md:flex">
            <a href="#workflow" className="transition hover:text-white">Workflow</a>
            <a href="#architecture" className="transition hover:text-white">Architecture</a>
            <a href="#science" className="transition hover:text-white">Science</a>
            <a href="#contact" className="transition hover:text-white">Contact</a>
          </nav>

          <div className="flex items-center gap-3">
            <Link
              to={accessToken ? '/app' : '/login'}
              className="hidden rounded-full border border-white/10 px-4 py-2 text-sm font-medium text-white transition hover:bg-white/5 sm:inline-flex"
            >
              Open app
            </Link>
            <a
              href="#contact"
              className="inline-flex items-center gap-2 rounded-full bg-cyan-500 px-4 py-2 text-sm font-semibold text-midnight-950 transition hover:bg-cyan-400"
            >
              Request demo <ArrowRight className="h-4 w-4" />
            </a>
          </div>
        </div>
      </header>

      <main className="relative z-10">
        <section className="mx-auto grid max-w-7xl gap-10 px-4 py-16 sm:px-6 lg:grid-cols-[1.05fr_0.95fr] lg:px-8 lg:py-20">
          <motion.div initial={{ opacity: 0, y: 18 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.5 }}>
            <div className="inline-flex items-center gap-2 rounded-full border border-cyan-400/20 bg-cyan-500/10 px-4 py-2 text-xs font-semibold uppercase tracking-[0.28em] text-cyan-200">
              <Sparkles className="h-3.5 w-3.5" /> Public product preview
            </div>

            <h1 className="mt-6 max-w-4xl text-5xl font-semibold tracking-tight text-white sm:text-6xl lg:text-7xl">
              A platform for{' '}
              <span className="bg-gradient-to-r from-cyan-300 via-cyan-200 to-emerald-300 bg-clip-text text-transparent">
                neural drug simulation and research insights
              </span>
            </h1>

            <p className="mt-6 max-w-2xl text-lg leading-8 text-slate-300">
              Silicon Patient evaluates dose response, seizure risk, and network synchrony through a public-facing product page that
              introduces the platform, explains the workflow, and shows the AWS-backed system architecture behind it.
            </p>

            <div className="mt-8 flex flex-wrap gap-3">
              <a
                href="#architecture"
                className="inline-flex items-center gap-2 rounded-full bg-white px-5 py-3 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01]"
              >
                Explore platform <ArrowRight className="h-4 w-4" />
              </a>
              <a
                href="#workflow"
                className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-5 py-3 text-sm font-semibold text-white transition hover:bg-white/10"
              >
                View workflow <SquareActivity className="h-4 w-4" />
              </a>
            </div>

            <div className="mt-10 grid gap-4 sm:grid-cols-2 xl:grid-cols-4">
              {metrics.map((metric) => (
                <div key={metric.label} className="flex min-h-[11rem] flex-col rounded-3xl border border-white/10 bg-white/5 p-4 shadow-panel">
                  <p className="text-[0.68rem] uppercase tracking-[0.32em] text-slate-400">{metric.label}</p>
                  <p className="mt-auto max-w-full break-words text-2xl font-semibold leading-tight tracking-tight text-white sm:text-[2rem]">
                    {metric.value}
                  </p>
                </div>
              ))}
            </div>
          </motion.div>

          <motion.div initial={{ opacity: 0, y: 18 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.55, delay: 0.05 }}>
            <div className="rounded-[2rem] border border-white/10 bg-[rgba(4,10,22,0.75)] p-4 shadow-panel backdrop-blur-xl">
              <div className="flex items-center justify-between border-b border-white/10 pb-3">
                <div>
                  <p className="text-xs uppercase tracking-[0.28em] text-cyan-300/70">Platform preview</p>
                  <p className="mt-1 text-sm text-slate-300">A concise view of the workflow, charts, and deployment story</p>
                </div>
                <span className="inline-flex items-center gap-2 rounded-full bg-emerald-500/10 px-3 py-1 text-xs font-semibold text-emerald-200 ring-1 ring-emerald-400/20">
                  <CircleDot className="h-3.5 w-3.5" /> Launch ready
                </span>
              </div>

              <div className="mt-4 grid gap-4">
                <ScreenshotFrame />

                <div className="grid gap-4 lg:grid-cols-2">
                  <div className="rounded-[1.75rem] border border-white/10 bg-slate-950/80 p-4">
                    <div className="flex items-center justify-between">
                      <p className="text-sm font-medium text-white">Risk vs signal</p>
                      <Globe className="h-4 w-4 text-cyan-300" />
                    </div>
                    <div className="mt-4 h-56">
                      <ResponsiveContainer width="100%" height="100%">
                        <AreaChart data={responseSeries}>
                          <defs>
                            <linearGradient id="riskFill" x1="0" y1="0" x2="0" y2="1">
                              <stop offset="5%" stopColor="#22d3ee" stopOpacity={0.35} />
                              <stop offset="95%" stopColor="#22d3ee" stopOpacity={0.02} />
                            </linearGradient>
                          </defs>
                          <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
                          <XAxis dataKey="label" stroke="#94a3b8" tick={{ fill: '#94a3b8', fontSize: 12 }} />
                          <YAxis stroke="#94a3b8" tick={{ fill: '#94a3b8', fontSize: 12 }} />
                          <Tooltip
                            contentStyle={{ background: '#020617', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }}
                          />
                          <Area type="monotone" dataKey="risk" stroke="#22d3ee" fill="url(#riskFill)" strokeWidth={3} />
                          <Line type="monotone" dataKey="signal" stroke="#34d399" strokeWidth={2} dot={false} />
                        </AreaChart>
                      </ResponsiveContainer>
                    </div>
                  </div>

                  <div className="rounded-[1.75rem] border border-white/10 bg-slate-950/80 p-4">
                    <p className="text-sm font-medium text-white">Output summary</p>
                    <div className="mt-4 space-y-3 text-sm text-slate-300">
                      <div className="flex items-center justify-between rounded-2xl bg-white/5 px-4 py-3">
                        <span>Simulation status</span>
                        <span className="font-semibold text-emerald-300">Complete</span>
                      </div>
                      <div className="flex items-center justify-between rounded-2xl bg-white/5 px-4 py-3">
                        <span>Artifact storage</span>
                        <span className="font-semibold text-cyan-300">S3</span>
                      </div>
                      <div className="flex items-center justify-between rounded-2xl bg-white/5 px-4 py-3">
                        <span>Compute lane</span>
                        <span className="font-semibold text-amber-200">g5.xlarge</span>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </motion.div>
        </section>

        <section id="workflow" className="mx-auto max-w-7xl px-4 py-12 sm:px-6 lg:px-8 lg:py-16">
          <SectionHeading
            eyebrow="Simulation workflow"
            title="From drug input to shareable result"
            blurb="The product story on Vercel should make the scientific process obvious in a few seconds. The sequence below mirrors the real workflow in the backend and worker stack."
          />

          <div className="mt-8 grid gap-4 lg:grid-cols-4">
            {workflowSteps.map((step, index) => (
              <motion.div
                key={step.title}
                initial={{ opacity: 0, y: 12 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true, margin: '-80px' }}
                transition={{ duration: 0.35, delay: index * 0.05 }}
                className="rounded-[1.75rem] border border-white/10 bg-white/5 p-5 shadow-panel"
              >
                <div className="flex items-center justify-between">
                  <span className="text-xs font-semibold uppercase tracking-[0.3em] text-cyan-300/70">0{index + 1}</span>
                  <Rocket className="h-4 w-4 text-cyan-300" />
                </div>
                <h3 className="mt-4 text-xl font-semibold text-white">{step.title}</h3>
                <p className="mt-3 text-sm leading-7 text-slate-300">{step.description}</p>
              </motion.div>
            ))}
          </div>
        </section>

        <section id="architecture" className="mx-auto max-w-7xl px-4 py-12 sm:px-6 lg:px-8 lg:py-16">
          <SectionHeading
            eyebrow="Architecture"
            title="Public site on Vercel, science stack on AWS"
            blurb="This is the deployment story investors and teams can understand at a glance. The landing page runs on Vercel while the simulation pipeline stays on AWS for CPU, GPU, and storage services."
          />

          <div className="mt-8 grid gap-4 xl:grid-cols-6">
            {architecture.map((item, index) => {
              const Icon = [Globe, Cpu, Database, Mic, BrainCircuit, Telescope][index];
              return (
                <div key={item.label} className="relative rounded-[1.75rem] border border-white/10 bg-white/5 p-5 shadow-panel">
                  <div className="flex items-center gap-3">
                    <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-cyan-500/15 text-cyan-200 ring-1 ring-cyan-400/20">
                      <Icon className="h-5 w-5" />
                    </div>
                    <div>
                      <p className="text-xs uppercase tracking-[0.28em] text-slate-400">{index + 1}</p>
                      <h3 className="text-lg font-semibold text-white">{item.label}</h3>
                    </div>
                  </div>
                  <p className="mt-4 text-sm leading-7 text-slate-300">{item.detail}</p>
                  {index < architecture.length - 1 ? (
                    <div className="mt-4 hidden h-px w-full bg-gradient-to-r from-cyan-400/40 to-transparent xl:block" />
                  ) : null}
                </div>
              );
            })}
          </div>
        </section>

        <section id="science" className="mx-auto max-w-7xl px-4 py-12 sm:px-6 lg:px-8 lg:py-16">
          <SectionHeading
            eyebrow="Scientific explanation"
            title="Built to explain the signal, not just show a score"
            blurb="The landing page should communicate what the platform is doing scientifically. Each run compares neural response, network stability, and safety envelopes so the output is legible to researchers." 
          />

          <div className="mt-8 grid gap-4 lg:grid-cols-3">
            {scienceCards.map((card) => {
              const Icon = card.icon;
              return (
                <motion.div
                  key={card.title}
                  initial={{ opacity: 0, y: 12 }}
                  whileInView={{ opacity: 1, y: 0 }}
                  viewport={{ once: true, margin: '-80px' }}
                  transition={{ duration: 0.35 }}
                  className="rounded-[1.75rem] border border-white/10 bg-white/5 p-6 shadow-panel"
                >
                  <div className="flex h-12 w-12 items-center justify-center rounded-2xl bg-cyan-500/15 text-cyan-200 ring-1 ring-cyan-400/20">
                    <Icon className="h-5 w-5" />
                  </div>
                  <h3 className="mt-4 text-xl font-semibold text-white">{card.title}</h3>
                  <p className="mt-3 text-sm leading-7 text-slate-300">{card.text}</p>
                </motion.div>
              );
            })}
          </div>
        </section>

        <section id="contact" className="mx-auto max-w-7xl px-4 py-12 pb-20 sm:px-6 lg:px-8 lg:py-16">
          <div className="grid gap-6 rounded-[2rem] border border-white/10 bg-gradient-to-br from-cyan-500/10 via-white/5 to-emerald-500/10 p-8 shadow-panel lg:grid-cols-[1.2fr_0.8fr] lg:p-10">
            <div>
              <p className="text-xs font-semibold uppercase tracking-[0.35em] text-cyan-300/70">Contact</p>
              <h2 className="mt-3 text-3xl font-semibold text-white">Ready for public deployment</h2>
              <p className="mt-4 max-w-2xl text-base leading-7 text-slate-300">
                This landing page is intentionally public, lightweight, and easy to deploy on Vercel. The simulation stack remains on AWS so the homepage can present the product clearly while the backend, database, cache, and worker evolve separately.
              </p>
              <div className="mt-6 flex flex-wrap gap-3">
                <a
                  href="mailto:siliconpatient@gmail.com"
                  className="inline-flex items-center gap-2 rounded-full bg-white px-5 py-3 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01]"
                >
                  <Mail className="h-4 w-4" /> siliconpatient@gmail.com
                </a>
                <Link
                  to={accessToken ? '/app' : '/login'}
                  className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-5 py-3 text-sm font-semibold text-white transition hover:bg-white/10"
                >
                  Open the app <ArrowRight className="h-4 w-4" />
                </Link>
              </div>
            </div>

            <div className="grid gap-4 rounded-[1.6rem] border border-white/10 bg-slate-950/70 p-5">
              <div className="flex items-center gap-3 rounded-2xl bg-white/5 px-4 py-3">
                <CheckCircle2 className="h-5 w-5 text-emerald-300" />
                <div>
                  <p className="text-sm font-medium text-white">Public marketing site</p>
                  <p className="text-xs text-slate-400">Vercel-friendly static deployment</p>
                </div>
              </div>
              <div className="flex items-center gap-3 rounded-2xl bg-white/5 px-4 py-3">
                <ShieldCheck className="h-5 w-5 text-cyan-300" />
                <div>
                  <p className="text-sm font-medium text-white">Scientific positioning</p>
                  <p className="text-xs text-slate-400">Clear explanation of the simulation engine</p>
                </div>
              </div>
              <div className="flex items-center gap-3 rounded-2xl bg-white/5 px-4 py-3">
                <Cpu className="h-5 w-5 text-amber-200" />
                <div>
                  <p className="text-sm font-medium text-white">AWS-ready architecture</p>
                  <p className="text-xs text-slate-400">Backend, RDS, Redis, GPU worker, and S3 are separated cleanly</p>
                </div>
              </div>
            </div>
          </div>
        </section>
      </main>
    </div>
  );
}