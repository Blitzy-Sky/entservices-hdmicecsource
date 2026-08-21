To execute the cases inside qemu

cd /tmp

git clone git@github.com:rdkcentral/entservices-hdmicecsource.git

cd entservices-hdmicecsource/Tests/vDeviceTests

EXECUTION:
with time : python3 SuitManager.py -t hdmicecsource
without time: python3 SuitManager.py hdmicecsource

Default Actions:
Plugin activation is enabled by default and runs before suite execution:
- hdmicecsource -> Controller.1.activate(callsign=org.rdk.HdmiCecSource)
- Init_Devicelist_Populate runs once, before the first test case.

Disable default activation only if needed:
- export AUTO_ACTIVATE_PLUGINS=0


If the testcases fail with "connection refused", configure endpoint host/ports before running.

Defaults used by the tests:
- MW JSON-RPC: http://127.0.0.1:9998/jsonrpc
- vComponent API: http://127.0.0.1:8080/api/postKVP

Useful overrides:
- TARGET_HOST (applies to both endpoints)
- JSONRPC_PORT
- VCOMPONENT_PORT
- WPEFRAMEWORK_JSONRPC_URL (full URL, highest priority)
- VCOMPONENT_API_URL (full URL, highest priority)

Examples:

# when running directly inside QEMU guest (services on localhost)
python3 SuitManager.py hdmicecsource

# when running from host against QEMU target IP
export TARGET_HOST=192.168.1.50
export JSONRPC_PORT=9998
export VCOMPONENT_PORT=8080
python3 SuitManager.py hdmicecsource

# full URL override form
export WPEFRAMEWORK_JSONRPC_URL=http://192.168.1.50:9998/jsonrpc
export VCOMPONENT_API_URL=http://192.168.1.50:8080/api/postKVP
python3 SuitManager.py hdmicecsource


Troubleshooting:
- If you see connection errors, verify WPEFramework JSON-RPC and the vComponent API are reachable using the endpoint overrides above.


Status:
NOT EXECUTED. Runtime validation is deferred.

Everything above this line is an instruction for someone who HAS a device; it is not a record
of a run. This suite has not been executed - on a device, on an emulator, or anywhere else -
and no result from it is reported anywhere.

The entry point is SuitManager.py, spelled exactly that way. This file and all 33 test-case
docstrings reference that spelling, and they have to: a case-sensitive filesystem rejects any
other, so a command written with a different case fails before anything runs. The HDMI CEC Sink
suite uses the same filename for the same reason, which keeps the two device-level suites
symmetric.

No test logic in this suite has been altered. Everything changed here is documentation - this
file, the module docstrings and the 33 case docstrings - and each case's executable body is
exactly what it was.

EVERY reference to the entry point in this directory now uses that spelling. Grepping this
directory for the old lower-case-initial form returns nothing at all, which is the check to run
rather than a claim to take on trust. Two references were missed on the first pass and are now
corrected, both in Init_Devicelist_Populate.py: its @dependencies block, and a comment further
down the same file that named the module in prose. Both were documentation-only - nothing imports
a module by the wrong spelling, so no execution path ever depended on them - but leaving them
would have meant this directory disagreeing with itself about the one command a reader has to
type. The earlier decision to report them rather than correct them rested on a scope argument
that does not hold: that file is a test file inside Tests/vDeviceTests, which is in scope, and the
correction is a docstring and a comment with no test logic in it.

The device under test is the source: a set-top box, which takes a CEC playback/tuner logical
address beneath the television. The virtual CEC peers this suite configures sit around it. The
device under test keeps its source role throughout; it is never reconfigured to stand in for a
peer of its own.

Static validation applied to the suite:
- python3 -m py_compile over all 4 modules in this directory and all 33 modules under
  Testcases/ - all compile.
- A suite-manager registration check of the tests registered in SuitManager.py against the
  test-case modules on disk under Testcases/, applied in BOTH directions, so that neither a
  registered module missing from disk nor an unregistered module on disk goes unnoticed: 33
  registered, 33 on disk, no discrepancy either way.
- YAML well-formedness parsing of every document under vcomponent_configurations/: 79
  documents, none malformed.
- A docstring-header check over all 33 case modules: each carries the nine documentation tags
  this suite uses (@file, @brief, @testcase, @details, @precondition, @dependencies,
  @expected_result, @pass_criteria, @failure_criteria), each @file and @testcase matches the
  module's own filename, and each module exposes a run_test() at module level.

Prerequisites that were not available, and so were not used:
- A QEMU target.
- A WPEFramework JSON-RPC endpoint on port 9998.
- A vComponent API on port 8080.
- The Python RAFT packages (python_raft, ut-raft), which are deliberately not installed.

No service was started on port 9998 or on port 8080, no QEMU target was launched, and no
transport was stubbed in order to produce a result. Nothing here was simulated or faked to
force a pass. Runtime validation of this suite is deferred until a proper device or emulator
environment is available.
