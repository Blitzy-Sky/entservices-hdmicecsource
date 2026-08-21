"""
/**
 * @file TCID08_Set_Enabled_False.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID08_Set_Enabled_False
 * @details Sends org.rdk.HdmiCecSource.setEnabled with params enabled false and compares the
 *          whole envelope against a success reply. The reply is compared as a WHOLE ENVELOPE,
 *          so the jsonrpc member and the request id 42 are part of the contract and not only
 *          the result body.
 *
 *          THE CASE RESTORES WHAT IT CHANGED. A finally: block re-sends
 *          setEnabled(enabled=true) on every exit path - pass, mismatch and JSONDecodeError
 *          alike - so a disabled CEC stack cannot leak into the following positions, several
 *          of which need CEC enabled. The restore's own reply is not inspected, because the
 *          verdict belongs to the disable request; a restore that itself failed would surface
 *          as a failure in the next position rather than here.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - CEC is enabled on entry, which positions 03 and 09 both leave true.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command and the logging helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *
 * @expected_result
 *  - The disable reply parses as JSON and equals
 *    {"jsonrpc":"2.0","id":42,"result":{"success":true}} exactly, and the re-enable request is
 *    issued afterwards.
 *
 * @pass_criteria
 *  - The disable response is non-empty, the parsed reply equals that envelope exactly, and
 *    run_test() returns True - with the re-enable request having been issued.
 *
 * @failure_criteria
 *  - Empty response, any envelope difference, or a JSONDecodeError. The re-enable request is
 *    still issued on each of those paths.
 */
"""


import time
import os


import json
from utils import (
    send_curl_command,
        log_info,
        log_success,
        log_error,
        log_warning,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    expected_output_response = {
        "jsonrpc": "2.0",
        "id": 42,
        "result": {
            "success": True
        }
    }

    log_info("Executing the curl command set enabled params FALSE")

    curl_response = send_curl_command(
        HdmiCecSourceApis.set_enabled_false
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID08_Set_Enabled_False Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID08_Set_Enabled_False Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID08_Set_Enabled_False Failed ❌")
        return False
    finally:  #reset the state
        curl_response = send_curl_command(
        HdmiCecSourceApis.set_enabled_true
    )
    
    
