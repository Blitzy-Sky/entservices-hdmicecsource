"""
/**
 * @file SuitManager.py
 * @brief SuitManager.py
 *
 * @testcase SuitManager
 * @details Orchestrates the HDMI CEC Source L3/device-level test suite by dynamically loading
 *          and executing test case modules, activating the required RDK plugin via JSON-RPC,
 *          and reporting per-test pass/fail results with summary statistics. The suite is
 *          authored here and its runtime execution is deferred to a device or emulator
 *          environment; nothing in this repository runs it.
 *
 *          This module is the command-line entry point of the suite, and the name on disk is
 *          the name to invoke: python3 SuitManager.py [-t] hdmicecsource. The spelling is the
 *          established repository convention, shared with the HDMI CEC Sink suite's entry
 *          point so the two device-level suites stay symmetric, and it must not be
 *          "corrected" - a case-sensitive filesystem rejects any other spelling.
 *
 *          This module never starts, emulates or stubs the services the cases talk to. It
 *          activates the plugin through Controller.1.activate, runs the suite's initialization
 *          module once, then calls each case's run_test() with stdout captured so a case's own
 *          logging is replayed under its result.
 *
 * @precondition
 *  - WPEFramework is running and reachable at the configured JSON-RPC endpoint.
 *  - The org.rdk.HdmiCecSource plugin is available for activation. Activation is on by default
 *    and can be suppressed with AUTO_ACTIVATE_PLUGINS=0.
 *  - All test case modules listed in SUITES are present under the Testcases/ directory.
 *
 * @dependencies
 *  - utils.py - the only module this one imports: the JSON-RPC dispatcher, the resolved
 *    endpoint URL and the logging helpers
 *  - Init_Devicelist_Populate.py - loaded by name through SUITE_INIT_MODULES and run once
 *    before the first case; a False return aborts the suite
 *  - Testcases/TCID*.py - the 33 registered case modules, imported by name from the tests list
 *  - HdmiCECSource_Curl.py is a dependency of those cases rather than of this module, which
 *    composes its own controller call through utils.py and builds no CEC request
 *
 * @expected_result
 *  - All registered test cases are executed in order and results are logged.
 *
 * @pass_criteria
 *  - Each test case module's run_test() returns True and is reported as PASSED.
 *
 * @failure_criteria
 *  - Any test case returns False, raises an exception, or the plugin fails to activate.
 */
"""

import importlib
import io
import sys
import time
from pathlib import Path
import os

from utils import log_error, log_info, log_success, send_jsonrpc_command, WPEFRAMEWORK_JSONRPC_URL


BASE_DIR = Path(__file__).resolve().parent
SUITES = {
    "hdmicecsource": {
        "banner": "******************** L3 SUITE - RDK - HDMI CEC SOURCE ****************************",
        "module_dir": BASE_DIR / "Testcases",
        "tests": [
            "TCID01_Send_Standby_Message",
            "TCID02_Get_Devicelist",
            "TCID03_Get_Enabled_Status",
            "TCID04_Get_OTP_Enabled",
            "TCID05_Get_Vendor_ID",
            "TCID06_Perform_OTP_Action",
            "TCID07_Send_Keypress_Event",
            "TCID08_Set_Enabled_False",
            "TCID09_Set_Enabled_True",
            "TCID10_Set_OTP_Name",
            "TCID11_Set_OTP_Enabled_False",
            "TCID12_Set_OTP_Enabled_True",
            "TCID13_Set_Vendor_ID",
            "TCID14_Verify_Vendor_ID_Readback",
            "TCID15_Get_OSD_Name",
            "TCID16_OTP_After_Powerstatus_Reporting",
            "TCID17_Menu_Language_CECversion_Flow",
            "TCID18_Active_Source_Routing_View_ON_Flow",
            "TCID19_Standby_OTP_Powerstatus_Flow",
            "TCID20_Standby_OTP_Userdef_CEC",
            "TCID21_Add_Remove_Device_Vcomponent",
            "TCID22_Verify_Devicelist_CEC_Disabled",
            "TCID23_Disable_CEC_Verify_Enabled",
            "TCID24_Standby_Userdef_Busstatus",
            "TCID25_Add_Network_Verify_Discovery",
            "TCID26_Standby_Then_OTP_Wakeup",
            "TCID27_Active_Source_Transition_After_OTP",
            "TCID28_Invalid_VendorID_Nochange",
            "TCID29_Repeated_Disable_Idempotent",
            "TCID30_Repeated_Enable_Idempotent",
            "TCID31_Invalid_OTP_Setnochange",
            "TCID32_Invalid_OSD_Setnochange",
            "TCID33_Process_Yaml_Health_Check",
        ],
    },
}

# Maps test suite names to their corresponding RDK plugin callsigns for activation
SUITE_PLUGIN_CALLSIGNS = {
    "hdmicecsource": "org.rdk.HdmiCecSource",
}

SUITE_INIT_MODULES = {
    "hdmicecsource": "Init_Devicelist_Populate",
}


def normalize_suite_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def load_test_cases(suite_name):
    suite_config = SUITES[suite_name]
    module_dir = str(suite_config["module_dir"])

    if module_dir not in sys.path:
        sys.path.insert(0, module_dir)

    test_cases = []
    for module_name in suite_config["tests"]:
        module = importlib.import_module(module_name)
        test_cases.append((module_name, module.run_test))

    return suite_config["banner"], test_cases


def activate_plugin_via_curl(callsign):
    response = send_jsonrpc_command(
        "Controller.1.activate",
        params={"callsign": callsign},
        request_id=1234567890,
    )
    if not response:
        return False
    if "error" in response:
        return False
    return "result" in response


def run_suite_init(suite_name):
    module_name = SUITE_INIT_MODULES.get(suite_name)
    if not module_name:
        return True

    if BASE_DIR.as_posix() not in sys.path:
        sys.path.insert(0, str(BASE_DIR))

    try:
        module = importlib.import_module(module_name)
    except Exception as exc:
        log_error(f"Init module import failed: {module_name} ({exc})")
        return False

    run_fn = getattr(module, "run_test", None)
    if not callable(run_fn):
        log_error(f"Init module missing run_test(): {module_name}")
        return False

    log_info(f"Running suite initialization: {module_name}.run_test()")
    try:
        ok = bool(run_fn())
    except Exception as exc:
        log_error(f"Suite initialization threw exception: {exc}")
        return False

    if ok:
        log_success("Suite initialization completed successfully")
    else:
        log_error("Suite initialization failed")
    return ok


def run_suite(suite_name):
    banner, test_cases = load_test_cases(suite_name)
    print(banner)

    auto_activate = os.environ.get("AUTO_ACTIVATE_PLUGINS", "1").lower() not in ("0", "false", "no")
    callsign = SUITE_PLUGIN_CALLSIGNS.get(suite_name)
    if auto_activate and callsign:
        log_info(f"Auto-activating plugin '{callsign}' via curl JSON-RPC at {WPEFRAMEWORK_JSONRPC_URL}")
        if activate_plugin_via_curl(callsign):
            log_success(f"Plugin activated: {callsign}")
            log_info("Waiting 6s for plugin to fully initialise...")
            time.sleep(6)
        else:
            log_error(f"Plugin activation failed: {callsign}")
            log_error("Check JSON-RPC endpoint reachability and plugin availability before running tests.")
            return False

    if not run_suite_init(suite_name):
        log_error("Aborting suite because initialization did not complete successfully.")
        return False

    passed = 0
    failed = 0
    failed_cases = []
    original_stdout = sys.stdout

    for tc_name, tc_fn in test_cases:
        log_info(f"\n{'='*60}")
        log_info(f"Running: {tc_name}")
        log_info(f"{'='*60}")
        captured = io.StringIO()
        sys.stdout = captured
        try:
            result = tc_fn()
        except Exception as exc:
            result = False
            print(f"EXCEPTION in {tc_name}: {exc}")
        finally:
            sys.stdout = original_stdout

        output = captured.getvalue()
        print(output, end="")

        if result:
            passed += 1
            log_success(f"[PASS] {tc_name}")
        else:
            failed += 1
            failed_cases.append(tc_name)
            log_error(f"[FAIL] {tc_name}")

        time.sleep(1)

    log_info(f"\n{'='*60}")
    log_info(f"Suite Summary: {passed} passed, {failed} failed")
    if failed_cases:
        log_error(f"Failed cases: {failed_cases}")
    log_info(f"{'='*60}")
    return failed == 0


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Run HDMI CEC test suites")
    parser.add_argument("suite", help=f"Test suite name. Available: {list(SUITES.keys())}")
    parser.add_argument("-t", "--timing", action="store_true", help="Enable timing output for passed test cases")
    
    args = parser.parse_args()
    
    # Set environment variable for timing mode
    if args.timing:
        os.environ["HDMICEC_TIMING_ENABLED"] = "1"
    
    suite_arg = normalize_suite_name(args.suite)
    matching = [k for k in SUITES if normalize_suite_name(k) == suite_arg]
    if not matching:
        log_error(f"Unknown suite '{args.suite}'. Available: {list(SUITES.keys())}")
        sys.exit(1)

    ok = run_suite(matching[0])
    sys.exit(0 if ok else 1)
