# Mechanistic Evidence Signatures: How to Read Emergent Biology

## Core Concept

The analyzer **detects emerged biology** by matching observable metric signatures against known patterns. It never predicts from channel identity—only measures what actually emerges from network simulation.

---

## The Five Observable Mechanisms

### 1. SUPPRESSION
Firing and propagation decreased

#### Observable Signature:
```
firing_rate           ↓ (>15% reduction from baseline)
  AND one of:
    synchronization   ↓ (>15% reduction)
    OR nii            ↓ (>15% reduction)  
    OR seizure_prob   ↓ (>15% reduction)
```

#### What It Means:
- Network is less active
- Fewer neurons firing
- Less propagation of activity
- Can be beneficial or harmful depending on context

#### Example Data:
```
Dose 10:
  baseline_rate = 20 Hz        current_rate = 15 Hz      (rate ↓25%)
  baseline_sync = 0.40         current_sync = 0.25       (sync ↓37%)
  baseline_nii = 0.20          current_nii = 0.18        (nii ↓10%)  [not significant]
  baseline_seizure = 5%        current_seizure = 2%      (seizure ↓60%)

Evidence: SUPPRESSION
Signature match: rate ↓25% + sync ↓37% + seizure ↓60%
Evidence strength: min(25%, 37%, 60%) / 100% = 0.25
```

#### When to Expect (But NEVER ASSUME):
- After sodium channel blockade (not guaranteed)
- After some calcium channel blockade (not guaranteed)
- NOT a direct prediction—only what actually emerges

---

### 2. EXCITATION
Firing and instability increased

#### Observable Signature:
```
firing_rate           ↑ (>20% increase)
  OR both:
    nii               ↑ (>15% increase)
    AND seizure_prob  ↑ (>15% increase)
```

#### What It Means:
- Network is more active
- More neurons firing
- Higher instability/synchronization
- Increased seizure susceptibility
- **Always concerning**

#### Example Data:
```
Dose 5:
  baseline_rate = 20 Hz        current_rate = 28 Hz      (rate ↑40%)
  baseline_nii = 0.20          current_nii = 0.38        (nii ↑90%)
  baseline_seizure = 5%        current_seizure = 18%     (seizure ↑260%)

Evidence: EXCITATION
Signature match: rate ↑40% + nii ↑90% + seizure ↑260%
Evidence strength: max(40%, 90%, 260%) / 100% = 2.6 → clamped to 1.0
```

#### When to Expect (But NEVER ASSUME):
- After potassium channel blockade (not guaranteed)
- After certain drug combinations (not guaranteed)
- NOT predictable from channel alone—only observed

---

### 3. STABILIZATION
Network synchronized less, instability decreased, rate maintained

#### Observable Signature:
```
calcium_blockade      ≥ 15%
  AND firing_rate     stable (|Δrate| < 10%)
  AND one of:
    synchronization   ↓ (>15% reduction)
    OR nii            ↓ (>15% reduction)
  AND seizure_prob    ↓ (>15% reduction)
```

#### What It Means:
- Network is synchronized **less** (not more)
- Network is **less unstable**
- But neurons are still **firing** (not silenced)
- Rate is maintained at functional levels
- **Most desirable mechanism**

#### Example Data:
```
Dose 15:
  Ca block = 35%                            [≥15% threshold: YES]
  baseline_rate = 20 Hz  current_rate = 21 Hz  (rate ±5%)     [stable]
  baseline_sync = 0.45   current_sync = 0.28   (sync ↓37%)    [decreased]
  baseline_nii = 0.25    current_nii = 0.10    (nii ↓60%)     [decreased]
  baseline_seizure = 8%  current_seizure = 1%  (seizure ↓87%) [decreased]

Evidence: STABILIZATION
Signature match: Ca≥15% + rate stable + sync↓37% + nii↓60% + seizure↓87%
Evidence strength: max(37%, 60%, 87%) / 100% = 0.87
```

#### Why It's NOT Suppression:
```
Suppression would be:  rate ↓30%, sync ↓37%, nii ↓60%
Stabilization is:      rate ±5%, sync ↓37%, nii ↓60%
                       KEY DIFFERENCE: ^^^^^^
                       Rate maintained despite reduced instability
```

#### When to Expect (But NEVER ASSUME):
- After calcium channel blockade (not guaranteed)
- After certain selective inhibitors (not guaranteed)
- Emerges only from simulation—cannot predict

---

### 4. SILENCING
Network activity near-total collapse

#### Observable Signature:
```
firing_rate < 10% of baseline
```

#### What It Means:
- Neurons have stopped firing
- Network is non-functional
- **Always toxic**

#### Example Data:
```
Dose 30:
  baseline_rate = 20 Hz  current_rate = 1.5 Hz  (rate to 7.5% of baseline)

Evidence: SILENCING
Signature match: rate < 10% baseline
Evidence strength: 1.0 - (1.5 / 20) = 0.925
```

#### Why It's Different from Suppression:
```
Suppression:  rate ↓50% → still functional (10 Hz)
Silencing:    rate ↓92% → non-functional (<2 Hz)
```

---

### 5. LIMITED EFFECT
No meaningful biological change

#### Observable Signature:
```
No patterns from 1-4 match
  AND
firing_rate stable (|Δrate| < 15%)
  AND
no toxic increases (nii ↑ < 40%, seizure ↑ < 40%)
```

#### What It Means:
- Drug did not significantly affect network
- Metrics are within noise/variability
- **Neutral outcome**

#### Example Data:
```
Dose 2:
  baseline_rate = 20 Hz  current_rate = 20.5 Hz  (rate ±2.5%)
  baseline_sync = 0.40   current_sync = 0.39     (sync ±2.5%)
  baseline_nii = 0.20    current_nii = 0.21      (nii ±5%)
  baseline_seizure = 5%  current_seizure = 5.2%  (seizure ±4%)

Evidence: LIMITED EFFECT
Signature match: None of the 1-4 patterns match
Evidence strength: 0.0
```

---

## How Blockade Causes Each Mechanism

### Sodium Blockade Path:
```
Na+ channels blocked
  ↓
Action potential initiation impaired
  ↓
Neurons fire less
  ↓
Network simulation:
  - Less input from each neuron
  - Propagation decreases
  
Emergent metrics:
  rate ↓, sync ↓, nii ↓, seizure ↓
  
Detected as: SUPPRESSION
```

### Potassium Blockade Path:
```
K+ channels blocked
  ↓
Repolarization impaired
  ↓
Neurons stay depolarized longer
  ↓
Higher excitability
  ↓
Network simulation:
  - More spontaneous firing
  - Increased synchronization
  - More instability
  
Emergent metrics:
  rate ↑, nii ↑, seizure ↑
  
Detected as: EXCITATION
```

### Calcium Blockade Path (Complex):
```
Ca2+ channels blocked
  ↓
Synaptic transmission reduced
  ↓
Ca2+ regulation altered
  ↓
Network simulation:
  - SAME firing rate (neurons still active)
  - LESS synchronization (reduced coupling)
  - LESS instability (reduced burst propagation)
  - LOWER seizure risk (reduced synchrony)
  
Emergent metrics:
  rate ≈ baseline, sync ↓, nii ↓, seizure ↓
  
Detected as: STABILIZATION (if Ca block ≥15%)
             OR SUPPRESSION (if rate also drops)
```

---

## Important: What's NOT Detected

### Drug Name Not Detected
```
❌ "Drug X" → Can't identify this
✓ "Stabilization mechanism" → Detected from metrics
```

### Channel Identity Not Detected  
```
❌ "K channel blocker" → Can't detect
✓ "Excitation pattern" → Detected if metrics show it
```

### Predicted Effects Not Detected
```
❌ "Na block should cause suppression" → Prediction
✓ "Suppression observed in metrics" → Observed
```

### Drug Class Not Used
```
❌ "Antiepileptic drug" → Classification abandoned
✓ "Seizure reduction observed" → Metric-based
```

---

## Real-World Example: Three Drugs, One Network

### Drug A (Actual Sodium Blocker):
```
Metrics observed: rate ↓40%, sync ↓30%, seizure ↓50%
Detected mechanism: SUPPRESSION
Report: "Network activity suppressed"
(Not: "sodium channel blocker detected")
```

### Drug B (Actual Potassium Blocker):
```
Metrics observed: rate ↑35%, nii ↑55%, seizure ↑45%
Detected mechanism: EXCITATION
Report: "Network excitability increased"
(Not: "potassium channel blocker detected")
```

### Drug C (Actual Calcium Blocker):
```
Metrics observed: rate ±5%, sync ↓40%, nii ↓50%, seizure ↓60%
Detected mechanism: STABILIZATION
Report: "Network synchronized less, became more stable"
(Not: "calcium channel blocker detected")
```

### Drug D (Unexpected: Sodium Blocker that Causes Excitation):
```
(Fictitious: maybe drug has metabolite that excites)
Metrics observed: rate ↑25%, nii ↑40%, seizure ↑35%
Detected mechanism: EXCITATION
Report: "Paradoxically, network excitability increased"
(The analyzer correctly detected emergence, despite channel identity)
```

---

## Detection Logic Flowchart

```
For each dose:
  ↓
1. Extract metrics (rate, sync, nii, seizure deltas)
  ↓
2. Check Silencing threshold
   if rate < 10% baseline → SILENCING
  ↓
3. Check Excitation signature
   if rate ↑>20% OR (nii ↑>15% AND seizure ↑>15%) → EXCITATION
  ↓
4. Check Stabilization signature  
   if Ca≥15% AND rate stable AND (sync↓ OR nii↓) AND seizure↓ → STABILIZATION
  ↓
5. Check Suppression signature
   if rate ↓>15% AND (sync↓ OR nii↓ OR seizure↓) → SUPPRESSION
  ↓
6. Default
   else → LIMITED EFFECT
  ↓
Mechanism detected + evidence strength calculated
```

---

## Confidence in Detection

Each mechanism has an **evidence strength** (0.0-1.0):

### High Confidence (0.8-1.0):
```
SUPPRESSION:    min(|rate_change%|, sync_reduction%, seizure_reduction%) > 80%
EXCITATION:     max(rate_increase%, nii_increase%, seizure_increase%) > 80%
STABILIZATION:  max(sync_reduction%, nii_reduction%, seizure_reduction%) > 80%
SILENCING:      rate < 5% baseline (strong collapse)
```

### Medium Confidence (0.5-0.8):
```
Clear signature but not overwhelming (50-80% magnitudes)
```

### Low Confidence (0.2-0.5):
```
Weak signal matching pattern (20-50% magnitudes)
```

### No Confidence (0.0-0.2):
```
LIMITED EFFECT
```

---

## Debugging: When Mechanisms Seem Wrong

If the detected mechanism doesn't match expectations:

1. **Check the metrics first** (not the channel):
   ```
   Expected: "Sodium blocker → suppression"
   Observed: "rate ↑40%, seizure ↑50%" 
   Detected: EXCITATION
   
   → This is correct! The metrics showed excitation.
   → The sodium channel assumption was wrong.
   ```

2. **Check mechanism consistency**:
   ```
   Dose 1:  SUPPRESSION (rate ↓)
   Dose 2:  EXCITATION (rate ↑) ← Fragmented? Check for:
   Dose 3:  SUPPRESSION (rate ↓)
   
   - Measurement noise?
   - Bimodal response?
   - Drug-metabolite different effect?
   ```

3. **Check that Stage 6 precedes Stage 12**:
   ```
   ✓ Stages 4-5 extract metrics
   ✓ Stage 6 detects mechanism from metrics
   ✓ Stage 7 classifies from mechanism + metrics
   ✓ Stage 12 reports from Stages 4-11
   
   ❌ Don't skip to Stage 12 before Stage 6
   ❌ Don't predict mechanism from channel (that's Stage 1-3 job)
   ```

---

## Summary Table

| Mechanism | Rate | Sync | NII | Seizure | Ca Block | Notes |
|-----------|------|------|-----|---------|----------|-------|
| SUPPRESSION | ↓>15% | ↓ or — | ↓ or — | ↓ or — | — | Network less active |
| EXCITATION | ↑>20% | — or ↑ | ↑>15% | ↑>15% | — | Network more excitable |
| STABILIZATION | ±<10% | ↓>15% | ↓>15% | ↓>15% | ≥15% | Rate maintained, sync ↓ |
| SILENCING | <10% base | — | — | — | — | Activity collapsed |
| LIMITED | ±<15% | ±<15% | ±<15% | ±<15% | — | No change |

---

## Key Takeaway

**The analyzer does not predict biology. It observes it.**

Every mechanism detected is grounded in observable network metrics, not drug identity or channel pharmacology. This makes it truly universal—applicable to any compound, combination, or pathway that produces observable changes in network dynamics.
