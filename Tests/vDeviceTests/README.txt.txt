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
and no result from it is reported anywhere. The suite itself pre-dates the current test-coverage
pass, which changed nothing in it but the name of the entry-point script referenced in this file
and in the test-case docstrings: the module on disk is SuitManager.py, while this README and all
33 docstrings previously said suiteManager.py, so the documented command failed on a
case-sensitive filesystem. No test logic was altered, and no case was run.

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

Prerequisites that were not available, and so were not used:
- A QEMU target.
- A WPEFramework JSON-RPC endpoint on port 9998.
- A vComponent API on port 8080.
- The Python RAFT packages (python_raft, ut-raft), which are deliberately not installed.

No service was started on port 9998 or on port 8080, no QEMU target was launched, and no
transport was stubbed in order to produce a result. Nothing here was simulated or faked to
force a pass. Runtime validation of this suite is deferred until a proper device or emulator
environment is available.
