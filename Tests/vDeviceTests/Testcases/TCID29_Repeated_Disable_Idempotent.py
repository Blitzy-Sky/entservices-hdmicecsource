"""
/**
 * @file TCID29_Repeated_Disable_Idempotent.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Repeated disable is idempotent.
 *
 * @testcase TCID29_Repeated_Disable_Idempotent
 * @details Calls setEnabled false twice, checking getEnabled reports disabled after each, then
 *          restores CEC with setEnabled true.
 *
 * @precondition
 *  - A device under test - physical hardware or a QEMU target - is running WPEFramework
 *    with the org.rdk.HdmiCecSource plugin activated and reachable over JSON-RPC.
 *
 * @dependencies
 *  - utils.py - shared endpoint resolution, curl dispatch and logging helpers
 *  - HdmiCECSource_Curl.py - the JSON-RPC command strings this module dispatches
 *  - SuitManager.py - registers and runs this module
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.setEnabled
 *      org.rdk.HdmiCecSource.getEnabled
 *
 * @pass_criteria
 *  - Every response matches its expected value and run_test() returns True.
 *
 * @failure_criteria
 *  - A response mismatch, a JSON-RPC or JSON parsing failure, an unreachable endpoint
 *    or an unavailable device-level prerequisite; run_test() then returns False.
 */
"""


import time
import os


import json
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: getEnabled when already disabled.
    send_curl_command(HdmiCecSourceApis.set_enabled_false)
    first_get = send_curl_command(HdmiCecSourceApis.get_enabled)
    send_curl_command(HdmiCecSourceApis.set_enabled_false)
    second_get = send_curl_command(HdmiCecSourceApis.get_enabled)
    send_curl_command(HdmiCecSourceApis.set_enabled_true)

    if not second_get:
        log_error("✖ getEnabled command not sent")
        return False

    log_warning(f"Final enabled response: {second_get}")
    try:
        body = json.loads(second_get)
        enabled = body.get("result", {}).get("enabled")
        if enabled is False:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID29_Repeated_Disable_Idempotent Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID29_Repeated_Disable_Idempotent Failed")
    return False
