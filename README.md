# project-implementation-RD

Repository for the research and development work on the 247-519-VA (Project I) capstone project at Vanier College: an ESP32-based smartwatch and custom waist-worn PCB system for fall detection and caregiver alerting.

## Current contents

### `documents/proposal/`
The written Project Proposal, built in LaTeX.

- `proposal.tex` — main document; pulls in the table files below via `\input`.
- `table1_stakeholder_needs.tex` through `table5_uncertainties.tex` — the proposal's tables (stakeholder needs, scope, platform limits, provisional requirements, ranked uncertainty register), kept as separate `\input` files for easier editing.
- `Proposal.pdf` — compiled output of the above.

### `figures/generated/`
- `context_diagram.pdf` — the system context diagram (single system-boundary block, wearer/caregiver/care-facility/MQTT broker shown around it), generated from the script below rather than drawn by hand.

### `src/`
- `context_diagram.py` — Python/matplotlib script that generates `figures/generated/context_diagram.pdf`. Run this script to regenerate the diagram after any changes to the system boundary or data flow.

## Status

This repository currently holds the proposal stage deliverable (Project Idea Form submission support). No firmware, PCB design files, or PoC testing code exist yet — those begin after proposal approval.
