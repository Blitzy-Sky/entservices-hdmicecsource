#!/usr/bin/env bash
###
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2025 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###
#
# =====================================================================================
# run_coverage.sh -- gcov/lcov coverage runner and line-coverage gate for the HDMI-CEC
#                    SOURCE plugin's L1 and L2 test suites.
#
# PURPOSE
#   Run a suite, capture coverage from the instrumented build tree, write the HTML report,
#   print a per-file table of line, function and branch figures derived from the trace
#   records, and fail when line coverage is below the bar.
#
# WHAT IT REPRODUCES, AND WHAT IT ADDS
#   The capture directory, the exclusion globs and the genhtml title below are reproduced
#   VERBATIM from this repository's own recipe -- ../.github/workflows/L1-tests.yml
#   (capture at :687-:689, globs at :693-:699, genhtml at :703-:706) and
#   ../.github/workflows/L2-tests.yml (capture at :766-:768, globs at :772-:781, genhtml at
#   :784-:787).  Nothing about the denominator is this script's invention.
#
#   Two things are ADDED, because the workflows lack both:
#     1. BRANCH DATA.  Each workflow copies the *test framework's* lcov configuration over
#        ~/.lcovrc before capturing (L1-tests.yml:685 -> entservices-testframework/Tests/
#        L1Tests/.lcovrc_l1; L2-tests.yml:764 -> that submodule's Tests/L2Tests/.lcovrc_l2)
#        and both of those files set `lcov_branch_coverage = 0`, so branch data is silently
#        discarded in CI.  Note they are NOT this repository's own Tests/L1Tests/.lcovrc_l1,
#        which CI never reads -- so enabling branch collection there is complementary but
#        NOT sufficient.  This script therefore takes ~/.lcovrc out of the picture for the
#        duration of the run and passes `--rc branch_coverage=1` to every lcov and genhtml
#        invocation.  The run-time override is the AUTHORITATIVE mechanism: the legacy
#        `lcov_branch_coverage` key is deprecated in lcov 2.x and defaults to zero, so no
#        configuration file can be relied upon to switch branch collection on.
#     2. A NUMERIC THRESHOLD.  The workflows apply none, and no coverage gate of any kind
#        existed anywhere in this workspace before this script.  Line coverage is gated at
#        >= ${COVERAGE_MIN:-80}% both in aggregate and per target; branch coverage is
#        reported as evidence only (see BRANCH COVERAGE below).
#
# RUN IT PER PLUGIN, SEQUENTIALLY -- THIS IS NOT ADVICE, IT IS A CORRECTNESS REQUIREMENT
#   Both HDMI-CEC plugins compile their tests into an identically named shared library:
#   Tests/L1Tests/CMakeLists.txt:19 here and in entservices-hdmicecsink both read
#   `set(PLUGIN_NAME L1TestsIO)`, and both Tests/L2Tests/CMakeLists.txt:19 read
#   `set(PLUGIN_NAME L2TestsIO)`.  Building one plugin therefore OVERWRITES the other's test
#   library, and RdkServicesL1Test itself compiles only test_JSON.cpp -- this plugin's own
#   cases live in that shared library and are linked in.  A stale library means the run
#   silently measures the other plugin.  The order per plugin is:
#       build the plugin -> rebuild AND reinstall entservices-testframework against it
#                        -> run -> capture coverage -> only then move to the other plugin
#   Skipping the test-framework rebuild is exactly where the collision bites.  preflight()
#   inspects the installed test library's symbols and refuses to run when they belong to the
#   other plugin, so the failure mode is a hard stop rather than a plausible wrong number.
#
# INPUTS (environment, all optional, all with documented defaults -- see --help)
#   WS, BUILD_DIR, INSTALL_DIR, L1_BUILD_DIR, L1_INSTALL_DIR, L2_BUILD_DIR,
#   L2_INSTALL_DIR, LEVEL_REBUILD_CMD, ARTIFACT_ROOT, COVERAGE_MIN, RUN_VALGRIND.
#
# GOVERNING CONTRACT
#   This project has NO user-specified rules: `review_rules` returns exactly
#   "No user rules provided."  That absence is not licence to lower the bar, so the
#   substituting binding contract is the enterprise-standard bar (specification section
#   0.12.1), honoured here as follows:
#     * Repository convention is authoritative.  The workflows win over instinct, and where
#       they disagree with expectation the tension is documented rather than silently
#       resolved -- see the doubled glob token, the gate spelling and the function-model
#       notes below.  The sibling runners in entservices-hdmicecsink/Tests and
#       hdmicec/tests/L1Tests share this script's structure, option names and artifact
#       layout so that one traceability report can consume all three.
#     * No new framework, tool or dependency.  The complete set this script executes is:
#       bash, lcov, genhtml, gcov and cmake (the last two only to echo their versions), plus
#       awk, sed, grep, sort, wc, tr, head, printf, cp, rm, rmdir, mkdir, chmod, find, mktemp,
#       basename, dirname and command -- and, optionally, nm for the library-provenance check
#       (with a grep fallback) and valgrind when explicitly asked for.  Nothing else.
#       No gcovr, no jq, no python, no pip, no apt, and no reporting layer of any kind: the
#       per-file table is awk over the trace records.
#     * Measured claims only.  Every figure this script prints is read out of a trace it
#       has just produced.  No coverage number is defaulted, inferred or hard-coded, and
#       there is no fallback path that prints a plausible figure when a step fails: a
#       failed step is a failed run.  The one place historical numbers appear is the
#       must-not-regress floor table, which is labelled as the recorded baseline it is.
#     * Honest reporting over convenient numbers.  No exclusion glob is added to flatter a
#       percentage, uncoverable content stays in the denominator and is enumerated with its
#       reason, and a gate failure is reported rather than filtered away.
#     * Additive-by-default.  This script modifies no production, build or configuration
#       file.  It does not touch Tests/gcc-with-coverage.cmake, Tests/clang.cmake, either
#       CMakeLists.txt, the workflows, /etc/lcovrc, entservices-testframework, or anything
#       under plugin/.  It is not side-effect free, and its three side effects are stated
#       here rather than buried:
#         (a) $HOME/.lcovrc -- CI plants a branch-disabled copy there, so the file is
#             stashed into a private mktemp directory, removed for the run, and restored by
#             an EXIT/INT/TERM/HUP trap however the run ends.  Nothing is destroyed and the
#             stash path is logged.  For a run that does not touch $HOME at all:
#                 HOME="$(mktemp -d)" ./run_coverage.sh l1
#         (b) the level's *.gcda counters are zeroed before the suite runs, so the figures
#             describe THIS run and cannot silently accumulate an earlier one.
#         (c) artifacts are written under $ARTIFACT_ROOT (default $WS/coverage-artifacts).
#             That tree is disposable build output: it is NOT part of the repository and
#             must never be committed.  The exact removal command is printed at the end of
#             every run, and ARTIFACT_ROOT can be pointed anywhere -- set it outside the
#             checkout if you want the run to leave nothing untracked in the tree at all.
#
# BRANCH COVERAGE IS REPORTED, NOT GATED
#   The gate is line coverage only, deliberately.  gcov counts branches as control-flow-
#   graph arcs, and those arcs include compiler-generated exception and static-destruction
#   edges that no test can reach, so 100% branch coverage is unattainable for most C++
#   translation units and a branch threshold would be a threshold on the compiler rather
#   than on the tests.  Branch figures are collected and printed because they are the
#   movement evidence for closing if/else paths with negative and corner-case tests.
#
# TOOLCHAIN THIS SCRIPT WAS EXERCISED WITH
#   lcov 2.0-1, gcov/gcc 13.4.0, GoogleTest 1.15.0, CMake 3.16.9 from /opt/cmake316.
#   CMake 3.16.x is a HARD constraint, not a preference: 3.20 and newer fail the plugin
#   test-library configuration step.  The local host is newer than CI's image, so the build
#   recipe below needs GCC-13 `-Wno-error=` relaxations; those are supplied at INVOCATION
#   time only and are never written into a committed build file.  CI pins GoogleTest v1.15.0
#   and clones entservices-testframework 1.0.14 while this workspace vendors b8eee47
#   (1.0.17), so mock behaviour can differ between a local run and CI.  Re-pinning is
#   outside the test-only change boundary: it is recorded here, not fixed.  Coverage figures
#   are compiler-sensitive at the margin, so a comparison across toolchains is not exact.
#
# EXIT STATUS IS THE VERDICT
#   0  suite green and every gated target at or above the bar.
#   1  a suite failed, a gate failed, or a precondition was not met.  Nothing is swallowed:
#      lcov's own non-zero exit is what fails the run.
# =====================================================================================

set -euo pipefail

# Resolved from this script's own location so that the working directory of the caller is
# irrelevant: Tests/ -> <repository> -> <workspace>.  No path is hard-coded.
SCRIPT_PATH="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/$(basename -- "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(dirname -- "$SCRIPT_PATH")"          # <repository>/Tests
REPO_ROOT="$(dirname -- "$SCRIPT_DIR")"            # <repository>  (entservices-hdmicecsource)
REPO_NAME="$(basename -- "$REPO_ROOT")"
readonly SCRIPT_PATH SCRIPT_DIR REPO_ROOT REPO_NAME

# ------------------------------------------------------------------------------------
# Measurement tooling resolved to absolute paths HERE, from the environment as inherited,
# BEFORE this script goes anywhere near INSTALL_DIR.  The test binaries genuinely have to be
# reached through INSTALL_DIR, which is caller-supplied; nothing else does.  Resolving the
# measurement tools up front means none of them can be picked up out of that tree once the
# runtime search paths point into it.
# ------------------------------------------------------------------------------------
resolve_tool() { # $1=tool name -> absolute path on stdout, empty when absent
    command -v -- "$1" 2>/dev/null || true
}
LCOV_BIN="$(resolve_tool lcov)"
GENHTML_BIN="$(resolve_tool genhtml)"
GCOV_BIN="$(resolve_tool gcov)"
FIND_BIN="$(resolve_tool find)"
MKTEMP_BIN="$(resolve_tool mktemp)"
NM_BIN="$(resolve_tool nm)"
CMAKE_BIN="$(resolve_tool cmake)"
VALGRIND_BIN="$(resolve_tool valgrind)"
readonly LCOV_BIN GENHTML_BIN GCOV_BIN FIND_BIN MKTEMP_BIN NM_BIN CMAKE_BIN VALGRIND_BIN

# ------------------------------------------------------------------------------------
# Environment inputs -- every one overridable, with the documented defaults.
# ------------------------------------------------------------------------------------
WS="${WS:-$(dirname -- "$REPO_ROOT")}"                        # workspace root, CI's $GITHUB_WORKSPACE
BUILD_DIR="${BUILD_DIR:-$WS/build/$REPO_NAME}"                # `lcov -c -d` target, exactly as in CI
INSTALL_DIR="${INSTALL_DIR:-$WS/install}"                     # provides the test binaries + plugins
COVERAGE_MIN="${COVERAGE_MIN:-80}"                            # the line-coverage bar
RUN_VALGRIND="${RUN_VALGRIND:-0}"                             # opt-in memcheck; never a gate

# Per-level overrides.  L1 and L2 need differently configured trees (different -I/-include/-D
# blocks, a level-specific mocks library, and a level-specific test library), so each level
# resolves its own build and install directory.  Both default to the single-tree values above,
# which is exactly right for `l1` or `l2` on its own; `all` additionally requires that the two
# levels not resolve to the same tree unless LEVEL_REBUILD_CMD switches it between them.
L1_BUILD_DIR="${L1_BUILD_DIR:-$BUILD_DIR}"
L1_INSTALL_DIR="${L1_INSTALL_DIR:-$INSTALL_DIR}"
L2_BUILD_DIR="${L2_BUILD_DIR:-$BUILD_DIR}"
L2_INSTALL_DIR="${L2_INSTALL_DIR:-$INSTALL_DIR}"

# Optional hook that switches a shared tree to a level.  Invoked as `$LEVEL_REBUILD_CMD <level>`
# immediately before each level runs under `all`; empty means "no hook", in which case `all`
# demands separate per-level trees rather than measuring one tree twice and calling the second
# figure L2.  Never invoked for a single-level run: there the caller has already built the tree
# for the level being measured.
LEVEL_REBUILD_CMD="${LEVEL_REBUILD_CMD:-}"

# Artifact root.  Three runners -- this one, entservices-hdmicecsink/Tests/run_coverage.sh and
# hdmicec/tests/L1Tests/run_coverage.sh -- share one long-lived $WS, whereas each CI job owns a
# throwaway $GITHUB_WORKSPACE and can afford flat file names.  Here flat names would mean the
# second run overwrites the first run's evidence and the traceability report can no longer
# attribute a trace to a target, so every artifact goes to
# $ARTIFACT_ROOT/<repository>/<level>/ while keeping CI's file names recognisable.
ARTIFACT_ROOT="${ARTIFACT_ROOT:-$WS/coverage-artifacts}"

# Resolved per level by run_level() before anything else happens.
LEVEL_BUILD_DIR=''
LEVEL_INSTALL_DIR=''
LEVEL_ARTIFACT_DIR=''

# Pristine search paths, captured once so that each level's runtime environment is computed
# from the same base and a second level cannot inherit the first level's install tree.
readonly BASE_PATH="${PATH:-}"
readonly BASE_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"

# genhtml title, spelled exactly as both workflows spell it (L1-tests.yml:704,
# L2-tests.yml:786).  Both levels share it, so the level distinction lives entirely in the
# exclusion globs and the artifact names.
readonly GENHTML_TITLE="$REPO_NAME coverage"

# `--ignore-errors` for the capture step: exactly the nine values the recipe specifies, and
# `category` is deliberately ABSENT from every list in this script.
#
# The reason is a verified mechanism, not a style preference: lcov 2.x REJECTS an unrecognised
# --ignore-errors class outright -- `lcov: ERROR: unknown argument for --ignore-errors: '...'`,
# exit 2 -- so a list is not a place to guess.  `category` is not part of the nine-value recipe
# and its availability differs between lcov 2.x builds (it is tolerated by the 2.0-1 build this
# script was exercised with, and documented elsewhere in this workspace as hard-failing), so
# the lists below stay on classes that are known-good here.  Add nothing speculatively: a
# rejected class turns a coverage run into a usage error.
readonly LCOV_CAPTURE_IGNORE="mismatch,gcov,unused,empty,negative,source,graph,inconsistent,corrupt"
# Narrower lists downstream, so that error classes which cannot legitimately arise in a step
# are not blanket-suppressed there.  `unused` is required for the filter step because an
# exclusion glob that matches nothing is an ERROR in lcov 2.x (exit 25) and the glob lists are
# reproduced verbatim rather than pruned to whatever this tree happens to contain.
readonly LCOV_FILTER_IGNORE="unused,empty,inconsistent"
# `inconsistent` is required for the summary and gate steps: this tree yields "line is hit but
# no branches on line have been evaluated" records, and without the ignore lcov escalates them
# to a `corrupt` read failure and exits non-zero -- which would make the gate fail for a reason
# that has nothing to do with coverage, i.e. would make the gate lie.  `corrupt` itself is NOT
# ignored, so a genuinely truncated trace still stops the run.
readonly LCOV_SUMMARY_IGNORE="empty,inconsistent"
# genhtml additionally needs `source`, because the trace records absolute paths from the build
# host and a source file that cannot be re-read is otherwise fatal to the report.
readonly GENHTML_IGNORE="empty,inconsistent,source"

# Filled by resolve_lcov_config() with `--config-file <this level's .lcovrc>` when the
# repository ships one, so that the settings in effect are the ones this repository versions
# rather than whatever the caller's home directory holds.  Tests/L1Tests/.lcovrc_l1 exists and
# asks in its own comments to be used exactly this way; there is no Tests/L2Tests/.lcovrc_l2,
# so L2 runs without one and relies on the --rc override alone, which is sufficient.
LCOV_CONFIG_ARGS=()

# ------------------------------------------------------------------------------------
# Exclusion globs, reproduced VERBATIM from the workflows, in the workflow's own order.
#
# THESE ARE LOAD-BEARING.  Neither list excludes */plugin/*, and that is the whole point:
# the coverage denominator is production source only, which is what forces coverage to move
# by ADDING TESTS rather than by editing production code.  Adding an exclusion -- above all
# anything matching plugin/ -- would flatter the percentage and break the honest-reporting
# contract; removing one would change the denominator away from CI's.  So: add nothing,
# remove nothing.
#
# The doubled token in the L2 list (`entservices-entservices-testframework`, L2-tests.yml:775)
# is doubled in the workflow too.  It is reproduced as written: "correcting" it would change
# which paths are removed and therefore the denominator this gate is applied to.
# ------------------------------------------------------------------------------------
readonly L1_EXCLUDES=(
    '/usr/include/*'
    '*/build/entservices-hdmicecsource/_deps/*'
    '*/install/usr/include/*'
    '*/Tests/headers/*'
    '*/Tests/mocks/*'
    '*/Tests/L1Tests/tests/*'
    '*/Thunder/*'
)
readonly L2_EXCLUDES=(
    '/usr/include/*'
    '*/build/entservices-hdmicecsource/_deps/*'
    '*/build/entservices-deviceanddisplay/_deps/*'
    '*/build/entservices-entservices-testframework/_deps/*'
    '*/build/mocks/*'
    '*/install/usr/include/*'
    '*/Tests/headers/*'
    '*/Tests/mocks/*'
    '*/Tests/L2Tests/*'
    '*/sqlite/*'
)

# ------------------------------------------------------------------------------------
# Files whose line-coverage VERDICT is waived at a given level.  They stay in the
# denominator, are still captured, and are still printed with their real figures; only the
# pass/fail judgement is waived, and every waiver is enumerated in the output with its
# reason so the traceability report can quote it.  No exclusion glob is used for this --
# filtering a file out of the denominator to make a percentage look better is precisely
# what the honest-reporting contract forbids.
#
#   plugin/Module.cpp at L1 -- its one instrumented line and its two functions are generated
#   by the plugin module-declaration macro, whose build-reference and service-metadata
#   accessors only the Thunder plugin loader calls at load time.  An in-process L1
#   GoogleTest binary never loads the plugin through a live host, so the line is unreachable
#   from L1.  The list is LEVEL-AWARE because that is the measured truth: the same file
#   measures 1/1 lines and 2/2 functions under L2, which does start a real Thunder host, with
#   no production change of any kind.  Calling it "uncoverable" without qualifying the level
#   would therefore be false, so the waiver is scoped to the level that genuinely cannot
#   reach it.
# ------------------------------------------------------------------------------------
readonly L1_GATE_EXEMPT=(
    'plugin/Module.cpp'
)
readonly L2_GATE_EXEMPT=()

# ------------------------------------------------------------------------------------
# Must-not-regress floors, recorded from the measured baseline for this submodule.  A floor
# is not a target to descend to: a target already above the bar must not lose coverage as
# tests are added elsewhere.  These are the only historical numbers in this script and they
# are labelled as such wherever they are printed -- everything else is measured live.
#
# Format: <path relative to the repository>=<recorded baseline line coverage percentage>
#
# LEVEL-SCOPED, and for a measured reason.  The recorded baselines were taken from the L1
# suite, and L1 and L2 exercise genuinely different code: the same file measures 82.1% under
# L1 and 81.6% under L2, and HdmiCecSourceImplementation.cpp measures 86.1% under L1 and
# 71.2% under L2, simply because an in-process unit suite and a Thunder-hosted functional
# suite reach different paths.  Applying an L1 baseline to an L2 trace would therefore report
# a "regression" that never happened, so no floor is asserted at L2 -- the specification
# records no L2 baseline to assert one from.  Inventing one would be a fabricated claim.
# ------------------------------------------------------------------------------------
readonly L1_COVERAGE_FLOORS=(
    'plugin/HdmiCecSource.h=85.7'
    'plugin/HdmiCecSourceImplementation.h=82.1'
    'plugin/HdmiCecSourceImplementation.cpp=81.8'
)
readonly L2_COVERAGE_FLOORS=()

# The Directive 4 acceptance target for this submodule, called out in the report so it cannot
# be lost in the table.  Its recorded baseline was 73.6% (39/53 lines), 75.0% (3/4 functions),
# 30.0% (18/60 branches) -- four covered lines short of the bar.
readonly ACCEPTANCE_TARGET='plugin/HdmiCecSource.cpp'

log()  { printf '[run_coverage] %s\n' "$*"; }
warn() { printf '[run_coverage] WARNING: %s\n' "$*" >&2; }
die()  { printf '[run_coverage] ERROR: %s\n' "$*" >&2; exit 1; }
rule() { printf '%s\n' '-------------------------------------------------------------------------------'; }

# ------------------------------------------------------------------------------------
# $HOME/.lcovrc stewardship.
#
# lcov reads $HOME/.lcovrc silently, and both workflows PLANT a branch-disabled copy there
# before capturing (L1-tests.yml:685, L2-tests.yml:764).  Leaving such a file in place
# suppresses exactly the branch data this script exists to collect, so it has to go before
# any lcov invocation -- not for tidiness, but because the measurement is wrong otherwise.
#
# The removal is done without destroying anything: the file is copied into a private
# mode-0700 mktemp directory first, then removed, and restored by the cleanup trap however
# the run ends -- normal exit, gate failure, or Ctrl-C.  The stash path is logged so the
# copy is never a mystery.
# ------------------------------------------------------------------------------------
LCOVRC_STASH_DIR=''
LCOVRC_STASHED=0

restore_home_lcovrc() {
    [ "$LCOVRC_STASHED" -eq 1 ] || return 0
    if [ -f "$LCOVRC_STASH_DIR/lcovrc" ]; then
        if cp -p -- "$LCOVRC_STASH_DIR/lcovrc" "$HOME/.lcovrc" 2>/dev/null; then
            log "restored your original $HOME/.lcovrc"
        else
            warn "could not restore $HOME/.lcovrc; your copy is preserved at $LCOVRC_STASH_DIR/lcovrc"
            return 0
        fi
    fi
    rm -f -- "$LCOVRC_STASH_DIR/lcovrc" 2>/dev/null || true
    rmdir -- "$LCOVRC_STASH_DIR" 2>/dev/null || true
    LCOVRC_STASHED=0
}

# `rm -f ~/.lcovrc` FIRST, before any lcov invocation -- with the original preserved.
neutralise_home_lcovrc() {
    [ "$LCOVRC_STASHED" -eq 0 ] || return 0
    if [ -e "$HOME/.lcovrc" ]; then
        [ -n "$MKTEMP_BIN" ] || die "mktemp is required to preserve your $HOME/.lcovrc safely."
        LCOVRC_STASH_DIR="$("$MKTEMP_BIN" -d "${TMPDIR:-/tmp}/run_coverage_lcovrc.XXXXXX")"
        chmod 0700 -- "$LCOVRC_STASH_DIR"
        cp -p -- "$HOME/.lcovrc" "$LCOVRC_STASH_DIR/lcovrc"
        LCOVRC_STASHED=1
        rm -f ~/.lcovrc
        log "removed $HOME/.lcovrc for this run (CI plants a branch-disabled copy there);"
        log "    your original is stashed at $LCOVRC_STASH_DIR/lcovrc and is restored on exit"
    else
        rm -f ~/.lcovrc
        log "no $HOME/.lcovrc present, so nothing there can suppress branch data"
    fi
}

on_exit() {
    local rc=$?
    restore_home_lcovrc
    return "$rc"
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

usage() {
    cat <<USAGE
Usage: $(basename -- "$SCRIPT_PATH") [l1|l2|all]

Runs an HDMI-CEC SOURCE plugin test suite, captures gcov/lcov coverage with branch data
enabled, writes the HTML report, prints a per-file table of line, function and branch
figures derived from the trace records, and applies a >=${COVERAGE_MIN}% line-coverage gate to
the aggregate and to every individual target.

Each level zeroes its own *.gcda counters before running the suite, so the figures describe
this run only, and writes every artifact into a per-plugin, per-level directory.

Levels:
  l1     Run RdkServicesL1Test, then capture, report and gate L1 coverage.
  l2     Run RdkServicesL2Test, then capture, report and gate L2 coverage.
  all    Run l1 then l2, sequentially (the default).  Fails if either level fails; the
         remaining level is not run and the level that failed is named.  Because an L1 tree
         and an L2 tree are NOT interchangeable, 'all' requires EITHER separate per-level
         build/install directories OR a LEVEL_REBUILD_CMD hook, and refuses to start
         without one of them rather than measure one tree twice.

Environment variables (all optional; shown with their defaults):
  WS=<workspace root>            Resolved by walking up from this script
                                 (Tests/ -> repository -> workspace), mirroring CI's
                                 \$GITHUB_WORKSPACE.  Currently: $WS
  BUILD_DIR=\$WS/build/$REPO_NAME
                                 Directory passed to 'lcov -c -d', matching both
                                 workflows.  Currently: $BUILD_DIR
  INSTALL_DIR=\$WS/install        Install tree providing the test binaries and the plugin
                                 libraries.  Currently: $INSTALL_DIR
  L1_BUILD_DIR / L2_BUILD_DIR    Per-level build trees; default to BUILD_DIR.
                                 Currently: $L1_BUILD_DIR
                                        and $L2_BUILD_DIR
  L1_INSTALL_DIR / L2_INSTALL_DIR
                                 Per-level install trees; default to INSTALL_DIR.
                                 Currently: $L1_INSTALL_DIR
                                        and $L2_INSTALL_DIR
  LEVEL_REBUILD_CMD=<unset>      Command that switches a shared tree to a level.  Invoked
                                 as '<cmd> <level>' before each level under 'all'; it owns
                                 the documented plugin -> testframework -> mocks rebuild
                                 sequence.  Currently: ${LEVEL_REBUILD_CMD:-<unset>}
  ARTIFACT_ROOT=\$WS/coverage-artifacts
                                 Root of the artifact tree; this run writes to
                                 \$ARTIFACT_ROOT/$REPO_NAME/<level>/.  Disposable build
                                 output -- never commit it.  Currently: $ARTIFACT_ROOT
  COVERAGE_MIN=80                Line-coverage bar, applied to the level aggregate AND to
                                 each target.  Currently: $COVERAGE_MIN
  RUN_VALGRIND=0                 Set to 1/true/yes/on to run the suite under valgrind
                                 memcheck with the options CI uses.  Never a gate.
                                 Currently: $RUN_VALGRIND

Artifacts (fixed names, no timestamps) in \$ARTIFACT_ROOT/$REPO_NAME/<level>/:
  coverage_<level>.info, filtered_coverage_<level>.info, coverage_<level>/index.html,
  rdk<LEVEL>TestResults.json, and valgrind_log when RUN_VALGRIND is enabled.
  At L2 the results file is written by out-of-scope framework code
  (entservices-testframework/Tests/L2Tests/L2testController.cpp:91-93 exports GTEST_OUTPUT
  before spawning WPEFramework), so it always lands in the suite's working directory -- the
  parent of the install tree -- as rdkL2TestResults.json; that path is cleared before the run
  and the file archived into the artifact directory afterwards.

The suite runs with its working directory set to the PARENT OF THE INSTALL TREE, because
L2testController.cpp:344 opens the hard-coded relative path './install/etc/WPEFramework/
plugins/'.  With the CI layout (INSTALL_DIR=\$WS/install) that is \$WS, exactly as in CI.

BUILD THE PLUGIN *AND* REBUILD entservices-testframework AGAINST IT BEFORE RUNNING.  Both
plugins emit identically named test libraries, so a stale framework build silently measures
the other plugin.  This script inspects the installed library's symbols and refuses to run
when they are the other plugin's.  The full recipe is in this script's header comment and is
echoed by --help-build.

  $(basename -- "$SCRIPT_PATH") --help-build     print the verified build recipe and exit
USAGE
}

# ------------------------------------------------------------------------------------
# The verified build recipe, echoed rather than executed.  This script measures; it does not
# own the build.  Printing the recipe keeps the run reproducible from the script itself, as
# the reproducibility clause requires, without pretending that a coverage runner is the right
# place to drive a nine-stage cross-repository build.  Wire it into LEVEL_REBUILD_CMD if you
# want `all` to switch a shared tree between levels.
# ------------------------------------------------------------------------------------
print_build_recipe() {
    cat <<'RECIPE'
Verified build recipe for the HDMI-CEC source plugin's test suites.
Run from the workspace root ($WS).  Dependency order matters; each stage installs into
$WS/install/usr and the next stage finds it there:

  ThunderTools (patched) -> Thunder (patched) -> entservices-apis -> external empty headers
    -> GoogleTest -> entservices-helpers -> mocks -> THE PLUGIN -> entservices-testframework

  pip install --break-system-packages jsonref      # required by the plugin configure step
  export PATH=/opt/cmake316/bin:$PATH              # CMake 3.16.x is a HARD constraint:
                                                   # 3.20+ fails the test-library configure

L1:
  cmake -S entservices-hdmicecsource -B build/entservices-hdmicecsource \
    -DPLUGIN_HDMICECSOURCE=ON -DRDK_SERVICES_L1_TEST=ON \
    -DUSE_THUNDER_R4=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build build/entservices-hdmicecsource -j$(nproc)
  cmake --install build/entservices-hdmicecsource

L2 (same, plus):
    -DPLUGIN_L2Tests=ON -DRDK_SERVICE_L2_TEST=ON
  and apply the L2 Thunder timeout patch BEFORE building Thunder.

NOTE THE ASYMMETRIC FLAG SPELLINGS, and do not "normalise" them -- both are as the
framework and the workflows declare them:
    RDK_SERVICES_L1_TEST   is PLURAL   (L1-tests.yml:486)
    RDK_SERVICE_L2_TEST    is SINGULAR (L2-tests.yml:20, :551)

Platform discovery is suppressed so that the force-included mocks are used instead of real
platform libraries:
    -DCMAKE_DISABLE_FIND_PACKAGE_DS=ON       -DCMAKE_DISABLE_FIND_PACKAGE_IARMBus=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_Udev=ON     -DCMAKE_DISABLE_FIND_PACKAGE_RFC=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_RBus=ON     -DCMAKE_DISABLE_FIND_PACKAGE_CEC=ON
Coverage instrumentation is already wired (Tests/gcc-with-coverage.cmake appends --coverage);
it needs no change and must not be edited.

On a host newer than CI's image, GCC-13 promotes several diagnostics to errors under the
framework's -Wall -Werror.  Pass the needed -Wno-error= relaxations in CMAKE_CXX_FLAGS at
INVOCATION time only -- never commit them into a build file.

THEN, AND THIS IS THE STEP THAT BITES WHEN SKIPPED, rebuild the test framework against the
plugin you just built, because both plugins emit libWPEFrameworkL1TestsIO.so /
libWPEFrameworkL2TestsIO.so and the second build overwrites the first:

  rm -rf build/entservices-testframework
  cmake -S entservices-testframework -B build/entservices-testframework \
    -DPLUGIN_HDMICECSOURCE=ON -DRDK_SERVICES_L1_TEST=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build build/entservices-testframework -j$(nproc)
  cmake --install build/entservices-testframework

Order per plugin, without exception:
  build plugin -> rebuild+reinstall testframework -> run -> capture -> other plugin
RECIPE
}

valgrind_enabled() {
    case "$(printf '%s' "$RUN_VALGRIND" | tr '[:upper:]' '[:lower:]')" in
        1|true|yes|on) return 0 ;;
        *)             return 1 ;;
    esac
}


# ------------------------------------------------------------------------------------
# Self-documenting banner: the resolved configuration and the tool versions that produced
# the figures.  Coverage numbers are compiler- and lcov-sensitive at the margin, so a run
# that does not say which toolchain produced it is not reproducible evidence.
# ------------------------------------------------------------------------------------
print_configuration() {
    local levels="$1"
    rule
    log "HDMI-CEC source plugin coverage runner"
    log "  repository        : $REPO_ROOT"
    log "  workspace (WS)    : $WS"
    log "  levels to run     : $levels"
    log "  build dir (L1/L2) : $L1_BUILD_DIR"
    log "                      $L2_BUILD_DIR"
    log "  install (L1/L2)   : $L1_INSTALL_DIR"
    log "                      $L2_INSTALL_DIR"
    log "  artifact root     : $ARTIFACT_ROOT/$REPO_NAME/<level>/  (disposable; never commit)"
    log "  line-coverage bar : ${COVERAGE_MIN}%  (aggregate and per target)"
    log "  branch coverage   : collected and reported, NOT gated"
    log "  valgrind          : $(valgrind_enabled && echo 'enabled (never a gate)' || echo 'disabled')"
    log "  level rebuild hook: ${LEVEL_REBUILD_CMD:-<unset>}"
    rule
    log "toolchain that produced the figures below:"
    # `|| true` here guards only the VERSION ECHO, never a measurement: an lcov that cannot
    # even report its version is diagnosed by preflight() with a real message instead of
    # leaving a blank line in the banner.
    log "  lcov    : $("$LCOV_BIN" --version 2>/dev/null | head -1 || true)"
    log "  genhtml : $("$GENHTML_BIN" --version 2>/dev/null | head -1 || true)"
    if [ -n "$GCOV_BIN" ]; then
        log "  gcov    : $("$GCOV_BIN" --version 2>/dev/null | head -1)"
    else
        log "  gcov    : not on PATH (only needed to (re)generate .gcda data, not to read it)"
    fi
    if [ -n "$CMAKE_BIN" ]; then
        log "  cmake   : $("$CMAKE_BIN" --version 2>/dev/null | head -1)  (3.16.x required to BUILD; 3.20+ fails the test-library configure)"
    else
        log "  cmake   : not on PATH -- fine for measuring an existing tree; needed to build one (use /opt/cmake316)"
    fi
    rule
}

# ------------------------------------------------------------------------------------
# Preconditions.  Every one of these has been a real failure mode, and each is checked here
# so that it surfaces as a named refusal instead of as a coverage figure that looks credible
# and is wrong.
# ------------------------------------------------------------------------------------
preflight() {
    [ -n "$LCOV_BIN" ]    || die "lcov not found on PATH.  lcov 2.0-1 or newer is required (it provides --fail-under-lines)."
    [ -n "$GENHTML_BIN" ] || die "genhtml not found on PATH; it ships with lcov."
    [ -n "$FIND_BIN" ]    || die "find not found on PATH."
    [ -n "$MKTEMP_BIN" ]  || die "mktemp not found on PATH."

    # lcov must actually be runnable before anything else is believed about it.  A broken or
    # hostile configuration file makes EVERY invocation fail -- `lcov --version` included -- so
    # this check distinguishes "lcov is unusable here" from "lcov is too old", which are very
    # different problems with very different fixes.
    if ! "$LCOV_BIN" --version >/dev/null 2>&1; then
        die "$LCOV_BIN cannot even report its version, so it is unusable in this environment.
       The usual cause is a bad lcov configuration file: this script removes \$HOME/.lcovrc for
       the run, but /etc/lcovrc (system-wide, out of scope for this script) or a --config-file
       is read too.  Reproduce with:  $LCOV_BIN --version
       For example, setting both 'lcov_branch_coverage' and 'genhtml_branch_coverage' makes
       lcov 2.0-1 fail every invocation with 'unexpected ARRAY for branch_coverage value'."
    fi

    # lcov 1.x has neither --fail-under-lines nor `--rc branch_coverage=1`, so the gate would
    # silently not exist and branch data would silently not appear.  Refuse rather than
    # produce a report that is missing the two things this script is for.
    if ! "$LCOV_BIN" --help 2>&1 | grep -q -- '--fail-under-lines'; then
        die "this lcov does not support --fail-under-lines, so the ${COVERAGE_MIN}% gate cannot be
       enforced.  Install lcov 2.0 or newer; refusing to report coverage without the gate."
    fi

    [ -d "$REPO_ROOT/plugin" ] || die "expected the plugin source at $REPO_ROOT/plugin -- is this really $REPO_NAME?"

    # ------------------------------------------------------------------------------
    # The exclusion lists are load-bearing, so their shape is asserted rather than trusted.
    # The counts are the workflow's own (L1-tests.yml:693-699 = 7 globs;
    # L2-tests.yml:772-781 = 10 globs), and no glob may match plugin/ because the plugin
    # sources ARE the denominator.  A future edit that adds a convenient exclusion, drops
    # one, or "corrects" the doubled L2 token therefore fails here instead of quietly
    # producing a different -- and flattering -- percentage.
    # ------------------------------------------------------------------------------
    [ "${#L1_EXCLUDES[@]}" -eq 7 ] || die "the L1 exclusion list has ${#L1_EXCLUDES[@]} globs; the workflow
       (.github/workflows/L1-tests.yml:693-699) has exactly 7.  The globs define the coverage
       denominator, so they are reproduced verbatim: add none, remove none."
    [ "${#L2_EXCLUDES[@]}" -eq 10 ] || die "the L2 exclusion list has ${#L2_EXCLUDES[@]} globs; the workflow
       (.github/workflows/L2-tests.yml:772-781) has exactly 10.  The globs define the coverage
       denominator, so they are reproduced verbatim: add none, remove none."
    local glob
    for glob in "${L1_EXCLUDES[@]}" "${L2_EXCLUDES[@]}"; do
        case "$glob" in
            *plugin*) die "the exclusion glob '$glob' matches plugin/, which would remove production
       source from the coverage denominator.  Neither workflow excludes */plugin/*, and that is
       deliberate: it is what forces coverage to move by ADDING TESTS rather than by editing or
       hiding production code.  Refusing to report a flattered percentage." ;;
        esac
    done

    case "$COVERAGE_MIN" in
        ''|*[!0-9.]*) die "COVERAGE_MIN must be a number (got '$COVERAGE_MIN')." ;;
    esac
    awk -v v="$COVERAGE_MIN" 'BEGIN { exit !(v + 0 >= 0 && v + 0 <= 100) }' \
        || die "COVERAGE_MIN must be between 0 and 100 (got '$COVERAGE_MIN')."
    if awk -v v="$COVERAGE_MIN" 'BEGIN { exit !(v + 0 != 80) }'; then
        warn "COVERAGE_MIN is ${COVERAGE_MIN}%, not the required 80%.  This is a diagnostic run:"
        warn "    its verdict is NOT the acceptance verdict for this submodule."
    fi
}

# lcov refuses to read a tree with no coverage data, but the error is generic; naming the
# actual cause (unbuilt tree, wrong directory, uninstrumented build) saves the reader a hunt.
validate_build_dir() {
    local dir="$1" level="$2" gcno
    [ -d "$dir" ] || die "build directory for ${level^^} does not exist: $dir
       Build the plugin first -- run '$(basename -- "$SCRIPT_PATH") --help-build' for the recipe --
       or point ${level^^}_BUILD_DIR/BUILD_DIR at the tree that holds this plugin's *.gcno files."
    gcno="$("$FIND_BIN" "$dir" -name '*.gcno' -print -quit 2>/dev/null || true)"
    [ -n "$gcno" ] || die "no *.gcno files under $dir, so nothing there is instrumented for coverage.
       Configure with Tests/gcc-with-coverage.cmake in CMAKE_CXX_FLAGS (it appends --coverage)
       and rebuild; see '$(basename -- "$SCRIPT_PATH") --help-build'."
}

validate_install_dir() {
    local dir="$1" level="$2" binary="$3"
    [ -d "$dir" ] || die "install tree for ${level^^} does not exist: $dir
       Run 'cmake --install' for the plugin and for entservices-testframework, then retry."
    [ -x "$dir/usr/bin/$binary" ] || die "$binary is not executable at $dir/usr/bin/$binary.
       The suite binary comes from entservices-testframework; build and install it against
       THIS plugin.  See '$(basename -- "$SCRIPT_PATH") --help-build'."
    [ -d "$dir/usr/lib/wpeframework/plugins" ] || die "missing $dir/usr/lib/wpeframework/plugins.
       That directory must be on LD_LIBRARY_PATH or the plugin under test will not load."
}

# ------------------------------------------------------------------------------------
# THE COLLISION GUARD.
#
# entservices-hdmicecsource and entservices-hdmicecsink both build their test cases into
# libWPEFrameworkL1TestsIO.so / libWPEFrameworkL2TestsIO.so (both Tests/L1Tests/
# CMakeLists.txt:19 read `set(PLUGIN_NAME L1TestsIO)`; both L2 files read `L2TestsIO`), and
# RdkServicesL1Test itself compiles only test_JSON.cpp -- the plugin's own cases arrive
# through that shared library.  So whichever plugin's test framework was installed LAST is
# the one that gets measured, regardless of which plugin's build tree lcov reads.  That
# failure is silent and produces a completely credible-looking number for the wrong plugin.
#
# The symbols are decisive and mutually exclusive: this plugin's mangled names contain
# "HdmiCecSource", the sink's contain "HdmiCecSink", and neither string is a substring of the
# other.  nm is preferred; a raw binary grep is the fallback so the check still happens on a
# host without binutils.  If neither can be used the run is refused rather than trusted --
# SKIP_LIBRARY_PROVENANCE_CHECK=1 exists as a documented, deliberate override.
# ------------------------------------------------------------------------------------
count_symbol_occurrences() { # $1=library  $2=token -> count on stdout
    local lib="$1" token="$2"
    if [ -n "$NM_BIN" ] && "$NM_BIN" -D --defined-only -- "$lib" >/dev/null 2>&1; then
        "$NM_BIN" -D --defined-only -- "$lib" 2>/dev/null | grep -c -- "$token" || true
    else
        grep -a -c -- "$token" "$lib" 2>/dev/null || true
    fi
}

verify_library_provenance() {
    local dir="$1" level="$2"
    local libname own_token rival_token lib own rival
    case "$level" in
        l1) libname='libWPEFrameworkL1TestsIO.so' ;;
        l2) libname='libWPEFrameworkL2TestsIO.so' ;;
        *)  die "verify_library_provenance: unknown level '$level'" ;;
    esac
    own_token='HdmiCecSource'
    rival_token='HdmiCecSink'

    lib="$dir/usr/lib/$libname"
    if [ ! -f "$lib" ]; then
        die "the ${level^^} test library $libname is not installed at $lib.
       This plugin's test cases live in that library, so without it the suite would measure
       nothing of this plugin.  Rebuild and reinstall entservices-testframework against
       $REPO_NAME; see '$(basename -- "$SCRIPT_PATH") --help-build'."
    fi

    if [ "${SKIP_LIBRARY_PROVENANCE_CHECK:-0}" = 1 ]; then
        warn "SKIP_LIBRARY_PROVENANCE_CHECK=1: not verifying that $libname belongs to $REPO_NAME."
        warn "    If it was built for the sink, the figures below describe the SINK, not this plugin."
        return 0
    fi

    own="$(count_symbol_occurrences "$lib" "$own_token")"
    rival="$(count_symbol_occurrences "$lib" "$rival_token")"
    : "${own:=0}" "${rival:=0}"

    if [ "$own" -eq 0 ] && [ "$rival" -eq 0 ]; then
        die "could not find either $own_token or $rival_token symbols in $lib, so it cannot be
       established that the installed ${level^^} test library belongs to $REPO_NAME.  Refusing to
       report a coverage figure that may describe the other plugin.  Rebuild and reinstall
       entservices-testframework against this plugin, or set
       SKIP_LIBRARY_PROVENANCE_CHECK=1 to override deliberately."
    fi
    if [ "$rival" -gt "$own" ]; then
        die "$lib carries $rival $rival_token symbols and only $own $own_token symbols: it was built
       for entservices-hdmicecsink, not $REPO_NAME.  Both plugins emit this same library name,
       so the sink build has overwritten this plugin's.  Rebuild the plugin, then rebuild AND
       reinstall entservices-testframework against it, then re-run.  See
       '$(basename -- "$SCRIPT_PATH") --help-build'."
    fi
    log "provenance OK: $libname carries $own $own_token symbols ($rival $rival_token) -> this plugin"
}


# ------------------------------------------------------------------------------------
# CLI contract.  Exactly one optional positional argument: l1, l2 or all (default all).
# Anything else prints usage and exits non-zero -- a typo must never be silently interpreted
# as "measure everything" or, worse, as a level that then reports the wrong figures.
# `parse_level` echoes the space-separated levels to run.
# ------------------------------------------------------------------------------------
parse_level() {
    local arg="${1:-all}"
    case "$arg" in
        l1)  printf 'l1' ;;
        l2)  printf 'l2' ;;
        all) printf 'l1 l2' ;;
        *)   return 1 ;;
    esac
}

# ------------------------------------------------------------------------------------
# Per-level input resolution and artifact directory.
# ------------------------------------------------------------------------------------
resolve_level_inputs() {
    local level="$1"
    case "$level" in
        l1) LEVEL_BUILD_DIR="$L1_BUILD_DIR"; LEVEL_INSTALL_DIR="$L1_INSTALL_DIR" ;;
        l2) LEVEL_BUILD_DIR="$L2_BUILD_DIR"; LEVEL_INSTALL_DIR="$L2_INSTALL_DIR" ;;
        *)  die "resolve_level_inputs: unknown level '$level'" ;;
    esac
    # ARTIFACT_ROOT is validated BEFORE it is used, because the HTML report directory is
    # replaced with `rm -rf` on every run and a derived path must never be allowed to collapse
    # to something dangerous.  Absolute, non-root, non-trivial: anything else is refused.
    case "$ARTIFACT_ROOT" in
        /)   die "ARTIFACT_ROOT must not be '/'." ;;
        /*)  : ;;
        *)   die "ARTIFACT_ROOT must be an absolute path (got '$ARTIFACT_ROOT')." ;;
    esac
    [ "${#ARTIFACT_ROOT}" -gt 4 ] || die "ARTIFACT_ROOT '$ARTIFACT_ROOT' is implausibly short; refusing to
       create and delete report directories underneath it."

    LEVEL_ARTIFACT_DIR="$ARTIFACT_ROOT/$REPO_NAME/$level"

    # An artifact destination that is a symlink, or an existing non-directory, is refused
    # rather than followed or clobbered: this script writes reports, it does not overwrite
    # whatever happens to be sitting at a path.
    if [ -L "$LEVEL_ARTIFACT_DIR" ]; then
        die "$LEVEL_ARTIFACT_DIR is a symlink; refusing to write artifacts through it."
    fi
    if [ -e "$LEVEL_ARTIFACT_DIR" ] && [ ! -d "$LEVEL_ARTIFACT_DIR" ]; then
        die "$LEVEL_ARTIFACT_DIR exists and is not a directory; refusing to write artifacts there."
    fi
    mkdir -p -- "$LEVEL_ARTIFACT_DIR"
}

# ------------------------------------------------------------------------------------
# Runtime environment for the suite, mirroring the workflows' run steps exactly
# (L1-tests.yml:658-659, L2-tests.yml:739-740).  usr/lib/wpeframework/plugins is MANDATORY:
# omit it and the plugin under test does not load, which shows up as a suite failure with no
# obvious cause.  Both entries are computed from the pristine base paths so that a second
# level cannot inherit the first level's install tree.
# ------------------------------------------------------------------------------------
setup_runtime_env() {
    local install="$1"
    export PATH="$install/usr/bin:$BASE_PATH"
    export LD_LIBRARY_PATH="$install/usr/lib:$install/usr/lib/wpeframework/plugins:$BASE_LD_LIBRARY_PATH"
}

# ------------------------------------------------------------------------------------
# Counter hygiene.  gcov ACCUMULATES into *.gcda across runs, so a tree that has already been
# exercised would hand back the union of every previous run.  Zeroing first is what makes the
# printed figures attributable to this run, which the measured-claims-only clause requires.
# `lcov -z` is used rather than deleting files, so the instrumentation itself is untouched.
# ------------------------------------------------------------------------------------
gcda_count() {
    "$FIND_BIN" "$1" -name '*.gcda' 2>/dev/null | wc -l | tr -d '[:space:]'
}

zero_counters() {
    local dir="$1" before
    before="$(gcda_count "$dir")"
    log "zeroing coverage counters in $dir ($before *.gcda present)"
    if ! "$LCOV_BIN" -z -d "$dir" \
            --ignore-errors "$LCOV_FILTER_IGNORE" >/dev/null 2>&1; then
        # -z is a convenience, not the measurement: if lcov cannot zero (for instance a
        # read-only tree) say so plainly, because the figures may then include an earlier run.
        warn "lcov -z could not reset the counters in $dir."
        warn "    Figures may include data accumulated by earlier runs of this tree."
        return 0
    fi
    log "counters zeroed (remaining *.gcda: $(gcda_count "$dir"))"
}

# After the suite has run there MUST be fresh .gcda data, or there is nothing to measure and
# any number produced would be meaningless.
verify_fresh_counters() {
    local dir="$1" level="$2" count
    count="$(gcda_count "$dir")"
    [ "${count:-0}" -gt 0 ] || die "no *.gcda files appeared in $dir after the ${level^^} suite ran.
       The suite did not exercise the instrumented objects in this tree -- most often the
       build directory belongs to a different level or a different plugin than the installed
       test library.  Refusing to report a coverage figure that was not measured."
    log "fresh coverage data present after the run: $count *.gcda files"
}

# ------------------------------------------------------------------------------------
# Suite execution.  Both suites MUST exit 0 -- that is the runtime acceptance condition, so a
# non-zero suite exit fails the run here and the gate is never reached.  Reporting coverage
# for a red suite would be reporting how much code a broken test run happened to touch.
# ------------------------------------------------------------------------------------
suite_binary_for_level() {
    case "$1" in
        l1) printf 'RdkServicesL1Test' ;;
        l2) printf 'RdkServicesL2Test' ;;
        *)  die "suite_binary_for_level: unknown level '$1'" ;;
    esac
}

run_suite() {
    local level="$1"
    local binary results_name results_path framework_results run_dir rc=0
    binary="$(suite_binary_for_level "$level")"
    results_name="rdk${level^^}TestResults.json"
    results_path="$LEVEL_ARTIFACT_DIR/$results_name"

    # WORKING DIRECTORY: the parent of the install tree, not simply $WS.
    #
    # In CI these are the same thing -- the install prefix is $GITHUB_WORKSPACE/install and the
    # workflow's working directory is $GITHUB_WORKSPACE -- so this reproduces CI exactly for the
    # default layout.  It matters whenever INSTALL_DIR is overridden to somewhere outside $WS,
    # because the L2 controller resolves a RELATIVE path: out-of-scope framework code at
    # entservices-testframework/Tests/L2Tests/L2testController.cpp:344 opens
    # "./install/etc/WPEFramework/plugins/" to switch autostart off for every plugin but its
    # own, and returns EXIT_AUTOSTART_FAILURE ("Error opening directory") if that directory is
    # not reachable from the working directory.  Anchoring on the install tree's parent is what
    # makes "./install/..." resolve, whatever INSTALL_DIR is.
    run_dir="$(dirname -- "$LEVEL_INSTALL_DIR")"
    framework_results="$run_dir/rdkL2TestResults.json"

    rule
    log "running the ${level^^} suite: $binary"
    log "  working dir     = $run_dir  (so the framework's './install/...' paths resolve)"
    log "  PATH            = $PATH"
    log "  LD_LIBRARY_PATH = $LD_LIBRARY_PATH"

    # The relative path above is literally "./install/...", so the install tree has to BE called
    # "install".  That is the framework's assumption, not this script's, and it cannot be fixed
    # from here -- so it is surfaced as a warning rather than allowed to look like a test failure.
    if [ "$level" = l2 ] && [ "$(basename -- "$LEVEL_INSTALL_DIR")" != install ]; then
        warn "the L2 install tree is named '$(basename -- "$LEVEL_INSTALL_DIR")', not 'install'."
        warn "    L2testController.cpp:344 opens the hard-coded relative path"
        warn "    './install/etc/WPEFramework/plugins/', so it will not find the plugin configs and"
        warn "    the suite will exit with an autostart failure.  Point L2_INSTALL_DIR/INSTALL_DIR at"
        warn "    a directory named 'install'.  (Framework code is out of scope for this change.)"
    fi

    rm -f -- "$results_path"

    # At L2 the results file is written by out-of-scope framework code:
    # entservices-testframework/Tests/L2Tests/L2testController.cpp:91-93 spawns WPEFramework
    # with `export GTEST_OUTPUT="json:$PWD/rdkL2TestResults.json"`, which overrides whatever
    # this script exports.  So the L2 file always appears at that fixed path; it is cleared
    # first, then archived into the artifact directory afterwards.  At L1 the binary honours
    # GTEST_OUTPUT, so the file is written straight into the artifact directory.
    if [ "$level" = l2 ]; then
        rm -f -- "$framework_results"
    fi

    (
        cd -- "$run_dir" || exit 1
        export GTEST_OUTPUT="json:$results_path"
        if valgrind_enabled; then
            [ -n "$VALGRIND_BIN" ] || die "RUN_VALGRIND is set but valgrind is not on PATH."
            log "valgrind memcheck enabled with the options CI uses; it is NOT a gate"
            "$VALGRIND_BIN" \
                --tool=memcheck \
                --log-file="$LEVEL_ARTIFACT_DIR/valgrind_log" \
                --leak-check=yes \
                --show-reachable=yes \
                --track-fds=yes \
                --fair-sched=try \
                "$binary"
        else
            "$binary"
        fi
    ) || rc=$?

    if [ "$level" = l2 ] && [ -f "$framework_results" ]; then
        cp -f -- "$framework_results" "$results_path" 2>/dev/null || true
        rm -f -- "$framework_results"
    fi

    if [ "$rc" -ne 0 ]; then
        die "the ${level^^} suite exited $rc.  L1 and L2 must pass at runtime, so this run stops here:
       coverage is not reported for a failing suite.  Results, if the run produced any, are at
       $results_path"
    fi
    log "${level^^} suite exited 0"
    if [ -f "$results_path" ]; then
        log "results archived: $results_path"
    else
        warn "the suite passed but produced no $results_name; the coverage figures are unaffected."
    fi
}

# ------------------------------------------------------------------------------------
# `--config-file` for the level, when this repository ships one.  Tests/L1Tests/.lcovrc_l1
# exists and its own comments ask a coverage runner to be pointed at it, so that the settings
# in effect are the ones this repository versions rather than whatever the caller's home
# directory holds.  There is no Tests/L2Tests/.lcovrc_l2 here, so L2 runs without one; that
# is sufficient, because `--rc branch_coverage=1` is what actually enables branch collection.
#
# Reading that file emits lcov "deprecated" WARNINGS for its `lcov_branch_coverage` and
# `geninfo_no_exception_branch` keys.  Those warnings are left visible on purpose: the file
# keeps the legacy spellings deliberately, so the warning is accurate and suppressing it would
# hide a real migration signal.
# ------------------------------------------------------------------------------------
resolve_lcov_config() {
    local level="$1" candidate
    LCOV_CONFIG_ARGS=()
    case "$level" in
        l1) candidate="$SCRIPT_DIR/L1Tests/.lcovrc_l1" ;;
        l2) candidate="$SCRIPT_DIR/L2Tests/.lcovrc_l2" ;;
        *)  die "resolve_lcov_config: unknown level '$level'" ;;
    esac
    if [ -f "$candidate" ]; then
        LCOV_CONFIG_ARGS=(--config-file "$candidate")
        log "using this repository's lcov configuration: ${candidate#"$WS"/}"
    else
        log "no ${level^^} lcov configuration in this repository; relying on the --rc override alone"
    fi
}

# ------------------------------------------------------------------------------------
# CAPTURE, FILTER, REPORT.
#
# The capture directory, the exclusion globs and the genhtml title are the workflow's.  The
# additions are `--rc branch_coverage=1` everywhere and the artifact naming.
#
# On `--rc branch_coverage=1`: this is the AUTHORITATIVE way to enable branch collection.
# lcov collects line and function data by default but NOT branch data, and the legacy
# configuration key `lcov_branch_coverage` is deprecated in lcov 2.x and defaults to zero --
# so no configuration file, including this repository's own .lcovrc_l1, can be relied upon to
# switch it on.  The run-time override wins over ~/.lcovrc, over /etc/lcovrc and over
# --config-file, which is exactly why it is used on every single invocation below.
#
# On `--ignore-errors` for the capture: exactly the nine values
# mismatch,gcov,unused,empty,negative,source,graph,inconsistent,corrupt.  `category` is NOT
# among them and must not be added -- it is not a valid --ignore-errors value in lcov 2.x and
# hard-fails the invocation outright.  Verified, not assumed.
# ------------------------------------------------------------------------------------
capture_coverage() {
    local level="$1"
    local raw="$LEVEL_ARTIFACT_DIR/coverage_$level.info"
    local filtered="$LEVEL_ARTIFACT_DIR/filtered_coverage_$level.info"
    local html="$LEVEL_ARTIFACT_DIR/coverage_$level"
    local -a excludes

    case "$level" in
        l1) excludes=("${L1_EXCLUDES[@]}") ;;
        l2) excludes=("${L2_EXCLUDES[@]}") ;;
        *)  die "capture_coverage: unknown level '$level'" ;;
    esac

    rule
    log "capturing coverage from $LEVEL_BUILD_DIR (branch data forced on)"
    rm -f -- "$raw" "$filtered"
    "$LCOV_BIN" -c \
        -o "$raw" \
        -d "$LEVEL_BUILD_DIR" \
        "${LCOV_CONFIG_ARGS[@]}" \
        --rc branch_coverage=1 \
        --ignore-errors "$LCOV_CAPTURE_IGNORE" \
        || die "lcov capture failed for level ${level^^}; no figure is reported."
    [ -s "$raw" ] || die "lcov produced an empty trace at $raw; nothing was measured."
    log "raw trace: $raw"

    # Branch records must actually be present, or `--rc branch_coverage=1` did not take effect
    # and the branch column would be a silent lie.  This is the check that makes the branch
    # enablement verifiable rather than merely intended.
    if grep -q '^BRDA:' "$raw"; then
        log "branch records present in the raw trace (BRDA), so branch collection is in effect"
    else
        warn "no BRDA records in $raw: branch data was not collected."
        warn "    Line and function figures remain valid; the branch column will read 'no data'."
    fi

    log "filtering with the ${level^^} exclusion globs, reproduced verbatim from the workflow"
    local glob
    for glob in "${excludes[@]}"; do
        log "    $glob"
    done
    # `unused` is in the filter's ignore list because an exclusion glob that matches nothing is
    # an ERROR in lcov 2.x (exit 25), and these globs are the workflow's -- not pruned to
    # whatever this particular tree happens to contain.
    "$LCOV_BIN" -r "$raw" "${excludes[@]}" \
        -o "$filtered" \
        "${LCOV_CONFIG_ARGS[@]}" \
        --rc branch_coverage=1 \
        --ignore-errors "$LCOV_FILTER_IGNORE" \
        || die "lcov filtering failed for level ${level^^}; no figure is reported."
    [ -s "$filtered" ] || die "the filtered trace at $filtered is empty.
       Every source file was excluded, which means the denominator is gone.  Refusing to
       report a coverage figure computed over nothing."
    log "filtered trace: $filtered"

    log "writing the HTML report"
    rm -rf -- "$html"
    "$GENHTML_BIN" \
        -o "$html" \
        -t "$GENHTML_TITLE" \
        "$filtered" \
        "${LCOV_CONFIG_ARGS[@]}" \
        --rc branch_coverage=1 \
        --ignore-errors "$GENHTML_IGNORE" >/dev/null \
        || die "genhtml failed for level ${level^^}."
    [ -f "$html/index.html" ] || die "genhtml did not produce $html/index.html."
    log "HTML report: $html/index.html"

    rule
    log "lcov summary for level ${level^^} (production source only):"
    "$LCOV_BIN" --summary "$filtered" \
        "${LCOV_CONFIG_ARGS[@]}" \
        --rc branch_coverage=1 \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" 2>&1 \
        | grep -E 'source files|lines\.|functions\.|branches\.' \
        || die "lcov --summary produced no coverage lines for $filtered."
}



# ------------------------------------------------------------------------------------
# PER-FILE TABLE: line, function and branch figures, read out of the trace's own records.
#
# `lcov --list` is NOT used, and that is deliberate: in lcov 2.x it emits malformed rates
# above 100%, so it cannot be the source of figures that get quoted in a report.  The trace
# is parsed instead, which is unambiguous:
#     SF:<path>                 starts a record
#     LF:/LH:                   lines found / lines hit          (authoritative totals)
#     BRF:/BRH:                 branches found / branches hit    (authoritative totals)
#     FNL:<index>,<line>[,<end>]        function LEADER
#     FNA:<index>,<count>,<name>        function ALIAS with its execution count
#     FN:<line>,<name> / FNDA:<count>,<name>   the legacy spellings, still handled
#     FNF:/FNH:                 functions found / hit, counted over LEADERS
#
# THE FUNCTION-RECORD TRAP, and why this reports the alias model.
#   The function records carry THREE comma-separated fields in lcov 2.x, and a parser that
#   splits on the first comma and treats what follows as the name mis-reads every one of
#   them -- which is how function denominators get doubled.  So the count is taken from the
#   numeric second field and the NAME IS ALWAYS THE LAST comma-separated field, which also
#   keeps C++ names containing commas (templates, operator overloads) from corrupting the
#   tally.
#   FNF:/FNH: count LEADERS, and several aliases can share one leader, so those records do
#   NOT reconcile with `lcov --summary`: measured on this repository's own L1 trace, leaders
#   report 98/102 (96.1%) while `lcov --summary` reports 106/124 (85.5%).  `lcov --summary`
#   and genhtml use the ALIAS model, so the alias figure is what this table prints as the
#   function column -- the TOTAL row then reconciles EXACTLY with the summary block printed
#   above it.  The leader figures are not hidden: they are listed underneath, labelled, so
#   the difference is visible instead of being a discrepancy someone has to rediscover.
#
# Every number here comes from the trace this run just produced.  Nothing is defaulted and
# there is no path that prints a figure when parsing fails: an empty parse is a hard stop.
# ------------------------------------------------------------------------------------
REPORT_BELOW_TARGETS=''
REPORT_FLOOR_BREACHES=''

per_file_report() {
    local level="$1"
    local filtered="$LEVEL_ARTIFACT_DIR/filtered_coverage_$level.info"
    local exempt_list=' ' floor_list=' '
    local report tab e f
    local -a exempt floors

    case "$level" in
        l1) exempt=("${L1_GATE_EXEMPT[@]+"${L1_GATE_EXEMPT[@]}"}")
            floors=("${L1_COVERAGE_FLOORS[@]+"${L1_COVERAGE_FLOORS[@]}"}") ;;
        l2) exempt=("${L2_GATE_EXEMPT[@]+"${L2_GATE_EXEMPT[@]}"}")
            floors=("${L2_COVERAGE_FLOORS[@]+"${L2_COVERAGE_FLOORS[@]}"}") ;;
        *)  die "per_file_report: unknown level '$level'" ;;
    esac
    for e in ${exempt[@]+"${exempt[@]}"}; do
        exempt_list="$exempt_list$e "
    done
    for f in ${floors[@]+"${floors[@]}"}; do
        floor_list="$floor_list$f "
    done

    tab="$(printf '\t')"
    rule
    report="$(
        awk '
            function flush() {
                if (sf != "")
                    printf "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", \
                           sf, lh, lf, fnh, fnf, fnah, fna, brh, brf, hasbr
                sf = ""
            }
            /^SF:/  { flush()
                      sf = substr($0, 4)
                      lh = 0; lf = 0; fnh = 0; fnf = 0
                      fnah = 0; fna = 0; brh = 0; brf = 0; hasbr = 0
                      next }
            /^LF:/  { lf  = substr($0, 4) + 0; next }
            /^LH:/  { lh  = substr($0, 4) + 0; next }
            /^FNF:/ { fnf = substr($0, 5) + 0; next }
            /^FNH:/ { fnh = substr($0, 5) + 0; next }
            /^BRF:/ { brf = substr($0, 5) + 0; hasbr = 1; next }
            /^BRH:/ { brh = substr($0, 5) + 0; next }
            # FNA:<index>,<execution count>,<name>.  Only the numeric second field is read;
            # the name is the LAST field, so a name containing commas cannot shift the count
            # and the denominator cannot be doubled.
            /^FNA:/ { split(substr($0, 5), fields, ",")
                      fna++
                      if (fields[2] + 0 > 0) fnah++
                      next }
            # Legacy spellings, for a trace produced by a different lcov: FN: declares a
            # function, FNDA: gives its count.  Counted only when no FNA: records were seen
            # for this record, so the two models can never be added together.
            /^FN:/   { legacy_fn[substr($0, 4)] = 1; legacy_seen++; next }
            /^FNDA:/ { split(substr($0, 6), fields, ",")
                       if (fields[1] + 0 > 0) legacy_hit++
                       next }
            /^end_of_record/ {
                      if (fna == 0 && legacy_seen > 0) { fna = legacy_seen; fnah = legacy_hit }
                      flush()
                      delete legacy_fn; legacy_seen = 0; legacy_hit = 0
                      next }
            END     { flush() }
        ' "$filtered" \
        | LC_ALL=C sort -t "$tab" -k1,1 \
        | awk -F'\t' -v min="$COVERAGE_MIN" -v repo="$REPO_NAME" -v exempt="$exempt_list" \
              -v floors="$floor_list" -v target="$ACCEPTANCE_TARGET" -v level="$level" '
            function pct(hit, found) { return found > 0 ? 100 * hit / found : 0 }
            function relpath(p,   marker, at) {
                marker = "/" repo "/"
                at = index(p, marker)
                return at > 0 ? substr(p, at + length(marker)) : p
            }
            # Each metric cell is a fixed width so the columns line up, and a metric with no
            # data reads "no data" rather than a misleading 0.0%.
            function cell(hit, found, have) {
                if (!have || found <= 0)
                    return sprintf("%6s %12s", "n/a", "no data")
                return sprintf("%6.1f%% %5d/%-5d", pct(hit, found), hit, found)
            }
            function floor_for(rel,   n, i, parts, kv) {
                n = split(floors, parts, " ")
                for (i = 1; i <= n; i++) {
                    if (parts[i] == "") continue
                    split(parts[i], kv, "=")
                    if (kv[1] == rel) return kv[2] + 0
                }
                return -1
            }
            BEGIN {
                printf "Per-file coverage (%s), derived from the filtered trace records\n\n", toupper(level)
                printf "%-42s %19s %19s %19s  %s\n", \
                       "FILE", "LINES", "FUNCTIONS", "BRANCHES", "LINE GATE"
                printf "%-42s %19s %19s %19s  %s\n", \
                       "----", "-----", "---------", "--------", "---------"
            }
            {
                sf = $1; lh = $2; lf = $3; fnh = $4; fnf = $5
                fnah = $6; fna = $7; brh = $8; brf = $9; hasbr = $10
                rel = relpath(sf)
                lpct = pct(lh, lf)

                is_exempt = (index(exempt, " " rel " ") > 0)
                if (lf == 0)                        { verdict = "no lines" }
                else if (lpct + 0 >= min + 0)       { verdict = "PASS" }
                else if (is_exempt)                 { verdict = "BELOW (exempt)"
                                                      printf "##EXEMPTBELOW %s %.1f %d %d\n", rel, lpct, lh, lf }
                else                                { verdict = "BELOW"
                                                      printf "##BELOW %s %.1f %d %d\n", rel, lpct, lh, lf }

                if (rel == target)
                    printf "##TARGET %s %.1f %d %d\n", rel, lpct, lh, lf

                fl = floor_for(rel)
                if (fl >= 0) {
                    if (lpct + 0 + 0.05 < fl)
                        printf "##FLOORBREACH %s %.1f %.1f\n", rel, lpct, fl
                    else
                        printf "##FLOOROK %s %.1f %.1f\n", rel, lpct, fl
                }

                printf "%-42s %19s %19s %19s  %s\n", rel, \
                       cell(lh, lf, lf > 0), \
                       cell(fnah, fna, fna > 0), \
                       cell(brh, brf, hasbr), \
                       verdict

                TLH += lh; TLF += lf; TFNH += fnh; TFNF += fnf
                TFNAH += fnah; TFNA += fna; TBRH += brh; TBRF += brf
                files++
                leaders[files] = sprintf("%d/%d", fnh, fnf)
                aliases[files] = sprintf("%d/%d", fnah, fna)
                order[files]   = rel
            }
            END {
                if (files == 0) { print "##NODATA"; exit 0 }
                printf "%-42s %19s %19s %19s  %s\n", \
                       "----", "-----", "---------", "--------", "---------"
                printf "%-42s %19s %19s %19s  %s\n", \
                       sprintf("TOTAL (%d source files)", files), \
                       cell(TLH, TLF, TLF > 0), \
                       cell(TFNAH, TFNA, TFNA > 0), \
                       cell(TBRH, TBRF, TBRF > 0), \
                       (pct(TLH, TLF) + 0 >= min + 0 ? "PASS" : "BELOW")
                printf "##AGGREGATE %.1f %d %d\n", pct(TLH, TLF), TLH, TLF
                print ""
                print "The FUNCTIONS column uses lcov'\''s alias model (FNA records), which is what"
                print "lcov --summary and genhtml report, so the TOTAL row reconciles with the summary"
                print "block above.  The trace also carries per-file leader records (FNL/FNF/FNH) whose"
                print "denominator is smaller because several aliases can share one leader:"
                for (i = 1; i <= files; i++)
                    printf "    %-42s leaders %-11s aliases %s\n", order[i], leaders[i], aliases[i]
            }
        '
    )" || die "per-file report generation failed for level ${level^^}."

    if printf '%s\n' "$report" | grep -q '^##NODATA$'; then
        die "the filtered trace for level ${level^^} yielded no per-file records.
       Refusing to report a coverage figure that was not measured."
    fi

    # Everything not prefixed with ## is the human-readable table.
    printf '%s\n' "$report" | grep -v '^##' || true

    REPORT_BELOW_TARGETS="$(printf '%s\n' "$report" | sed -n 's/^##BELOW //p')"
    REPORT_FLOOR_BREACHES="$(printf '%s\n' "$report" | sed -n 's/^##FLOORBREACH //p')"

    report_branch_note
    report_acceptance_target "$level" "$report"
    report_floors "$level" "$report"
    report_exemptions "$level" "$report"
    report_attribution_guidance "$level"
}

# ------------------------------------------------------------------------------------
# BRANCH COVERAGE IS REPORTED, NOT GATED -- stated in the output as well as in the comments,
# because a reader of the report needs the reason as much as a reader of the script does.
# ------------------------------------------------------------------------------------
report_branch_note() {
    printf '\n'
    rule
    log "Line coverage is the gate (bar: ${COVERAGE_MIN}%).  Branch coverage is REPORTED, NOT GATED:"
    log "    gcov counts branches as control-flow-graph arcs, and those arcs include"
    log "    compiler-generated exception and static-destruction edges that no test can reach, so"
    log "    100% branch coverage is unattainable for most C++ translation units.  A branch"
    log "    threshold would gate the compiler, not the tests.  The branch column is movement"
    log "    evidence for closing if/else paths with negative and corner-case tests."
}

# ------------------------------------------------------------------------------------
# The Directive 4 acceptance target for this submodule, called out so it cannot be lost in
# the table.  The recorded baseline is labelled as such; the current figure is measured.
# ------------------------------------------------------------------------------------
report_acceptance_target() {
    local level="$1" report="$2" line
    line="$(printf '%s\n' "$report" | sed -n 's/^##TARGET //p' | head -1)"
    rule
    if [ -z "$line" ]; then
        warn "the acceptance target $ACCEPTANCE_TARGET does not appear in the ${level^^} trace."
        warn "    It is in the denominator by design, so its absence means the build tree did not"
        warn "    compile it or the capture missed it -- the ${level^^} figure is not a verdict on it."
        return 0
    fi
    local rel pct_value hit found
    read -r rel pct_value hit found <<<"$line"
    log "ACCEPTANCE TARGET (specification section 0.9.2) -- $rel"
    log "    measured now      : ${pct_value}% lines (${hit}/${found})"
    log "    recorded baseline : 73.6% (39/53) lines, 75.0% (3/4) functions, 30.0% (18/60) branches"
    log "    required          : >= ${COVERAGE_MIN}% lines"
    if awk -v v="$pct_value" -v m="$COVERAGE_MIN" 'BEGIN { exit !(v + 0 >= m + 0) }'; then
        log "    verdict           : MEETS THE BAR"
    else
        warn "    verdict           : BELOW THE BAR -- close it by adding tests, never by adding an"
        warn "                        exclusion glob and never by editing production source."
    fi
}

# ------------------------------------------------------------------------------------
# Must-not-regress floors.  A floor breach does not fail the gate on its own -- the gate is
# the >= bar -- but it is surfaced prominently, because a file sliding from 98% to 85% while
# still "passing" is exactly the regression Directive 4's floor language exists to catch.
# A 0.05 percentage-point tolerance absorbs the rounding in the recorded baselines.
# ------------------------------------------------------------------------------------
report_floors() {
    local level="$1" report="$2" ok breaches
    ok="$(printf '%s\n' "$report" | sed -n 's/^##FLOOROK //p')"
    breaches="$(printf '%s\n' "$report" | sed -n 's/^##FLOORBREACH //p')"
    rule
    if [ "$level" != l1 ]; then
        log "must-not-regress floors: none recorded for ${level^^}."
        log "    The baseline figures in the specification were measured under L1, and the two levels"
        log "    reach different code -- HdmiCecSourceImplementation.cpp measures 86.1% under L1 and"
        log "    71.2% under L2 from the same sources.  Asserting an L1 baseline against an ${level^^}"
        log "    trace would report a regression that never happened, so none is asserted here."
        return 0
    fi
    log "must-not-regress floors (recorded L1 baseline percentages, not live measurements):"
    if [ -n "$ok" ]; then
        printf '%s\n' "$ok" | while read -r path now floor; do
            log "    OK       $path  now ${now}%  >= floor ${floor}%"
        done
    fi
    if [ -n "$breaches" ]; then
        printf '%s\n' "$breaches" | while read -r path now floor; do
            warn "    BREACH   $path  now ${now}%  <  floor ${floor}%"
        done
        warn "    A floor is a floor, not a target to descend to: coverage that existed must not be"
        warn "    lost as tests are added elsewhere.  Investigate before accepting this run."
    fi
    if [ -z "$ok" ] && [ -z "$breaches" ]; then
        log "    none of the floored files appear in this level's trace"
    fi
}

# ------------------------------------------------------------------------------------
# Enumerated exemptions.  These files stay in the denominator and keep their real figures;
# only the verdict is waived, and the reason is printed so the traceability report can quote
# it.  Filtering them out instead would be the convenient number the contract forbids.
# ------------------------------------------------------------------------------------
report_exemptions() {
    local level="$1" report="$2" exempt_below
    exempt_below="$(printf '%s\n' "$report" | sed -n 's/^##EXEMPTBELOW //p')"
    [ -n "$exempt_below" ] || return 0
    rule
    log "below the bar and ENUMERATED AS UNREACHABLE AT THIS LEVEL (verdict waived, figures kept,"
    log "still in the denominator -- no exclusion glob was added for these):"
    printf '%s\n' "$exempt_below" | while read -r path pct_value hit found; do
        log "    $path  ${pct_value}% (${hit}/${found} lines)"
        log "        Reason: its instrumented line and its functions are generated by the plugin"
        log "        module-declaration macro, whose build-reference and service-metadata accessors"
        log "        only the Thunder plugin loader invokes at load time.  An in-process ${level^^}"
        log "        GoogleTest binary never loads the plugin through a live host."
        if [ "$level" = l1 ]; then
            log "        Measured at 100% (1/1 lines, 2/2 functions) under L2, which does start a real"
            log "        Thunder host -- so NO production change is required, only an execution model"
            log "        that loads the plugin.  Saying 'uncoverable' unqualified would be false."
        fi
    done
}

# ------------------------------------------------------------------------------------
# How these figures are to be quoted.  This pass edits test files, which shifts line numbers,
# so a line-number citation goes stale the moment it is written.  The gap register keys its
# findings by stable anchor IDs and by section-6.2 rank, and the traceability report is keyed
# the same way; matching it here is what keeps the evidence chain joinable.
# ------------------------------------------------------------------------------------
report_attribution_guidance() {
    local level="$1"
    rule
    log "quoting these figures (COVERAGE_GAPS.md section 6.2 keys, NOT line numbers):"
    log "    Cite a production SYMBOL NAME plus the gap's stable HTML anchor id and its"
    log "    section-6.2 rank.  For this submodule:"
    log "        #gap-plugin-source                  -- section 4a, ranked row 30 (P1, live-data"
    log "                                               methods/events) and row 40 (P2,"
    log "                                               informational getters and config setters)"
    log "        #gap-plugin-source-ondeviceremoved  -- section 4a, ranked row 37 (P2)"
    log "    Do NOT cite line numbers: this pass edits test files and shifts them, so a line"
    log "    citation is stale on arrival.  Symbol names and anchor ids are stable."
    log "    Level measured: ${level^^}.  Trace: $LEVEL_ARTIFACT_DIR/filtered_coverage_$level.info"
}


# ------------------------------------------------------------------------------------
# THE GATE.
#
# `--fail-under-lines` is only accepted ALONGSIDE an operation: the bare
# `lcov --fail-under-lines 80 trace.info` form is rejected by lcov 2.0-1 with
#     lcov: ERROR: invalid command line: Need one of options -z, -c, -a, -e, -r, -l,
#           --diff, --intersect, --subtract, or --summary
# and exits 2.  So the aggregate check is spelled with --summary, which is the same operation
# whose figures were printed above.  Verified both directions, not assumed.
#
# lcov's own non-zero exit IS the verdict.  It is captured only so that the per-target check
# can also run and both failures can be named in one pass -- it is never swallowed, never
# `|| true`, and never reinterpreted: if either check fails, this function exits non-zero and
# the script's exit status is the gate.
#
# TWO checks, because Directive 4 says ">= 80% per target", not ">= 80% on average":
#   1. the level AGGREGATE, via lcov itself; and
#   2. every individual TARGET, from the per-file table, with enumerated exemptions waived.
# A large well-covered file could otherwise carry a badly-covered one over the line.
# ------------------------------------------------------------------------------------
apply_gate() {
    local level="$1"
    local filtered="$LEVEL_ARTIFACT_DIR/filtered_coverage_$level.info"
    local rc=0 failures=0

    rule
    log "applying the >= ${COVERAGE_MIN}% LINE-coverage gate to the ${level^^} aggregate"
    "$LCOV_BIN" --summary "$filtered" \
        --fail-under-lines "$COVERAGE_MIN" \
        "${LCOV_CONFIG_ARGS[@]}" \
        --rc branch_coverage=1 \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" >/dev/null || rc=$?

    if [ "$rc" -ne 0 ]; then
        warn "${level^^} AGGREGATE line coverage is below ${COVERAGE_MIN}% (lcov exited $rc)"
        failures=$((failures + 1))
    else
        log "${level^^} aggregate line coverage meets the ${COVERAGE_MIN}% bar"
    fi

    if [ -n "$REPORT_BELOW_TARGETS" ]; then
        warn "these ${level^^} TARGETS are below ${COVERAGE_MIN}% line coverage:"
        printf '%s\n' "$REPORT_BELOW_TARGETS" | while read -r path pct_value hit found; do
            printf '[run_coverage]     %s  %s%% (%s/%s lines)\n' "$path" "$pct_value" "$hit" "$found" >&2
        done
        failures=$((failures + 1))
    else
        log "every ${level^^} target meets the ${COVERAGE_MIN}% bar (exemptions enumerated above)"
    fi

    if [ "$failures" -ne 0 ]; then
        rule
        warn "LEVEL ${level^^}: COVERAGE GATE FAILED (bar: ${COVERAGE_MIN}% lines)"
        die "close the gap by ADDING TESTS.  Not by adding an exclusion glob, not by editing
       production source, and not by lowering COVERAGE_MIN -- it defaults to 80 because that
       is the required bar, and a run with a different value says so in its own output.
       Artifacts for inspection: $LEVEL_ARTIFACT_DIR"
    fi

    rule
    log "LEVEL ${level^^}: COVERAGE GATE PASSED -- suite green, aggregate and every target at or"
    log "                 above ${COVERAGE_MIN}% lines (bar: ${COVERAGE_MIN}%)"
    if [ -n "$REPORT_FLOOR_BREACHES" ]; then
        warn "...but a must-not-regress floor was breached (listed above).  The gate passed; the"
        warn "   regression is still real and should not be accepted silently."
    fi
}


# ------------------------------------------------------------------------------------
# THE SEQUENCING BANNER.  This is echoed at runtime, not only written in the header comment,
# because the failure it describes is silent: the numbers still look completely credible when
# they belong to the other plugin.  Anyone reading a coverage run's output should see the
# constraint that makes that run trustworthy.
# ------------------------------------------------------------------------------------
print_sequencing_banner() {
    rule
    log "RUN ONE PLUGIN AT A TIME, AND REBUILD THE TEST FRAMEWORK AGAINST IT FIRST."
    log "    entservices-hdmicecsource and entservices-hdmicecsink both build their test cases"
    log "    into an identically named library -- Tests/L1Tests/CMakeLists.txt:19 reads"
    log "    'set(PLUGIN_NAME L1TestsIO)' in BOTH repositories, and both L2 files read"
    log "    'L2TestsIO'.  Building one plugin overwrites the other's test library, and"
    log "    RdkServicesL1Test itself compiles only test_JSON.cpp: this plugin's own cases"
    log "    arrive through that shared library.  So the required order, per plugin, is"
    log "        build the plugin -> rebuild AND reinstall entservices-testframework against it"
    log "                         -> run -> capture -> only then the other plugin"
    log "    Skipping the framework rebuild is exactly where the collision bites.  This run"
    log "    verifies the installed library's symbols below and refuses to continue if they"
    log "    belong to the sink."
}

# ------------------------------------------------------------------------------------
# One level, end to end.  The ORDER of these steps is the whole argument of this script:
#   neutralise ~/.lcovrc  ->  so branch data is not silently suppressed
#   validate the trees    ->  so a wrong or unbuilt tree is named, not measured
#   verify provenance     ->  so the figures cannot belong to the other plugin
#   zero the counters     ->  so the figures belong to THIS run
#   run the suite         ->  and stop here if it is not green
#   verify fresh data     ->  so an unexercised tree cannot yield a figure
#   capture/filter/report ->  the workflow's recipe, plus branch data
#   per-file table        ->  measured figures, targets, floors, exemptions
#   gate                  ->  aggregate AND per target; exit status is the verdict
# ------------------------------------------------------------------------------------
run_level() {
    local level="$1"
    local binary
    binary="$(suite_binary_for_level "$level")"

    rule
    log "================ LEVEL ${level^^} ================"

    # Idempotent: main() already neutralised $HOME/.lcovrc before the first lcov invocation.
    # Repeated here so that a level cannot run against a file that reappeared mid-run.
    neutralise_home_lcovrc
    resolve_level_inputs "$level"
    resolve_lcov_config "$level"

    log "build   : $LEVEL_BUILD_DIR"
    log "install : $LEVEL_INSTALL_DIR"
    log "artifacts: $LEVEL_ARTIFACT_DIR"

    validate_build_dir "$LEVEL_BUILD_DIR" "$level"
    validate_install_dir "$LEVEL_INSTALL_DIR" "$level" "$binary"
    verify_library_provenance "$LEVEL_INSTALL_DIR" "$level"

    setup_runtime_env "$LEVEL_INSTALL_DIR"

    zero_counters "$LEVEL_BUILD_DIR"
    run_suite "$level"
    verify_fresh_counters "$LEVEL_BUILD_DIR" "$level"

    capture_coverage "$level"
    per_file_report "$level"
    apply_gate "$level"
}

# Invoked before each level under `all`, when the caller supplied a hook that switches a
# shared tree between levels.  Never invoked for a single-level run: there the caller has
# already built the tree for the level being measured.
run_level_rebuild_hook() {
    local level="$1"
    [ -n "$LEVEL_REBUILD_CMD" ] || return 0
    rule
    log "switching the tree to ${level^^} via LEVEL_REBUILD_CMD: $LEVEL_REBUILD_CMD $level"
    # Deliberately word-split: LEVEL_REBUILD_CMD is a command line, not a single path.
    # shellcheck disable=SC2086
    $LEVEL_REBUILD_CMD "$level" \
        || die "LEVEL_REBUILD_CMD failed for ${level^^}; not measuring a tree that was not
       switched to this level."
}

# ------------------------------------------------------------------------------------
# `all` must not measure one tree twice and label the second figure L2.  An L1 tree and an L2
# tree are configured differently (different -I/-include/-D blocks, a level-conditional mocks
# library and a different test library), so either the caller supplies separate per-level
# trees or a hook that switches a shared one.  Anything else is refused: reporting an L2
# figure captured from an L1 tree would be a fabricated measurement.
# ------------------------------------------------------------------------------------
check_all_admissible() {
    [ -n "$LEVEL_REBUILD_CMD" ] && return 0
    if [ "$L1_BUILD_DIR" = "$L2_BUILD_DIR" ] || [ "$L1_INSTALL_DIR" = "$L2_INSTALL_DIR" ]; then
        die "'all' would measure the same tree twice and call the second figure L2.
       An L1 tree and an L2 tree are not interchangeable: they are configured with different
       include/define blocks, a level-conditional mocks library and a different test library.
       Supply EITHER separate trees:
           L1_BUILD_DIR=... L1_INSTALL_DIR=... L2_BUILD_DIR=... L2_INSTALL_DIR=... $(basename -- "$SCRIPT_PATH") all
       OR a hook that switches a shared tree between levels:
           LEVEL_REBUILD_CMD='/path/to/switch-level.sh' $(basename -- "$SCRIPT_PATH") all
       OR run one level at a time, building in between:
           $(basename -- "$SCRIPT_PATH") l1   # then rebuild for L2, then:
           $(basename -- "$SCRIPT_PATH") l2"
    fi
    return 0
}

print_artifact_summary() {
    local levels="$1" level
    rule
    log "artifacts written (disposable build output -- NOT part of the repository, never commit):"
    for level in $levels; do
        log "    $ARTIFACT_ROOT/$REPO_NAME/$level/"
        log "        coverage_$level.info            raw trace"
        log "        filtered_coverage_$level.info   production-source-only trace (the one to quote)"
        log "        coverage_$level/index.html      HTML report"
    done
    log "    remove them with:  rm -rf '$ARTIFACT_ROOT'"
}

main() {
    case "${1:-}" in
        -h|--help)     usage; exit 0 ;;
        --help-build)  print_build_recipe; exit 0 ;;
    esac

    if [ "$#" -gt 1 ]; then
        printf 'ERROR: expected at most one level argument, got %d.\n\n' "$#" >&2
        usage >&2
        exit 2
    fi

    local levels level
    if ! levels="$(parse_level "${1:-all}")"; then
        printf "ERROR: unknown level '%s'.  Expected l1, l2 or all.\n\n" "${1:-}" >&2
        usage >&2
        exit 2
    fi

    # $HOME/.lcovrc goes FIRST, before ANY lcov invocation -- including the version echo in
    # print_configuration and the capability probe in preflight.  This ordering is not
    # cosmetic and it is not only about branch data: a hostile or merely stale file there
    # breaks lcov OUTRIGHT.  Setting both the legacy `lcov_branch_coverage` and
    # `genhtml_branch_coverage` keys, for instance, makes lcov 2.0-1 fail EVERY invocation with
    # "ERROR: unexpected ARRAY for branch_coverage value" and exit 255 -- `lcov --version` and
    # `lcov --help` included.  Probing lcov's capabilities before neutralising the file would
    # therefore misdiagnose a working lcov as an unsupported one.
    neutralise_home_lcovrc

    print_configuration "$levels"
    preflight
    print_sequencing_banner

    case "$levels" in
        'l1 l2') check_all_admissible ;;
    esac

    for level in $levels; do
        case "$levels" in
            'l1 l2') run_level_rebuild_hook "$level" ;;
        esac
        run_level "$level" || die "level ${level^^} failed; the remaining levels were not run."
    done

    print_artifact_summary "$levels"
    rule
    log "DONE.  Levels measured: $(printf '%s' "$levels" | tr '[:lower:]' '[:upper:]').  Suites green and the"
    log "       >= ${COVERAGE_MIN}% line-coverage gate passed for every one of them."
    log "       Branch figures are reported as evidence and are deliberately not gated."
}

main "$@"

