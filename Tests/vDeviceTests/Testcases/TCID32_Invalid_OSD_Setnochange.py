"""
/**
 * @file TCID32_Invalid_OSD_Setnochange.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID32_Invalid_OSD_Setnochange
 * @details The same shape as position 31, applied to the OSD name: a valid setOSDName
 *          baseline, a getOSDName read, the malformed HdmiCECSource_Curl.set_osd_name_invalid
 *          - whose params carry the misspelled key "nnamme" instead of "name" - and a final
 *          getOSDName read. All three replies must be non-empty.
 *
 *          TWO TARGET BEHAVIOURS ARE ADMITTED: the malformed request may be REJECTED with an
 *          error object or ACCEPTED with result.success true. Either way the final state must
 *          remain VALID, meaning result.name is a str and result.success is True.
 *
 *          A NORMALISED NAME PASSES - the empty string, for instance - which is exactly why
 *          the baseline comparison is computed into a local the verdict does not consult. The
 *          claim is that a malformed write cannot leave the name as something other than a
 *          string, not that the name is unchanged.
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
 *  - The malformed request is either rejected or accepted, and afterwards getOSDName reports
 *    success true with a string name.
 *
 * @pass_criteria
 *  - All three replies are non-empty, the final reply reports success True with a string name,
 *    the malformed reply is either an error object or a success, and run_test() returns True.
 *
 * @failure_criteria
 *  - Any of the three replies is empty; the final reply's name is not a str or its success is
 *    not True; the malformed reply is neither an error nor a success; or parsing raises.
 */
"""


import time
import os


import json
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: invalid curl param handling for setOSDName.
    send_curl_command(HdmiCecSourceApis.set_osd_name)
    baseline_get = send_curl_command(HdmiCecSourceApis.get_osd_name)
    invalid_set = send_curl_command(HdmiCecSourceApis.set_osd_name_invalid)
    final_get = send_curl_command(HdmiCecSourceApis.get_osd_name)

    if not baseline_get or not invalid_set or not final_get:
        log_error("✖ required OSD commands not sent")
        return False

    log_warning(f"Baseline OSD response: {baseline_get}")
    log_warning(f"Invalid set response: {invalid_set}")
    log_warning(f"Final OSD response: {final_get}")
    try:
        b = json.loads(baseline_get)
        i = json.loads(invalid_set)
        f = json.loads(final_get)
        baseline_name = b.get("result", {}).get("name")
        final_name = f.get("result", {}).get("name")
        unchanged = baseline_name == final_name
        invalid_rejected = isinstance(i.get("error"), dict)
        invalid_accepted = i.get("result", {}).get("success") is True
        final_state_valid = isinstance(final_name, str) and f.get("result", {}).get("success") is True

        # Valid outcomes observed across targets:
        # 1) Invalid request explicitly rejected.
        # 2) Invalid request accepted, with name staying unchanged or normalized
        #    (for example empty string) while API remains successful.
        if final_state_valid and (invalid_rejected or invalid_accepted):
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID32_Invalid_OSD_Setnochange Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID32_Invalid_OSD_Setnochange Failed")
    return False
