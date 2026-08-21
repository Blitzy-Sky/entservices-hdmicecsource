"""
/**
 * @file TCID15_Get_OSD_Name.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID15_Get_OSD_Name
 * @details Calls org.rdk.HdmiCecSource.getOSDName and compares the whole envelope against
 *          {"jsonrpc":"2.0","id":42,"result":{"name":"Sky TV","success":true}}. The reply is
 *          compared as a WHOLE ENVELOPE, so the jsonrpc member and the request id 42 are part
 *          of the contract and not only the result body.
 *
 *          "Sky TV" is NOT the plugin default - the provisioned static OSD name is "TV Box" -
 *          so this readback depends on position 10 having written the name earlier in the same
 *          suite execution, or on the target's persisted cecOSDName already holding it. Run
 *          under a name filter without position 10, the case correctly fails instead of
 *          relaxing its expectation.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - Position 10 has run in the same suite execution, or the persisted cecOSDName is "Sky TV".
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
 *  - The reply parses as JSON and equals the envelope above exactly.
 *
 * @pass_criteria
 *  - The response is non-empty, the parsed reply equals that envelope exactly, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Empty response, a name other than the one position 10 wrote, any other envelope
 *    difference, a JSONDecodeError, or run_test() returns False.
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
        "name": "Sky TV",
        "success": True
        }
    }

    log_info("Executing the curl command get OSD Name")

    curl_response = send_curl_command(
        HdmiCecSourceApis.get_osd_name
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID15_Get_OSD_Name Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID15_Get_OSD_Name Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID15_Get_OSD_Name Failed ❌")
        return False
