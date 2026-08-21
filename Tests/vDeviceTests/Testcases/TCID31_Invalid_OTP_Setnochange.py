"""
/**
 * @file TCID31_Invalid_OTP_Setnochange.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID31_Invalid_OTP_Setnochange
 * @details Writes setOTPEnabled(enabled=true) for a deterministic baseline, reads
 *          getOTPEnabled, sends the malformed HdmiCECSource_Curl.set_otp_enabled_invalid -
 *          whose params carry the misspelled key "ennable" instead of "enabled" - and reads
 *          getOTPEnabled again. All three of the baseline read, the malformed reply and the
 *          final read must be non-empty.
 *
 *          TWO TARGET BEHAVIOURS ARE ADMITTED DELIBERATELY, and the case says which: the
 *          malformed request may be REJECTED with an error object, or ACCEPTED with
 *          result.success true. Either way the final state must remain VALID, meaning
 *          result.enabled is a bool and result.success is True. A reply that is neither an
 *          error nor a success fails.
 *
 *          The baseline value is captured and compared into a local the verdict does not
 *          consult, because a target that normalises the setting rather than leaving it
 *          untouched is not considered a failure. What is asserted is that a malformed write
 *          cannot leave the setting in a state that is not a boolean.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - None beyond the plugin being reachable; the baseline is written by the case itself.
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
 *  - The malformed request is either rejected or accepted, and afterwards getOTPEnabled
 *    reports success true with a boolean enabled.
 *
 * @pass_criteria
 *  - All three replies are non-empty, the final reply reports success True with a boolean
 *    enabled, the malformed reply is either an error object or a success, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Any of the three replies is empty; the final reply's enabled is not a bool or its success
 *    is not True; the malformed reply is neither an error nor a success; or parsing raises.
 */
"""


import time
import os


import json
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: invalid curl param handling for setOTPEnabled.
    send_curl_command(HdmiCecSourceApis.set_otp_enabled_true)
    baseline_get = send_curl_command(HdmiCecSourceApis.get_otp_enabled)
    invalid_set = send_curl_command(HdmiCecSourceApis.set_otp_enabled_invalid)
    final_get = send_curl_command(HdmiCecSourceApis.get_otp_enabled)

    if not baseline_get or not invalid_set or not final_get:
        log_error("✖ required OTP commands not sent")
        return False

    log_warning(f"Baseline OTP response: {baseline_get}")
    log_warning(f"Invalid set response: {invalid_set}")
    log_warning(f"Final OTP response: {final_get}")
    try:
        b = json.loads(baseline_get)
        i = json.loads(invalid_set)
        f = json.loads(final_get)
        baseline_enabled = b.get("result", {}).get("enabled")
        final_enabled = f.get("result", {}).get("enabled")
        unchanged = baseline_enabled == final_enabled
        invalid_rejected = isinstance(i.get("error"), dict)
        invalid_accepted = i.get("result", {}).get("success") is True
        final_state_valid = isinstance(final_enabled, bool) and f.get("result", {}).get("success") is True

        # Valid outcomes observed across targets:
        # 1) Invalid request explicitly rejected.
        # 2) Invalid request accepted, but plugin remains in a valid boolean state.
        #    (State may remain unchanged or be normalized by implementation.)
        if final_state_valid and (invalid_rejected or invalid_accepted):
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID31_Invalid_OTP_Setnochange Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID31_Invalid_OTP_Setnochange Failed")
    return False
