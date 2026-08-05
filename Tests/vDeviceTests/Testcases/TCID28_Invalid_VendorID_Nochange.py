"""
/**
 * @file TCID28_Invalid_VendorID_Nochange.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID28_Invalid_VendorID_Nochange
 * @details Validates the 'TCID28_Invalid_VendorID_Nochange' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
 *
 * @precondition
 *  - Required plugin is active and reachable via JSON-RPC endpoint.
 *  - Target environment is ready for HDMI CEC emulation/command execution.
 *
 * @dependencies
 *  - utils.py
 *  - HdmiCECSource_Curl.py
 *  - SuitManager.py
 *  - vcomponent_configurations/hdmicec/commands/*.yaml (for emulation-based scenarios)
 *
 * @expected_result
 *  - API responses and scenario validations match expected values.
 *
 * @pass_criteria
 *  - Expected response equals actual response and testcase returns True.
 *
 * @failure_criteria
 *  - Response mismatch, command failure, JSON parsing error, or testcase returns False.
 */
"""


import time
import os


import json
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: invalid curl param handling for setVendorId.
    baseline_set = send_curl_command(HdmiCecSourceApis.set_vendor_id)
    baseline_get = send_curl_command(HdmiCecSourceApis.get_vendor_id)
    invalid_set = send_curl_command(HdmiCecSourceApis.set_vendor_id_invalid)
    final_get = send_curl_command(HdmiCecSourceApis.get_vendor_id)

    if not final_get:
        log_error("✖ getVendorId command not sent")
        return False

    log_warning(f"Final vendor response: {final_get}")
    try:
        b = json.loads(baseline_get)
        f = json.loads(final_get)
        if "result" in b and "result" in f and b["result"].get("vendorid") == f["result"].get("vendorid"):
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID28_Invalid_VendorID_Nochange Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID28_Invalid_VendorID_Nochange Failed")
    return False
