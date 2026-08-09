"""
/**
 * @file TCID30_Repeated_Enable_Idempotent.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID30_Repeated_Enable_Idempotent
 * @details Sends setEnabled(enabled=true) twice, reading getEnabled after each write, and
 *          requires the second read to report result.enabled exactly True - the idempotence
 *          claim for the true operand.
 *
 *          Nothing is restored, and nothing needs to be: enabled is the state the rest of the
 *          suite expects, and it is the state position 29's own restore leaves behind. The
 *          first read's value is logged only, and neither write reply is inspected.
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
 *  - After two enables the reader reports result.enabled true.
 *
 * @pass_criteria
 *  - The second getEnabled returns a non-empty response reporting enabled True, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - The second read returns nothing, enabled is not exactly True, or parsing raises.
 */
"""


import time
import os


import json
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: getEnabled when already enabled.
    send_curl_command(HdmiCecSourceApis.set_enabled_true)
    first_get = send_curl_command(HdmiCecSourceApis.get_enabled)
    send_curl_command(HdmiCecSourceApis.set_enabled_true)
    second_get = send_curl_command(HdmiCecSourceApis.get_enabled)

    if not second_get:
        log_error("✖ getEnabled command not sent")
        return False

    log_warning(f"Final enabled response: {second_get}")
    try:
        body = json.loads(second_get)
        enabled = body.get("result", {}).get("enabled")
        if enabled is True:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID30_Repeated_Enable_Idempotent Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID30_Repeated_Enable_Idempotent Failed")
    return False
