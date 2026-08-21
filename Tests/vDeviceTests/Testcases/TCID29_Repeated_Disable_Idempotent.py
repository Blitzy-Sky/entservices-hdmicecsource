"""
/**
 * @file TCID29_Repeated_Disable_Idempotent.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID29_Repeated_Disable_Idempotent
 * @details Sends setEnabled(enabled=false) twice, reading getEnabled after each write, and
 *          then RESTORES the state with setEnabled(enabled=true) before the verdict is
 *          computed - so the disabled stack cannot leak into the positions that follow even
 *          when the case fails.
 *
 *          THE VERDICT IS THE SECOND READ ALONE: result.enabled must be exactly False. That is
 *          the idempotence claim - the second disable neither toggled the setting back nor
 *          left the reader reporting something other than false. The first read's value is
 *          logged only, and none of the three write replies is inspected.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - None beyond the plugin being reachable; the case is valid from either CEC state.
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
 *  - After two disables the reader reports result.enabled false, and CEC is re-enabled before
 *    the case returns.
 *
 * @pass_criteria
 *  - The second getEnabled returns a non-empty response reporting enabled False, and
 *    run_test() returns True.
 *
 * @failure_criteria
 *  - The second read returns nothing, enabled is not exactly False, or parsing raises. The
 *    re-enable request has been issued on every one of those paths.
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
