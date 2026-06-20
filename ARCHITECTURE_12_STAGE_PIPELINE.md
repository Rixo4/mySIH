# Universal Computational Neuropharmacology Engine

## ARCHITECTURE: 12-Stage Universal Pipeline

The analyzer implements a strict 12-stage pipeline based on **emergent biology**, not drug classification.

### Core Principle

The analyzer **never thinks in drug names or drug classes**. It thinks only in terms of observable biological emergence:

```
Drug Parameters 
  ↓ (Hill equation)
Channel Blockade (Na%, K%, Ca%)
  ↓ (Neuron model)
Neuron Dynamics
  ↓ (Network model)
Network Dynamics (emergence happens here)
  ↓ (collection)
Per-Dose Metrics
  ↓ (comparison to baseline)
Per-Dose Biological Interpretation
  ↓ (pattern matching)
Mechanistic Evidence (What emerged?)
  ↓ (classification)
Per-Dose Classification
  ↓ (aggregation)
Dose-Response Patterns
  ↓ (analysis)
Mechanistic Dominance
  ↓ (independent)
Safety Analysis
  ↓ (quality)
Confidence Analysis
  ↓ (description only)
Summary / Report
```

---

## The 12 Stages

### **Stage 1: Channel Blockade**
**Input**: Drug parameters (IC50 values, Hill coefficients, tested dose)

**Computation**: Hill equation for each channel
```
Blockade% = 1 / (1 + (IC50/Dose)^Hill)
```

**Output**: Na block %, K block %, Ca block %

**Status**: Molecular effects only. No conclusions about biology.

---

### **Stage 2: Neuron Dynamics**
**Input**: Channel blockade percentages

**Computation**: Modify neuron membrane properties
- **Sodium blockade** → Reduced action potential generation → Lower firing tendency
- **Potassium blockade** → Impaired repolarization → Higher excitability
- **Calcium blockade** → Reduced synaptic transmission → Lower synchronization

**Output**: Individual neuron behavior changes (not directly observed, internal to simulation)

**Status**: Still mechanistic, not yet biological.

---

### **Stage 3: Network Dynamics**
**Input**: Modified neurons with altered properties

**Computation**: Neurons interact in the network
- Firing patterns emerge
- Synchronization emerges
- Burst patterns emerge
- Seizure susceptibility emerges

**Output**: Network-level metrics (the first truly biological level)

**Status**: **Emergent biology begins here**

**Delivered as**: `DoseObservation` with per-dose metrics

---

### **Stage 4: Per-Dose Metrics Collection**
**Function**: `extractPerDoseMetrics()`

**Input**: `DoseObservation` (one for each tested dose)

**Process**: Extract all network metrics for this dose
- Mean firing rate (Hz)
- Synchronization index (0-1)
- Burst index
- Neural Instability Index (NII)
- Seizure probability (%)

**Output**: `PerDoseMetrics` with baseline and observed values

**Key Property**: Pure observation, no interpretation

---

### **Stage 5: Per-Dose Biological Interpretation**
**Function**: `interpretPerDoseBiology()`

**Input**: `PerDoseMetrics` (current dose) + baseline (dose 0)

**Process**: Calculate biological changes relative to baseline
```
rate_change_frac = (rate - baseline_rate) / baseline_rate
sync_delta = sync - baseline_sync
sync_reduction_pct = ((baseline_sync - sync) / baseline_sync) * 100
nii_delta = nii - baseline_nii
seizure_delta = seizure - baseline_seizure
[etc.]
```

**Output**: `PerDoseBiologicalInterpretation`
- Absolute changes in each metric
- Percent changes (reductions/increases)
- Normalized forms for comparison

**Key Property**: Still pure measurement. No classification yet.

---

### **Stage 6: Mechanistic Evidence Detection**
**Function**: `detectMechanisticEvidence()`

**Input**: `PerDoseBiologicalInterpretation` + `DoseObservation`

**Process**: Pattern match against known observable signatures
- **No reference to channel identities**
- **Only observable biology**

**Evidence Categories**:

#### **SUPPRESSION**
```
Signature:
  rate ↓ (>15% reduction)
  AND (sync ↓ OR nii ↓ OR seizure ↓)
  
Interpretation: Firing and propagation decreased
Observable mechanism: Network less active
```

#### **EXCITATION**
```
Signature:
  (rate ↑ >20%) OR (nii ↑ >15% AND seizure ↑ >15%)
  
Interpretation: Firing and instability increased
Observable mechanism: Network more unstable
```

#### **STABILIZATION**
```
Signature:
  Ca block ≥ 15%
  AND rate stable (|Δrate| < 10%)
  AND (sync ↓ >15% OR nii ↓ >15%)
  AND seizure ↓ >15%
  
Interpretation: Network synchronized less, less unstable, rate maintained
Observable mechanism: Network synchronized without silencing
```

#### **SILENCING**
```
Signature:
  rate < 10% of baseline
  
Interpretation: Network activity collapsed
Observable mechanism: Network shutdown
```

#### **LIMITED EFFECT**
```
Signature: No above patterns match

Interpretation: No meaningful biological change
Observable mechanism: None detected
```

**Output**: `MechanisticEvidence`
- `mechanism`: Enum value
- `description`: Text description
- `evidence_strength`: 0.0-1.0
- `is_consistent`: Boolean

**Key Property**: 
- **NEVER says** "Na block caused suppression"
- **ALWAYS says** "Suppression emerged from network dynamics"
- Mechanism is inferred from **observed metrics**, not from channel identities

---

### **Stage 7: Per-Dose Classification**
**Function**: `classifyPerDoseBiology()`

**Input**: `PerDoseBiologicalInterpretation` + `MechanisticEvidence`

**Process**: Classify this dose into a `BiologicalState`

**States**:
- `LimitedEffect` — No meaningful change
- `ControlledSuppression` — Rate ↓, stable, beneficial
- `NeuralSilencing` — Rate → ~0
- `Hyperexcitability` — Rate or instability ↑
- `NetworkStabilization` — Sync/NII/Seizure ↓ while rate stable
- `ToxicInstability` — NII or Seizure >> baseline

**Output**: `PerDoseClassification`
- `dose`: The dose
- `state`: Biological state
- `confidence`: Classification confidence (0-1)

**Key Property**: Each dose is classified independently based on **metrics alone**.

---

### **Stage 8: Dose-Response Analysis**
**Function**: `analyzeDoseResponse()`

**Input**: Array of `PerDoseClassification` (all doses)

**Process**: Look for patterns across the dose range
1. **When does response begin?** (onset_dose)
2. **Where is response strongest?** (peak_dose)
3. **Does response saturate?** (saturation_dose)
4. **Where does toxicity appear?** (toxicity_dose)
5. **Is the response continuous?** (is_continuous)
6. **Does response follow sigmoidal curve?** (is_sigmoidal, R² value)
7. **Is there a therapeutic window?** (window_min, window_max)

**Output**: `DoseResponseAnalysis`
- response_onset_dose
- response_peak_dose
- response_saturation_dose
- toxicity_threshold_dose
- is_continuous (boolean)
- is_sigmoidal (boolean)
- r2_value (sigmoid fit quality)
- has_therapeutic_window (boolean)
- window_min, window_max

**Key Property**: Describes patterns that **emerged from individual dose classifications**.

---

### **Stage 9: Mechanistic Dominance**
**Function**: `determineMechanisticDominance()`

**Input**: Array of `MechanisticEvidence` (all doses)

**Process**: Which mechanism dominates the entire curve?
```
Count occurrences of each mechanism across all doses
primary_mechanism = most common
prevalence_pct = (count / total_doses) * 100
is_consistent_across_curve = (prevalence_pct > 70%)
```

**Output**: `MechanisticDominance`
- primary_mechanism: Enum (Suppression, Excitation, Stabilization, Silencing, LimitedEffect)
- prevalence_pct: How many doses show this mechanism?
- is_consistent_across_curve: Is it the dominant mechanism across the dose range?

**Key Property**: The mechanism that dominates describes the drug's overall effect on **network biology**.

---

### **Stage 10: Safety Analysis (Independent)**
**Function**: `analyzeSafety()`

**Input**: Array of all data + mechanistic dominance

**Process**: Ask safety questions independently of efficacy
1. **Did seizure risk rise?** (max seizure % > baseline + 15%)
2. **Did network instability increase?** (max NII > baseline + 40%)
3. **Did network collapse occur?** (any dose with rate < 10% baseline)
4. **Does toxicity appear before efficacy?** (toxicity_dose < effect_onset_dose)
5. **Is there a safe margin?** (window_min > toxicity_dose or toxicity_dose > window_max)

**Output**: `SafetyAnalysis`
- seizure_risk_elevated (boolean)
- network_instability_observed (boolean)
- network_collapse_observed (boolean)
- toxicity_before_effect (boolean)
- safe_margin_exists (boolean)
- primary_safety_concern (text)
- overall_safety_score (0=safe, 1=toxic)

**Key Property**: 
- Safety is assessed **independently** of mechanism
- A suppressive drug can still be unsafe (if margin is narrow)
- A stabilizing drug can still be unsafe (if instability appears at some dose)
- **Safety doesn't care about the drug's primary mechanism**

---

### **Stage 11: Confidence Analysis**
**Function**: `analyzeConfidence()`

**Input**: All stages 4-10 data

**Process**: Measure trustworthiness of conclusions

**Inputs to confidence**:
- `curve_quality`: How well do the data fit? (R² value)
- `mechanism_consistency`: How consistent is the detected mechanism across doses?
- `dose_consistency`: How reproducible are the effects?
- `window_continuity`: If therapeutic window exists, how continuous is it?
- `data_variability`: How much noise is in the data?

**Computation**: Weighted scoring
```
confidence = (
  0.25 * curve_quality +
  0.25 * mechanism_consistency +
  0.25 * dose_consistency +
  0.15 * window_continuity +
  0.10 * (1.0 - data_variability)
)
```

**Output**: `ConfidenceAnalysis`
- curve_quality (0-1)
- mechanism_consistency (0-1)
- dose_consistency (0-1)
- window_continuity (0-1)
- data_variability (0-1)
- overall_confidence (0-1)
- confidence_level ("LOW", "MEDIUM", "HIGH")

**Key Property**: 
- **NOT based on run count alone**
- Based on **data quality and internal consistency**
- A high-quality experiment with 1 run can have HIGH confidence
- A low-quality experiment with 100 runs can have LOW confidence

---

### **Stage 12: Final Summary/Report**
**Function**: `generateSummary()`

**Input**: All stages 4-11 outputs

**Process**: Create report that **describes** what emerged
- Primary mechanism (from Stage 9)
- Safety profile (from Stage 10)
- Dose-response quality (from Stage 8)
- Confidence level (from Stage 11)

**Output**: Text summaries and recommendations

**Key Property**: 
- **The report DESCRIBES observations**
- **The report DOES NOT DECIDE biology**
- All statements must be traceable to Stages 4-11
- Never references drug identity or channel names
- Only references observed emergent biology

**Example Report**:
```
PRIMARY MECHANISM: Network Stabilization
- Dose range 10-20: Synchronization decreased 30-45%
- Dose range 10-20: Seizure probability decreased 25-35%
- Rate remained stable (105-110% of baseline)
- Evidence strength: 0.85

SAFETY PROFILE: Safe margin exists
- Peak seizure probability: 8% (baseline 5%)
- Minimal increase in instability
- Network did not collapse at any dose
- Safety score: 0.15 (0=safe, 1=toxic)

DOSE-RESPONSE: Sigmoidal suppressive effect
- Response onset: ~5 units
- Strong response plateau: 15-25 units
- Therapeutic window: 10-25 units
- Window width: 15 units
- Curve fit quality (R²): 0.92

CONFIDENCE: HIGH
- Mechanism consistency: 85% of doses show stabilization
- Dose consistency: Stable across replicates
- Curve quality: R² = 0.92 (excellent)
- Data variability: Low (<5%)
- Overall confidence: 0.88

RECOMMENDATION: PROMISING
Network stabilization without toxicity observed.
```

---

## Implementation Notes

### What Changed

1. **Header** (`PharmaDecisionEngine.h`):
   - Added `MechanisticMechanism` enum
   - Added `PerDoseMetrics` structure
   - Added `PerDoseBiologicalInterpretation` structure
   - Added `MechanisticEvidence` structure
   - Added `PerDoseClassification` structure
   - Added `DoseResponseAnalysis` structure
   - Added `MechanisticDominance` structure
   - Added `SafetyAnalysis` structure
   - Added `ConfidenceAnalysis` structure
   - Updated `PharmaDecisionReport` to include new stages

2. **Implementation** (`PharmaDecisionEngine.cpp`):
   - Added `extractPerDoseMetrics()` (Stage 4)
   - Added `interpretPerDoseBiology()` (Stage 5)
   - Added `detectMechanisticEvidence()` (Stage 6)
   - TODO: Add remaining stage functions
   - TODO: Refactor `evaluate()` to use new pipeline

### Backward Compatibility

- Old fields in `PharmaDecisionReport` are maintained
- `DoseFeatures` vector kept for compatibility
- Existing enums (`BiologicalState`, `DrugRiskTier`) maintained
- `PharmaDecisionEngine::evaluate()` still works the same way

### Next Steps

1. Implement Stage 7-9 functions (classification, dose-response, dominance)
2. Implement Stage 10-11 functions (safety, confidence)
3. Refactor `evaluate()` to use new pipeline
4. Update `generateSummary()` to be description-only
5. Add comprehensive testing for each stage

---

## Example: How Drug Categories Are Eliminated

### Old Thinking (WRONG):
```
Input: K blockade = 60%
Output: "This is excitatory because K blocks repolarization"
→ Conclusion made from channel identity, not observed biology
```

### New Thinking (CORRECT):
```
Input: K blockade = 60%
  ↓ (neural simulation with K blocked)
Neurons: Repolarization delayed, excitability increased
  ↓ (network simulation)
Network: Firing rate increased, NII increased, seizure probability increased
  ↓ (Stage 4: Extract metrics)
Metrics: rate = 150% baseline, nii = +0.45, seizure = +35%
  ↓ (Stage 5: Interpret)
Interpretation: rate ↑45%, NII ↑45%, seizure ↑35%
  ↓ (Stage 6: Detect mechanism)
Evidence: "Firing and instability increased" → EXCITATION mechanism detected
  ↓ (Stage 7: Classify)
Classification: Hyperexcitability state
  ↓ (Stage 12: Report)
Report: "Network showed hyperexcitable state"
→ Conclusion from observed biology, not channel identity
```

The key difference: We **measure** what happens, we don't **predict** it from channel identity.
