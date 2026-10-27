# H3D checks

## All checks through CTest

```bash
cmake -S src -B build -DCUGR_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## Independent die assignment with sanitizers

From the repository root, with the project's `build/` directory prepared,
compile the independent test executable:

```bash
c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Isrc -isystem rsyn/src tests/h3d/DieAssignmentTest.cpp \
    src/h3d/H3DTree.cpp src/h3d/DieAssignment.cpp -o build/h3d_die_assignment_test
./build/h3d_die_assignment_test
```

Rsyn headers are marked as external system headers so strict warning checks
apply to the new implementation without promoting legacy dependency warnings
to errors.

The oracle enumerates every edge-to-die assignment on small trees and scores
the connections independently of the DP. Checks cover all root choices,
mirrored die preferences and seeded equal-cost interval/child choices,
coincident pins on different dies, shared HBTs at branches, multiple HBTs on
a fixed topology, high-degree nodes, a 10,000-node chain, and malformed graphs.

`hbtCount` is the minimum number of **topological** HBTs on the supplied planar
tree. Physical HBT site assignment, site merging, and dual-DEF routing are later
stages and are not part of this executable.

## LEF/DEF smoke check

```bash
python3 tests/h3d/check_fixture.py
```

See [the synthetic dual-die fixture](../data/h3d/tiny/README.md) for its
coordinates, connectivity and HBT configuration.

## Independent HBT grid and location DP with sanitizers

```bash
c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Isrc -isystem rsyn/src tests/h3d/HbtOptimizationTest.cpp \
    src/h3d/HbtGrid.cpp src/h3d/HbtLocationOptimizer.cpp src/h3d/H3DTree.cpp \
    src/h3d/DieAssignment.cpp -o build/h3d_hbt_optimization_test
./build/h3d_hbt_optimization_test
```

Use a distinct output filename if the normal CTest executables should be kept.

## Complete dual-die flow

```bash
python3 tests/h3d/check_end_to_end.py
```

This checks HBT ownership/geometry, joint connectivity reconstructed from the
output DEFs, no lost terminals or shorts, guide names, DEF re-import,
determinism, same-net HBT merging, local-only nets, congested rerouting and
resource exhaustion. See [the implementation guide](../../H3D_IMPLEMENTATION.md).

The optimization test also enumerates 80 multi-variable cases (nonuniform
coordinates and holes), checks exact single-site coalescing, and verifies that
the two-net/two-site exhaustion example is repaired by the site budget.
End-to-end checks compare synchronized pad capacities with a fresh final-geometry
map and verify that committed demand is preserved.

`check_end_to_end.py` explicitly selects `-h3d-method enhanced` for its budget
and coalescing scenarios. The router's default is `paper`, seed 0.

## Paper-method checks

`h3d_paper_method_test` checks seeded leaf roots, directional nearest ties,
Equation-6 regions, Algorithm-1 candidates/Equation-7 costs, fixed-pin BFS
forests, and reverse-DFS splitting. A pruning counterexample must cost 60 in
paper mode and 54 in enhanced full-domain mode; it detects accidental use of
the enhancement through the paper entry point.

The paper tests also check zero-wirelength two-to-one and three-to-two site
coalescing, rejection of any positive-wirelength forced merge, unchanged
candidate/cost tables, and reproducible equivalent-quantity-assignment search.
`-h3d-paper-ties coalesce` is the default; use `first` for the original traceback.
Quantity assignment search is opt-in through `-h3d-assignment-trials N` (default 1).
Its minimum HBT count stays unchanged, while selection among different minimum
assignments can trade location wirelength for fewer physical sites.

The scarcity regression obeys HPWL ordering and exhaustively scores all four
two-HBT/two-site assignments. Both pruned and full-domain Equation-7 solvers
choose the same unique local optimum, exhaust both sites, and fail the next net;
a feasible joint allocation requires a higher first-net local cost.
`runPaperFullDomainAudit` preserves the paper roots and BFS forest for controlled
candidate-domain comparisons; its diagnostic mode is never labeled as paper output.

`h3d_input_provenance` additionally verifies that the converter cannot silently
invent a 3 um pitch, checks placer TerminalSize/TerminalSpacing and DBU/bounds,
labels explicit overrides, and excludes upper-boundary grid centers.

Root-cause evidence and source provenance are in [run_006](../../eval/h3d_aes/run_006/REPORT.md).
The subsequent [run_007](../../eval/h3d_aes/run_007/REPORT.md) authenticates the real
placer/DEF/LEF files and fully routes the source-model input with a 1 um pad and
2 um grid using the single-pass paper algorithm. The input provenance test also
checks a row-based placement core contained in the larger DEF outline.

For ASan/UBSan (with `build/` prepared):

```bash
c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Isrc -isystem rsyn/src tests/h3d/PaperMethodTest.cpp \
    src/h3d/H3DTree.cpp src/h3d/DieAssignment.cpp src/h3d/HbtGrid.cpp \
    src/h3d/HbtLocationOptimizer.cpp src/h3d/SubnetPartition.cpp -o build/h3d_paper_method_asan
./build/h3d_paper_method_asan
```

Independent real-AES trace checks:

```bash
python3 scripts/validate_h3d_paper.py --experiment eval/h3d_aes/run_003 --stage paper_full_verified
python3 scripts/validate_h3d_paper.py --experiment eval/h3d_aes/run_003 \
    --stage completed_prefix_refinement --compare-stage paper_full_verified
python3 scripts/validate_h3d_aes.py --experiment eval/h3d_aes/run_003 \
    --phase routing --stage completed_prefix_refinement
```

The first two independently scan legal points, enumerate Equation-7 transitions,
rebuild fixed-pin BFS forests and die-copy connectivity, verify fixed pins and
sequential site ownership, and compare initial algorithm traces. A subset's
future-net counter is checked against its exact selection-size offset.
The last check reconstructs DEF connectivity and verifies placement, original
I/O geometry, endpoint preservation, pads and guides. See
[run_003/REPORT.md](../../eval/h3d_aes/run_003/REPORT.md) for the failed full-AES
scope and the successfully refined prefix. Neither proves identity with the
unavailable author implementation or full-domain location optimality.

Latest audited runs are under [run_005](../../eval/h3d_aes/run_005/REPORT.md):

```bash
python3 scripts/validate_h3d_paper.py --experiment eval/h3d_aes/run_005 --stage full
python3 scripts/validate_h3d_paper.py --experiment eval/h3d_aes/run_005 \
    --stage completed_prefix_refinement --compare-stage full
```

The checker accepts any genuinely minimizing Equation-7 predecessor, reconstructs
the canonical baseline, checks every accepted merge step, verifies minimum-count
edge assignments for all recorded quantity trials, and verifies the declared
physical-site/wirelength selection order. Full Equation-7 table checks apply to
the selected assignment; unselected trials have quantity/selection metadata checks.

Historical AES counterexamples can be replayed without altering the saved data:

```bash
python3 scripts/validate_h3d_aes.py --experiment eval/h3d_aes/run_002 --phase replay-old-locations
```

The command creates a new replay directory and refuses to overwrite one that
already exists; use a fresh experiment for another recorded replay.
