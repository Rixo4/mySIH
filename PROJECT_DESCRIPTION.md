# Silicon Patient Platform (`Neuro_drug_testing`)
## Comprehensive Project Description & Architectural Blueprint

> **A biologically credible in-silico computational neuropharmacology simulation engine and web console powered by C++20/CUDA, FastAPI, and React.**

---

## 📌 Executive Summary

The **Silicon Patient Platform** (`Neuro_drug_testing`) is a specialized computational platform designed for early-stage neuropharmacology research and drug evaluation. It simulates the biophysical impact of novel chemical compounds on central nervous system (CNS) microcircuits to assess **drug efficacy**, **safety margins**, and **mechanistic liability risks** (such as pro-convulsant/seizure activity, GABAergic sedation, or NMDA hypofunction disinhibition) prior to live animal or human clinical testing.

---

## 🎯 Problem Statement & Value Proposition

### The Challenge in Neuro-Pharmacology
1. **High Clinical Failure Rate**: Over 90% of neuro-therapeutic candidates fail during clinical trials due to unpredicted CNS toxicity or lack of efficacy.
2. **Black-Box Biology**: Animal models are expensive, time-consuming, and often fail to provide granular, step-by-step insight into the exact ion-channel and synaptic mechanisms causing adverse events.

### The Silicon Solution
- **Predictive In-Silico Modeling**: Translates molecular binding affinity ($\text{IC}_{50}$, Hill coefficients) directly into emergent cortical microcircuit behaviors.
- **Emergent Biology Approach**: Classifies drug profiles based purely on observable biophysical network emergence rather than arbitrary drug labels.
- **Reproducible Audit Ledger**: Stores complete execution telemetry, raw spike train outputs, parsed JSON metrics, and formal liability reports for every experiment run.

---

## 🏗️ System Architecture

The application adopts a decoupled, multi-tier architecture:

```text
+-----------------------------------------------------------------------+
|                           REACT FRONTEND                              |
|   (React 18 + TypeScript + Vite + Tailwind CSS + Recharts + Lucide)   |
|   - Drug Evaluation Console                                           |
|   - Real-time Interactive Visualization Dashboard                      |
|   - Run History & Audit Trail Management                              |
+-----------------------------------------------------------------------+
                                   |
                             HTTP / REST API
                                   v
+-----------------------------------------------------------------------+
|                            FASTAPI BACKEND                            |
|             (Python 3.11 + SQLAlchemy + SQLite + Pydantic)            |
|   - Engine Process Manager & Runner                                   |
|   - Report Parser & Signal Visualizer Data Pipeline                   |
|   - User Authentication (JWT) & Rate Limiting                         |
|   - Database Ledger & Run Artifact Filesystem Store                   |
+-----------------------------------------------------------------------+
                                   |
                          CLI Process Spawn / IPC
                                   v
+-----------------------------------------------------------------------+
|                        NATIVE SIMULATION ENGINE                       |
|                 (C++20 + CUDA 12 + OpenMP Multi-threading)            |
|   - Ion Channel Conductance & Membrane Voltage Solvers                |
|   - Vesicle Depletion, Neuromodulators & Reuptake Transporters         |
|   - Excitatory Pyramidal & Inhibitory Interneuron Microcircuits       |
|   - 12-Stage Biological Emergence Pipeline & Liability Assessor       |
+-----------------------------------------------------------------------+
```

---

## 🧬 The 12-Stage Biological Emergence Pipeline

The native C++ analyzer ([`PharmaDecisionEngine.cpp`](file:///c:/Users/rishi/OneDrive/Desktop/NeuroSIH/engine/analyzer/PharmaDecisionEngine.cpp)) processes drug evaluation candidates through a strict 12-stage emergence pipeline:

1. **Stage 1: Channel Blockade**: Applies the Hill equation ($1 / (1 + (\text{IC}_{50} / \text{Dose})^{\text{Hill}})$) for $\text{Na}^+$, $\text{K}^+$, $\text{Ca}^{2+}$, D1, NMDA, and $\text{GABA}_A$ channels.
2. **Stage 2: Single-Neuron Dynamics**: Modifies ionic conductance and membrane excitability in biophysical neuron models.
3. **Stage 3: Network Dynamics**: Simulates interactions between Excitatory Pyramidal cells and Fast-Spiking Inhibitory Interneurons.
4. **Stage 4: Per-Dose Metrics Collection**: Extracts firing frequencies (Hz), synchronization index (0–1), Neural Instability Index (NII), and burst patterns.
5. **Stage 5: Per-Dose Biological Interpretation**: Compares observed metrics against baseline control levels.
6. **Stage 6: Mechanistic Evidence Signatures**: Runs pattern matching algorithms to detect emergent biological signatures.
7. **Stage 7: Per-Dose Classification**: Categorizes microcircuit behavior for each evaluated dose level.
8. **Stage 8: Dose-Response Patterns**: Maps trends across the tested concentration spectrum.
9. **Stage 9: Mechanistic Dominance Analysis**: Identifies the primary driver responsible for observed network alterations.
10. **Stage 10: Safety & Risk Analysis**: Computes therapeutic index, pro-convulsant liability, and sedation risk.
11. **Stage 11: Confidence Analysis**: Evaluates statistical credibility and signal-to-noise ratio.
12. **Stage 12: Report Generation**: Compiles structured Markdown and JSON reports for storage and presentation.

---

## 📂 Repository Layout & Key File Map

```text
Neuro_drug_testing/
├── engine/                               # Native C++/CUDA Simulation Core
│   ├── main.cpp                          # Engine CLI entrypoint
│   ├── analyzer/                         # 12-Stage Decision Engine & Signal Processing
│   │   ├── PharmaDecisionEngine.cpp/.h   # Main pharmaceutical safety classifier
│   │   ├── NetworkAnalyzer.cpp/.h       # Microcircuit metric calculator
│   │   └── Metrics.cpp/.h                # Spike train & field potential analytics
│   ├── cuda/                             # GPU Parallel Acceleration
│   │   ├── CudaSimulator.cpp/.h          # Host-device memory manager
│   │   └── NeuronUpdate.cu               # CUDA Hodgkin-Huxley solver kernel
│   ├── drug/                             # Molecular Binding Models
│   │   └── DrugModel.cpp/.h              # Hill equation channel blockade models
│   ├── neuron/                           # Biophysical Cell Models
│   │   └── NeuronModel.cpp/.h            # Membrane potential solvers
│   ├── synapse/                          # Synapse & Transmitter Kinetics
│   │   ├── Synapse.cpp/.h                # AMPA/NMDA/GABA receptor gating
│   │   ├── NeuromodulatorSystem.cpp/.h   # Dopamine & neuromodulatory dynamics
│   │   ├── NeurotransmitterPool.cpp/.h   # Vesicle depletion & recycling
│   │   └── ReuptakeTransporter.cpp/.h    # DAT/GAT reuptake kinetics
│   ├── network/                          # Neural Microcircuits
│   │   └── Network.cpp/.h                # E/I population connectivity matrix
│   └── simulation/                       # Execution Loops
│       ├── SimulationEngine.cpp/.h       # Multi-threaded CPU engine
│       └── BatchedSimulationEngine.cpp/.h# Parallel dose-range engine
│
├── backend/                              # FastAPI Python Service
│   ├── app/
│   │   ├── main.py                       # REST API entrypoint & middleware
│   │   ├── orchestration.py              # Run lifecycle management
│   │   ├── engine_runner.py              # Native C++ binary process runner
│   │   ├── report_parser.py              # Output report parser
│   │   ├── visualization.py              # Recharts payload formatter
│   │   ├── auth.py                       # JWT authentication service
│   │   └── artifact_store.py             # Per-run artifact file storage
│   └── runs/                             # Persistent run outputs (<run_id>/)
│
├── frontend/                             # React + Vite Web Console
│   ├── src/
│   │   ├── pages/                        # Page Views
│   │   │   ├── DrugEvaluationPage.tsx    # Evaluation input console
│   │   │   ├── ReportDetailPage.tsx      # Comprehensive liability report view
│   │   │   ├── RunHistoryPage.tsx        # Audit trail & past runs table
│   │   │   └── DashboardPage.tsx         # Executive summary view
│   │   └── components/                   # UI Components
│   │       ├── DrugEvaluationVisualizationDashboard.tsx # Interactive charts
│   │       ├── DrugInputForm.tsx         # IC50 & Hill coefficient input form
│   │       └── RunHistoryTable.tsx       # Paginated run history table
│
├── ARCHITECTURE_12_STAGE_PIPELINE.md      # Detailed pipeline documentation
├── MECHANISTIC_EVIDENCE_SIGNATURES.md    # Biological signature definitions
├── CMakeLists.txt                        # Engine build system
├── docker-compose.yml                    # Docker container orchestration
└── test_*.json                           # Drug profile test datasets
```

---

## 🛠️ Tech Stack Matrix

| Area | Technology | Key Capabilities |
| :--- | :--- | :--- |
| **Engine** | C++20, CUDA 12, OpenMP, CMake | High-speed differential equation solvers, parallel GPU kernels |
| **Backend** | Python 3.11, FastAPI, SQLAlchemy, SQLite, Alembic | Async process execution, structured JSON parsing, persistent DB |
| **Frontend** | React 18, TypeScript, Vite, Tailwind CSS, Recharts | Interactive charts, responsive design system, real-time UI states |
| **DevOps** | Docker, Docker Compose, Terraform | Multi-container deployment, cloud infra readiness |

---

## 🚀 Quick Start Guide

### 1. Build Native Engine
```bash
cmake -S . -B build-validate
cmake --build build-validate --target silicon_patient -j4
```

### 2. Configure & Start Backend
```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

### 3. Start Frontend
```powershell
cd frontend
npm install
npm run dev
```

---

*Project created and maintained for biologically credible computational neuropharmacology research.*
