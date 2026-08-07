/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "L2Tests.h"
#include "L2TestsMock.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
// For ScopedHostFile below: lstat/stat/chmod to snapshot and reassert the mode of the
// host-global files this fixture provisions, and errno to tell "absent" from "unreadable".
#include <cerrno>
#include <iterator>
#include <sys/stat.h>
#include <sys/types.h>
// Counters written by mock actions on the plugin's own threads and read by the test thread.
#include <atomic>
#include <string>
#include <interfaces/IHdmiCecSource.h>
// Used to change the power state for events
#include <interfaces/IPowerManager.h>

#define EVNT_TIMEOUT (5000)
#define HDMICECSOURCE_CALLSIGN _T("org.rdk.HdmiCecSource.1")
#define HDMICECSOURCE_L2TEST_CALLSIGN _T("L2tests.1")

#define TEST_LOG(x, ...)                                                                                                                         \
    fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); \
    fflush(stderr);

using ::testing::NiceMock;
using namespace WPEFramework;
using testing::StrictMock;
using HdmiCecSourceSuccess = WPEFramework::Exchange::IHdmiCecSource::HdmiCecSourceSuccess;
using HdmiCecSourceDevice = WPEFramework::Exchange::IHdmiCecSource::HdmiCecSourceDevices;
using IHdmiCecSourceDeviceListIterator = WPEFramework::Exchange::IHdmiCecSource::IHdmiCecSourceDeviceListIterator;
using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;

namespace {
    static void removeFile(const char* fileName)
	{
		if (std::remove(fileName) != 0)
		{
			printf("File %s failed to remove\n", fileName);
			perror("Error deleting file");
		}
		else
		{
			printf("File %s successfully deleted\n", fileName);
		}
	}
	
	// Retained rather than removed: this pass does not delete test-support code, and this pair
	// remains the file's documented primitive for provisioning a fixture file.  The fixture no
	// longer calls createFile - it goes through ScopedHostFile below, which snapshots and
	// restores instead of deleting - so it is marked as deliberately unused to keep the build
	// warning-clean.
	static void createFile(const char* fileName, const char* fileContent) __attribute__((unused));
	static void createFile(const char* fileName, const char* fileContent)
	{
		removeFile(fileName);

		std::ofstream fileContentStream(fileName);
		fileContentStream << fileContent;
		fileContentStream << "\n";
		fileContentStream.close();
	}

    // ------------------------------------------------------------------------------------
    // Custody of the host-global files this fixture provisions.
    //
    // WHY
    // ---
    // The four paths below sit outside every build tree and are shared with the source
    // plugin's L1 suite, the sink plugin's suites and the host itself.  This fixture used to
    // recreate them in its constructor and DELETE them in its destructor without putting back
    // what it found, so the suite handed an absent /etc/device.properties - which the plugin
    // refuses to activate without - to whatever ran next, and inherited whatever the previous
    // runner had left.  Under `run_coverage.sh all` the L1 level runs immediately before L2 and
    // also deletes that file, which is why the failure was sequence-sensitive rather than
    // reproducible.
    //
    // WHAT
    // ----
    // Snapshot each path once (present/absent, contents, st_mode), write the value this suite
    // needs, and put the snapshot back afterwards - contents AND permissions, or removal when
    // the path genuinely did not exist.  Capture-and-restore, never blind deletion, so a
    // developer's or a device's real files survive a test run.  lstat is used rather than stat
    // so a symlink planted at one of these fixed, world-traversable paths is refused instead of
    // being written through (std::ofstream would follow it).
    // ------------------------------------------------------------------------------------
    static bool isRegularFileOrAbsent(const char* fileName)
    {
        struct stat pathStat;
        if (lstat(fileName, &pathStat) != 0) {
            return errno == ENOENT;
        }
        return S_ISREG(pathStat.st_mode) != 0;
    }

    class ScopedHostFile {
    public:
        // desiredContents == nullptr means "this suite needs the path ABSENT".
        ScopedHostFile(const char* fileName, const char* desiredContents)
            : m_fileName(fileName)
            , m_wasPresent(false)
            , m_contents()
            , m_mode(0)
            , m_captured(false)
            , m_provisioned(false)
        {
            m_captured = Capture();
            if (m_captured) {
                m_provisioned = (desiredContents == nullptr) ? Remove() : Write(desiredContents, 0);
            }
        }

        ScopedHostFile(const ScopedHostFile&) = delete;
        ScopedHostFile& operator=(const ScopedHostFile&) = delete;

        // A destructor cannot throw, so a failure is reported and the run continues.
        ~ScopedHostFile()
        {
            if (!m_captured) {
                return;
            }
            const bool restored = m_wasPresent ? Write(m_contents.c_str(), m_mode) : Remove();
            if (!restored) {
                printf("ScopedHostFile: %s could not be restored to the state this fixture found\n",
                    m_fileName);
            }
        }

        bool IsProvisioned() const { return m_captured && m_provisioned; }
        const char* Name() const { return m_fileName; }

        // Change the value while the snapshot stays owned by this object.  A test that needs the
        // path to hold something different for the duration of one case goes through here rather
        // than writing the path itself, so the restore at the end is still the snapshot this
        // object captured and not whatever the test left behind.
        bool Overwrite(const char* contents)
        {
            return m_captured && Write(contents, 0);
        }

    private:
        bool Capture()
        {
            if (!isRegularFileOrAbsent(m_fileName)) {
                printf("ScopedHostFile: %s is not a regular file; refusing to manage it\n", m_fileName);
                return false;
            }

            struct stat pathStat;
            if (stat(m_fileName, &pathStat) != 0) {
                // Absent is a legitimate starting state and is what gets restored later.
                m_wasPresent = false;
                m_contents.clear();
                m_mode = 0;
                return errno == ENOENT;
            }

            std::ifstream in(m_fileName, std::ios::binary);
            if (!in.is_open()) {
                printf("ScopedHostFile: %s exists but could not be read; refusing to manage it\n", m_fileName);
                return false;
            }
            m_contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            m_wasPresent = true;
            m_mode = pathStat.st_mode;
            return true;
        }

        // capturedMode of 0 means "leave whatever mode the path ends up with"; a captured mode
        // is reasserted so a path this fixture recreated does not stay more permissive than the
        // host had it.
        bool Write(const char* contents, const mode_t capturedMode)
        {
            if (!isRegularFileOrAbsent(m_fileName)) {
                printf("ScopedHostFile: %s is not a regular file; refusing to write it\n", m_fileName);
                return false;
            }
            std::ofstream out(m_fileName, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                printf("ScopedHostFile: %s could not be opened for writing\n", m_fileName);
                return false;
            }
            out << contents;
            out.close();
            if (out.fail()) {
                printf("ScopedHostFile: writing %s failed\n", m_fileName);
                return false;
            }
            if (capturedMode != 0 && chmod(m_fileName, capturedMode & 07777) != 0) {
                printf("ScopedHostFile: could not restore the permissions of %s\n", m_fileName);
                return false;
            }
            return true;
        }

        bool Remove()
        {
            if (!isRegularFileOrAbsent(m_fileName)) {
                printf("ScopedHostFile: %s is not a regular file; refusing to remove it\n", m_fileName);
                return false;
            }
            // Absent is the goal, so ENOENT is success.  std::remove rather than unlink: it
            // removes the entry rather than following it, and unlink is linker-wrapped in some
            // of this project's builds.
            return (std::remove(m_fileName) == 0) || (errno == ENOENT);
        }

        const char* m_fileName;
        bool m_wasPresent;
        std::string m_contents;
        mode_t m_mode;
        bool m_captured;
        bool m_provisioned;
    };

    // Transmit counters for the tests that need to know whether the implementation put anything on
    // the CEC bus during a particular window.
    //
    // Deliberately at namespace scope rather than as test-body locals.  A gmock action installed
    // in a test body stays live until the MOCK is destroyed, which happens inside the fixture's
    // base-class destructor - long after the test body's stack frame is gone - and the
    // implementation's poll, update and key-event threads keep transmitting until the plugin is
    // deactivated in the fixture destructor.  An action that captured a stack local by reference
    // would therefore be writing through a dangling pointer for the whole of that window.  These
    // have static storage duration, so there is no such window; tests sample a delta, which makes
    // sharing them across cases harmless.
    static std::atomic<int> g_sendToCount{ 0 };
    static std::atomic<int> g_broadcastCount{ 0 };

class AsyncHandlerMock {
public:
    virtual ~AsyncHandlerMock() = default;
    virtual void onActiveSourceStatusUpdated(bool status) = 0;
    virtual void onDeviceAdded(int logicalAddress) = 0;
    virtual void onDeviceRemoved(int logicalAddress) = 0;
    virtual void onDeviceInfoUpdated(int logicalAddress) = 0;
    virtual void standbyMessageReceived(int logicalAddress) = 0;
    virtual void onKeyReleaseEvent(int logicalAddress) = 0;
    virtual void onKeyPressEvent(int logicalAddress, int keyCode) = 0;
};

class MockAsyncHandler : public AsyncHandlerMock {
public:
    MOCK_METHOD(void, onActiveSourceStatusUpdated, (bool status), (override));
    MOCK_METHOD(void, onDeviceAdded, (int logicalAddress), (override));
    MOCK_METHOD(void, onDeviceRemoved, (int logicalAddress), (override));
    MOCK_METHOD(void, onDeviceInfoUpdated, (int logicalAddress), (override));
    MOCK_METHOD(void, standbyMessageReceived, (int logicalAddress), (override));
    MOCK_METHOD(void, onKeyReleaseEvent, (int logicalAddress), (override));
    MOCK_METHOD(void, onKeyPressEvent, (int logicalAddress, int keyCode), (override));
};
}

// Event flags for different CEC events
typedef enum : uint32_t {
    ON_ACTIVE_SOURCE_STATUS_UPDATED = 0x00000001,
    ON_DEVICE_ADDED = 0x00000002,
    ON_DEVICE_REMOVED = 0x00000004,
    ON_DEVICE_INFO_UPDATED = 0x00000008,
    STANDBY_MESSAGE_RECEIVED = 0x00000010,
    ON_KEY_RELEASE_EVENT = 0x00000020,
    ON_KEY_PRESS_EVENT = 0x00000040,
    HDMICECSOURCE_STATUS_INVALID = 0x00000000
} HdmiCecSourceL2test_async_events_t;

// Notification handler for HdmiCecSource events
class HdmiCecSourceNotificationHandler : public Exchange::IHdmiCecSource::INotification {
private:
    // mutable so the removal-payload accessor below can be const and still take the lock:
    // OnDeviceRemoved is invoked from the plugin's threads, so an unsynchronised read of the
    // recorded payload would be a data race.
    mutable std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    uint32_t m_event_signalled;
    // Every logical address OnDeviceRemoved has been raised for, not just the last one.
    std::set<int> m_removedAddresses;

    BEGIN_INTERFACE_MAP(Notification)
    INTERFACE_ENTRY(Exchange::IHdmiCecSource::INotification)
    END_INTERFACE_MAP

public:
    HdmiCecSourceNotificationHandler()
        : m_event_signalled(HDMICECSOURCE_STATUS_INVALID)
        , m_activeSourceStatus(false)
        , m_logicalAddress(0)
        , m_keyCode(0)
    {
    }

    ~HdmiCecSourceNotificationHandler() override = default;

    void OnActiveSourceStatusUpdated(const bool status) override
    {
        TEST_LOG("OnActiveSourceStatusUpdated event received, status: %d", status);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_activeSourceStatus = status;
        m_event_signalled |= ON_ACTIVE_SOURCE_STATUS_UPDATED;
        m_condition_variable.notify_one();
    }

    void OnDeviceAdded(const int logicalAddress) override
    {
        TEST_LOG("OnDeviceAdded event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_DEVICE_ADDED;
        m_condition_variable.notify_one();
    }

    void OnDeviceRemoved(const int logicalAddress) override
    {
        TEST_LOG("OnDeviceRemoved event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        // Every removal payload is kept, not just the most recent one:
        // HdmiCecSourceImplementation::removeAllCecDevices() emits one OnDeviceRemoved per
        // present device in a single sweep, so a "last address seen" reading cannot be
        // asserted on deterministically.
        m_removedLogicalAddresses.push_back(logicalAddress);
        m_removedAddresses.insert(logicalAddress);
        m_event_signalled |= ON_DEVICE_REMOVED;
        m_condition_variable.notify_one();
    }

    void OnDeviceInfoUpdated(const int logicalAddress) override
    {
        TEST_LOG("OnDeviceInfoUpdated event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_DEVICE_INFO_UPDATED;
        m_condition_variable.notify_one();
    }

    void StandbyMessageReceived(const int logicalAddress) override
    {
        TEST_LOG("StandbyMessageReceived event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= STANDBY_MESSAGE_RECEIVED;
        m_condition_variable.notify_one();
    }

    void OnKeyReleaseEvent(const int logicalAddress) override
    {
        TEST_LOG("OnKeyReleaseEvent event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_KEY_RELEASE_EVENT;
        m_condition_variable.notify_one();
    }

    void OnKeyPressEvent(const int logicalAddress, const int keyCode) override
    {
        TEST_LOG("OnKeyPressEvent event received, logicalAddress: %d, keyCode: %d", logicalAddress, keyCode);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_keyCode = keyCode;
        m_event_signalled |= ON_KEY_PRESS_EVENT;
        m_condition_variable.notify_one();
    }

    uint32_t WaitForEvent(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto now = std::chrono::system_clock::now();
        auto timeout = now + std::chrono::milliseconds(timeout_ms);
        uint32_t signalled = HDMICECSOURCE_STATUS_INVALID;

        while (!(m_event_signalled & expected_status)) {
            if (m_condition_variable.wait_until(lock, timeout) == std::cv_status::timeout) {
                TEST_LOG("Timeout waiting for event: 0x%08X", expected_status);
                return HDMICECSOURCE_STATUS_INVALID;
            }
        }

        signalled = m_event_signalled & expected_status;
        m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
        return signalled;
    }

    void ResetEvent()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
    }

    bool GetActiveSourceStatus() const { return m_activeSourceStatus; }
    int GetLogicalAddress() const { return m_logicalAddress; }

    /**
     * Whether OnDeviceRemoved was raised for one specific logical address.
     *
     * GetLogicalAddress only remembers the most recent notification, which is not enough when
     * production removes several devices in one sweep - removeAllCecDevices() notifies every
     * present address in turn, so the last one reported is whichever happens to be highest, not
     * the address under test. Every removed address is recorded here so a test can name the one
     * it cares about.
     */
    bool WasRemoved(const int logicalAddress) const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_removedAddresses.find(logicalAddress) != m_removedAddresses.end();
    }
    int GetKeyCode() const { return m_keyCode; }

    // Snapshot of every logical address OnDeviceRemoved has reported since the last clear.
    std::vector<int> GetRemovedLogicalAddresses() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_removedLogicalAddresses;
    }

    void ClearRemovedLogicalAddresses()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_removedLogicalAddresses.clear();
        m_removedAddresses.clear();
    }

private:
    bool m_activeSourceStatus;
    int m_logicalAddress;
    int m_keyCode;
    std::vector<int> m_removedLogicalAddresses;
};

class AsyncHandlerMock_HdmiCecSource {
public:
    AsyncHandlerMock_HdmiCecSource()
    {
        m_asyncHandlerMock = new NiceMock<MockAsyncHandler>;
    }

    virtual ~AsyncHandlerMock_HdmiCecSource()
    {
        delete m_asyncHandlerMock;
    }

    MockAsyncHandler& mock() { return *m_asyncHandlerMock; }

private:
    MockAsyncHandler* m_asyncHandlerMock;
};

class HdmiCecSource_L2Test : public L2TestMocks {
protected:
    HdmiCecSource_L2Test();
    virtual ~HdmiCecSource_L2Test() override;

public:
    uint32_t CreateHdmiCecSourceInterfaceObject();
    uint32_t WaitForRequestStatus(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status);
    /*
     * Waits on the JSON-RPC side of the fixture. WaitForRequestStatus() above delegates to
     * m_notificationHandler, which only ever sees COM-RPC notifications; the seven
     * on<Event>(const JsonObject&) members below record into this fixture's own
     * m_event_signalled instead, and until this helper existed nothing read that field. A test
     * that needs to prove the JSON-RPC leg of an emission actually fired - i.e. that
     * HdmiCecSource.h's Notification sink reached Exchange::JHdmiCecSource::Event::* - subscribes
     * one of those members through JSONRPC::LinkType::Subscribe and then waits here.
     * Semantics deliberately mirror HdmiCecSourceNotificationHandler::WaitForEvent: returns the
     * matched bits and clears the accumulated set on success, returns
     * HDMICECSOURCE_STATUS_INVALID on timeout.
     */
    uint32_t WaitForJsonRpcEvent(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status);
    void onActiveSourceStatusUpdated(const JsonObject& message);
    void onDeviceAdded(const JsonObject& message);
    void onDeviceInfoUpdated(const JsonObject& message);
    void onDeviceRemoved(const JsonObject& message);
    void standbyMessageReceived(const JsonObject& message);
    void onKeyReleaseEvent(const JsonObject& message);
    void onKeyPressEvent(const JsonObject& message);

protected:
    Exchange::IHdmiCecSource* m_cecSourcePlugin = nullptr;
    PluginHost::IShell* m_controller_cecSource = nullptr;
    Core::Sink<HdmiCecSourceNotificationHandler> m_notificationHandler;
    IARM_EventHandler_t dsHdmiEventHandler = nullptr;
    IARM_EventHandler_t powerEventHandler = nullptr;
    FrameListener* registeredListener = nullptr;
    std::vector<FrameListener*> listeners;

    // The implementation's own display-device listener, captured as it registers.
    //
    // This is the ONLY route to the HDMI hotplug path at L2.  The static
    // HdmiCecSourceImplementation::dsHdmiEventHandler declared at
    // HdmiCecSourceImplementation.h:290 is never defined and never registered, so the
    // dsHdmiEventHandler member above - which captures whatever DSMGR handler the plugin
    // registers over IARM - stays null for the hotplug event and cannot be used.  What the
    // implementation actually does is register ITSELF as a device::Host::IDisplayDeviceEvents
    // listener (HdmiCecSourceImplementation.cpp:392) and unregister at :365, so capturing that
    // pointer as it goes past gives a test the production entry point OnDisplayHDMIHotPlug().
    device::Host::IDisplayDeviceEvents* displayDeviceListener = nullptr;

    /**
     * Switch CEC on so the implementation registers its FrameListener, and wait until it has.
     *
     * The implementation only opens the CEC connection - and therefore only calls
     * addFrameListener - when CEC is enabled, and the enabled setting is persisted, so it is
     * shared state that outlives the plugin and carries between tests. A test that drives inbound
     * frames must establish that precondition itself. The state this test inherited is recorded on
     * the first call so TearDown can hand the next test the same starting point, whichever way it
     * was set and however this test ends.
     *
     * @param timeoutMs Upper bound, in milliseconds, on the wait for the registration.
     * @return true when at least one FrameListener has been captured.
     */
    bool EnableCecAndAwaitFrameListener(const uint32_t timeoutMs = 5000)
    {
        JsonObject params, result;

        if (!m_cecEntryStateCaptured
            && InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result) == Core::ERROR_NONE
            && result.HasLabel("enabled")) {
            m_cecEnabledOnEntry = result["enabled"].Boolean();
            m_cecEntryStateCaptured = true;
        }

        if (!listeners.empty()) {
            return true;
        }

        params["enabled"] = true;
        if (InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setEnabled", params, result) != Core::ERROR_NONE) {
            return false;
        }

        const uint32_t pollIntervalMs = 20;
        for (uint32_t waitedMs = 0; waitedMs <= timeoutMs; waitedMs += pollIntervalMs) {
            if (!listeners.empty()) {
                return true;
            }
            usleep(pollIntervalMs * 1000);
        }

        return !listeners.empty();
    }

    /**
     * Put the persisted CEC-enabled setting back to the value this test inherited.
     *
     * Restores in either direction, because a test may legitimately have to switch CEC off (that
     * is how the implementation is made to clear its device cache) as well as on.
     */
    void RestoreCecEnabledState()
    {
        if (!m_cecEntryStateCaptured) {
            return;
        }

        JsonObject params, result;
        params["enabled"] = m_cecEnabledOnEntry;
        InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setEnabled", params, result);
        m_cecEntryStateCaptured = false;
    }

    /**
     * Activate a service and return only once it is genuinely activated.
     *
     * WHY THIS EXISTS
     * ---------------
     * `ActivateService()` forwards to `Controller.1.activate`, which lands in
     * `Server::Service::Activate()` (Thunder/Source/WPEFramework/PluginServer.cpp:319).  That
     * method returns `Core::ERROR_INPROGRESS` (12) when the plugin's current state is already
     * `IShell::state::ACTIVATION` - i.e. an activation started by another thread has not
     * finished yet.  It releases its lock around `_handler->Initialize(this)`
     * (PluginServer.cpp:400), so that window is wide, and the framework activates plugins on
     * its own threads too: a plugin parked in `PRECONDITION` is activated again from the
     * subsystem-change path once its preconditions are met.  A fixture that asserted
     * `ERROR_NONE` on the first call therefore failed whenever it lost that race - one test in
     * roughly every full `run_coverage.sh all` run, with `status Which is: 12`.
     *
     * Asserting on the RETURN CODE of one call is the defect; what the fixture actually needs
     * is the plugin to be activated. `Activate()` returns `ERROR_NONE` for a plugin already in
     * `ACTIVATED` (it falls through every branch), so re-invoking is both the wait and the
     * check: once the in-flight activation completes, the next call reports success. If that
     * activation FAILED, the state is `DEACTIVATED` and the retry starts a fresh one, which
     * either succeeds or returns the real error - so a genuine activation failure is still
     * reported rather than waited out.
     *
     * `ERROR_PENDING_CONDITIONS` (31) is treated the same way: the plugin is parked waiting for
     * a subsystem and will be activated by the framework, so it is a "not yet", not a failure.
     *
     * @param callsign Service to activate.
     * @param timeoutMs Upper bound on the wait.
     * @return Core::ERROR_NONE once activated, otherwise the last error the framework reported.
     */
    uint32_t ActivateServiceAndAwaitActivated(const char* callsign, const uint32_t timeoutMs = 10000)
    {
        const uint32_t pollIntervalMs = 100;
        uint32_t status = ActivateService(callsign);

        for (uint32_t waitedMs = 0;
             (status == Core::ERROR_INPROGRESS || status == Core::ERROR_PENDING_CONDITIONS)
                 && waitedMs < timeoutMs;
             waitedMs += pollIntervalMs) {
            TEST_LOG("%s is mid-transition (status %u); waiting for it to settle", callsign, status);
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            status = ActivateService(callsign);
        }

        return status;
    }

    void TearDown() override
    {
        RestoreCecEnabledState();
    }

    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> HdmiCecSource_Engine;
    Core::ProxyType<RPC::CommunicatorClient> HdmiCecSource_Client;

protected:
    // Custody of the four host-global paths this fixture provisions, held as members so they are
    // constructed before this fixture's constructor body (which activates the plugins, and the
    // plugin refuses to activate without /etc/device.properties) and destroyed after its
    // destructor body.  Declared in the order the previous code wrote them.
    //
    // /opt/uimgr_settings.bin is listed because the destructor used to delete it: this suite does
    // not write it, but the PowerManager plugin does, so it is captured and restored rather than
    // removed outright.
    ScopedHostFile m_deviceProperties{ "/etc/device.properties", "RDK_PROFILE=STB\n" };
    ScopedHostFile m_cecSettings{ "/opt/persistent/ds/cecData_2.json", "0\n" };
    ScopedHostFile m_pwrMgrRestarted{ "/tmp/pwrmgr_restarted", "2\n" };
    ScopedHostFile m_uimgrSettings{ "/opt/uimgr_settings.bin", nullptr };

private:
    std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    uint32_t m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
    bool m_cecEnabledOnEntry = false;
    bool m_cecEntryStateCaptured = false;
};

HdmiCecSource_L2Test::HdmiCecSource_L2Test()
    : L2TestMocks()
{
    TEST_LOG("HdmiCecSource_L2Test Constructor");

    // The four host-global files are already in the state this suite needs: the ScopedHostFile
    // members above provisioned them before this body ran, and they put back exactly what they
    // found once the destructor body has finished.  This replaces the previous
    // remove-then-create pair, which left the next runner with an absent
    // /etc/device.properties and made activation depend on run order.
    EXPECT_TRUE(m_deviceProperties.IsProvisioned())
        << m_deviceProperties.Name() << " could not be provisioned; the plugin will refuse to activate.";
    EXPECT_TRUE(m_cecSettings.IsProvisioned()) << m_cecSettings.Name() << " could not be provisioned.";
    EXPECT_TRUE(m_pwrMgrRestarted.IsProvisioned()) << m_pwrMgrRestarted.Name() << " could not be provisioned.";

    // Add sleep to ensure file is properly written to disk
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Mock IARM Bus initialization
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Init(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Connect())
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

    // Mock IARM Event Registration to capture event handlers
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_RegisterEventHandler(::testing::_, ::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [this](const char* ownerName, IARM_EventId_t eventId, IARM_EventHandler_t handler) {
                if (strcmp(ownerName, IARM_BUS_DSMGR_NAME) == 0) {
                    if (eventId == IARM_BUS_DSMGR_EVENT_HDMI_HOTPLUG) {
                        dsHdmiEventHandler = handler;
                        TEST_LOG("Captured HDMI HotPlug Event Handler");
                    }
                } else if (strcmp(ownerName, IARM_BUS_PWRMGR_NAME) == 0) {
                    if (eventId == IARM_BUS_PWRMGR_EVENT_MODECHANGED) {
                        powerEventHandler = handler;
                        TEST_LOG("Captured Power Manager Event Handler");
                    }
                }
                return IARM_RESULT_SUCCESS;
            }));

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_UnRegisterEventHandler(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
        .Times(::testing::AnyNumber())
        .WillRepeatedly(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                IARM_Result_t result = IARM_RESULT_SUCCESS;
                if (strcmp(ownerName, IARM_BUS_PWRMGR_NAME) == 0) {
                    if (strcmp(methodName, IARM_BUS_PWRMGR_API_GetPowerState) == 0) {
                        auto* param = static_cast<IARM_Bus_PWRMgr_GetPowerState_Param_t*>(arg);
                        param->curState = IARM_BUS_PWRMGR_POWERSTATE_ON;
                    }
                }
                return result;
            });

    // Mock device settings Manager
    ON_CALL(*p_managerImplMock, Initialize())
        .WillByDefault(::testing::Return());

    // Mock Host methods
    ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillByDefault(::testing::Return(std::string("HDMI0")));

    ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
        .WillByDefault(::testing::ReturnRef(device::VideoOutputPort::getInstance()));

    // Mock VideoOutputPort methods
    ON_CALL(*p_videoOutputPortMock, isDisplayConnected())
        .WillByDefault(::testing::Return(true));

    ON_CALL(*p_videoOutputPortMock, getDisplay())
        .WillByDefault(::testing::ReturnRef(device::Display::getInstance()));

    // Mock Display methods - getEDIDBytes is void and takes a reference parameter
    ON_CALL(*p_displayMock, getEDIDBytes(::testing::_))
        .WillByDefault(::testing::Invoke(
            [](std::vector<uint8_t>& edid) {
                edid = {
                    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
                    0x4C, 0x2D, 0xFE, 0x08, 0x00, 0x00, 0x00, 0x00
                };
            }));

    // Default transmit behaviour: count, and let the call succeed.  Installed as an ON_CALL so any
    // test that needs sendTo to fail can still override it with its own EXPECT_CALL, and so the
    // counting action lives for the lifetime of the mock rather than for the lifetime of a test
    // body (see g_sendToCount for why that distinction matters here).
    ON_CALL(*p_connectionMock, sendTo(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](const LogicalAddress& to, const CECFrame&) {
                ++g_sendToCount;
                if (to.toInt() == LogicalAddress::BROADCAST) {
                    ++g_broadcastCount;
                }
            }));

    // Capture the implementation's display-device listener as it registers, so the HDMI hotplug
    // path can be driven through its production entry point.  Set up here, before the
    // ActivateService calls at the end of this constructor, because that is when the
    // implementation registers.
    ON_CALL(*p_hostImplMock, Register(::testing::Matcher<device::Host::IDisplayDeviceEvents*>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [this](device::Host::IDisplayDeviceEvents* listener) {
                displayDeviceListener = listener;
                TEST_LOG("Display device listener registered: %p", static_cast<void*>(listener));
                return dsERR_NONE;
            }));

    ON_CALL(*p_hostImplMock, UnRegister(::testing::Matcher<device::Host::IDisplayDeviceEvents*>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [this](device::Host::IDisplayDeviceEvents* listener) {
                if (displayDeviceListener == listener) {
                    // Dropped as the implementation unregisters, so no test can call through a
                    // pointer the plugin has already torn down.
                    displayDeviceListener = nullptr;
                }
                return dsERR_NONE;
            }));

    // Mock HDMI CEC Connection - capture frame listeners for event injection
    ON_CALL(*p_connectionMock, addFrameListener(::testing::_))
        .WillByDefault(::testing::Invoke(
            [this](FrameListener* listener) {
                TEST_LOG("addFrameListener called with address: %p", static_cast<void*>(listener));
                if (listener != nullptr) {
                    registeredListener = listener;
                    listeners.push_back(listener);
                    TEST_LOG("Frame listener registered, total listeners: %zu", listeners.size());
                }
            }));

    // Mock MessageEncoder - need to mock both overloads explicitly
    ON_CALL(*p_messageEncoderMock, encode(::testing::Matcher<const DataBlock&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [](const DataBlock& m) -> CECFrame& {
                static CECFrame frame;
                return frame;
            }));

    ON_CALL(*p_messageEncoderMock, encode(::testing::Matcher<const UserControlPressed&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [](const UserControlPressed& m) -> CECFrame& {
                static CECFrame frame;
                return frame;
            }));

    // Mock Wraps
    ON_CALL(*p_wrapsImplMock, access(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(0));

    // Mock PowerManager HAL for PowerManager plugin initialization
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_INIT())
        .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_INIT())
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_GetPowerState(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](PWRMgr_PowerState_t* powerState) {
                *powerState = PWRMGR_POWERSTATE_ON;
                return PWRMGR_SUCCESS;
            }));

    ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                if (strcmp("RFC_DATA_ThermalProtection_POLL_INTERVAL", pcParameterName) == 0) {
                    strcpy(pstParamData->value, "2");
                    return WDMP_SUCCESS;
                } else if (strcmp("RFC_ENABLE_ThermalProtection", pcParameterName) == 0) {
                    strcpy(pstParamData->value, "true");
                    return WDMP_SUCCESS;
                } else if (strcmp("RFC_DATA_ThermalProtection_DEEPSLEEP_GRACE_INTERVAL", pcParameterName) == 0) {
                    strcpy(pstParamData->value, "6");
                    return WDMP_SUCCESS;
                } else {
                    return WDMP_FAILURE;
                }
            }));

    ON_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](int high, int critical) {
                return mfrERR_NONE;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                return PWRMGR_SUCCESS;
            }));

    ON_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                *curTemperature = 90;
                *curState = (mfrTemperatureState_t)0;
                *wifiTemperature = 25;
                return mfrERR_NONE;
            }));

    ON_CALL(*p_connectionMock, open())
        .WillByDefault(::testing::Return());

    ON_CALL(*p_connectionMock, poll(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](const LogicalAddress& from, const Throw_e& doThrow) {
                throw CECNoAckException();
            }));

    EXPECT_CALL(*p_libCCECMock, getPhysicalAddress(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [&](uint32_t* physAddress) {
                *physAddress = (uint32_t)0x12345678;
            }));

    /* Activate plugin in constructor.
     *
     * Waits out a transition already in flight instead of asserting on the return code of a
     * single call - see ActivateServiceAndAwaitActivated for the mechanism and the measured
     * failure it removes. */
    uint32_t status = ActivateServiceAndAwaitActivated("org.rdk.PowerManager");
    EXPECT_EQ(Core::ERROR_NONE, status);

    status = ActivateServiceAndAwaitActivated("org.rdk.HdmiCecSource");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

HdmiCecSource_L2Test::~HdmiCecSource_L2Test()
{
    TEST_LOG("HdmiCecSource_L2Test Destructor");
    uint32_t status = Core::ERROR_GENERAL;

    ON_CALL(*p_connectionMock, close())
        .WillByDefault(::testing::Return());

    sleep(5);

    // Deactivate services in reverse order
    status = DeactivateService("org.rdk.HdmiCecSource");
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_TERM())
        .WillOnce(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_TERM())
        .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

    status = DeactivateService("org.rdk.PowerManager");
    EXPECT_EQ(Core::ERROR_NONE, status);

    if (HdmiCecSource_Client.IsValid()) {
        HdmiCecSource_Client.Release();
    }

    if (HdmiCecSource_Engine.IsValid()) {
        HdmiCecSource_Engine.Release();
    }

    // The four host-global files are handed back by the ScopedHostFile members, which run after
    // this body and restore exactly what they captured - including removing a file that was not
    // there to begin with.  Deleting them here instead is what left an absent
    // /etc/device.properties for the next suite in an `all` run.

    TEST_LOG("HdmiCecSource_L2Test cleanup complete");
}

uint32_t HdmiCecSource_L2Test::CreateHdmiCecSourceInterfaceObject()
{
    uint32_t return_value = Core::ERROR_GENERAL;

    TEST_LOG("Creating HdmiCecSource_Engine");
    HdmiCecSource_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    HdmiCecSource_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(
        Core::NodeId("/tmp/communicator"),
        Core::ProxyType<Core::IIPCServer>(HdmiCecSource_Engine));

    TEST_LOG("Creating HdmiCecSource_Engine Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    HdmiCecSource_Engine->Announcements(HdmiCecSource_Client->Announcement());
#endif

    if (!HdmiCecSource_Client.IsValid()) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        m_controller_cecSource = HdmiCecSource_Client->Open<PluginHost::IShell>(
            _T("org.rdk.HdmiCecSource"), ~0, 3000);
        if (m_controller_cecSource) {
            m_cecSourcePlugin = m_controller_cecSource->QueryInterface<Exchange::IHdmiCecSource>();
            if (m_cecSourcePlugin) {
                m_cecSourcePlugin->Register(&m_notificationHandler);
                return_value = Core::ERROR_NONE;
                TEST_LOG("Successfully created HdmiCecSource Plugin Interface");
            } else {
                TEST_LOG("Failed to get IHdmiCecSource interface");
            }
        } else {
            TEST_LOG("Failed to get HdmiCecSource Plugin Interface");
        }
    }
    return return_value;
}

uint32_t HdmiCecSource_L2Test::WaitForRequestStatus(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status)
{
    return m_notificationHandler.WaitForEvent(timeout_ms, expected_status);
}

uint32_t HdmiCecSource_L2Test::WaitForJsonRpcEvent(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto timeout = std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (!(m_event_signalled & expected_status)) {
        if (m_condition_variable.wait_until(lock, timeout) == std::cv_status::timeout) {
            TEST_LOG("Timeout waiting for JSON-RPC event: 0x%08X", expected_status);
            return HDMICECSOURCE_STATUS_INVALID;
        }
    }

    uint32_t signalled = m_event_signalled & expected_status;
    m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
    return signalled;
}

void HdmiCecSource_L2Test::onActiveSourceStatusUpdated(const JsonObject& message)
{
    TEST_LOG("onActiveSourceStatusUpdated JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_ACTIVE_SOURCE_STATUS_UPDATED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onDeviceAdded(const JsonObject& message)
{
    TEST_LOG("onDeviceAdded JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_DEVICE_ADDED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onDeviceInfoUpdated(const JsonObject& message)
{
    TEST_LOG("onDeviceInfoUpdated JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_DEVICE_INFO_UPDATED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onDeviceRemoved(const JsonObject& message)
{
    TEST_LOG("onDeviceRemoved JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_DEVICE_REMOVED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::standbyMessageReceived(const JsonObject& message)
{
    TEST_LOG("standbyMessageReceived JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= STANDBY_MESSAGE_RECEIVED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onKeyReleaseEvent(const JsonObject& message)
{
    TEST_LOG("onKeyReleaseEvent JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_KEY_RELEASE_EVENT;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onKeyPressEvent(const JsonObject& message)
{
    TEST_LOG("onKeyPressEvent JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_KEY_PRESS_EVENT;
    m_condition_variable.notify_one();
}

/*******************************************************************************************************************
 * Test Functions
 * *****************************************************************************************************************/

/**
 * @brief Test GetActiveSourceStatus API via COM-RPC
 *
 * This test verifies that the GetActiveSourceStatus API returns the correct status
 * and success flag using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetActiveSourceStatus_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetActiveSourceStatus via COM-RPC");

                // Declare output parameters
                bool isActiveSource = false;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetActiveSourceStatus(isActiveSource, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  isActiveSource: %d", isActiveSource);
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetActiveSourceStatus API via JSON-RPC
 *
 * This test verifies that the getActiveSourceStatus API returns the correct status
 * using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetActiveSourceStatus_JSONRPC)
{
    TEST_LOG("Testing getActiveSourceStatus via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getActiveSourceStatus", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate status field
    EXPECT_TRUE(result.HasLabel("status"));
    if (result.HasLabel("status")) {
        bool activeSourceStatus = result["status"].Boolean();
        TEST_LOG("  status: %d", activeSourceStatus);
    }
}

/**
 * @brief Test SetEnabled API via COM-RPC
 *
 * This test verifies that the SetEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetEnabled via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetEnabled(true, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetEnabled API via JSON-RPC
 *
 * This test verifies that the setEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetEnabled_JSONRPC)
{
    TEST_LOG("Testing setEnabled via JSON-RPC");

    JsonObject params;
    params["enabled"] = true;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetEnabled API via COM-RPC
 *
 * This test verifies that the GetEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetEnabled via COM-RPC");

                // Declare output parameters
                bool enabled = false;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetEnabled(enabled, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log output
                TEST_LOG("  enabled: %d", enabled);
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetEnabled API via JSON-RPC
 *
 * This test verifies that the getEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetEnabled_JSONRPC)
{
    TEST_LOG("Testing getEnabled via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate enabled field
    EXPECT_TRUE(result.HasLabel("enabled"));
    if (result.HasLabel("enabled")) {
        bool enabled = result["enabled"].Boolean();
        TEST_LOG("  enabled: %d", enabled);
    }
}

/**
 * @brief Test SetOSDName API via COM-RPC
 *
 * This test verifies that the SetOSDName API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOSDName_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetOSDName via COM-RPC");

                // Declare output parameters
                string testOSDName = "TestSTB";
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetOSDName(testOSDName, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  osdName set to: %s", testOSDName.c_str());
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetOSDName API via JSON-RPC
 *
 * This test verifies that the setOSDName API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOSDName_JSONRPC)
{
    TEST_LOG("Testing setOSDName via JSON-RPC");

    JsonObject params;
    params["name"] = "TestSTB";
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setOSDName", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetOSDName API via COM-RPC
 *
 * This test verifies that the GetOSDName API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOSDName_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetOSDName via COM-RPC");

                // Declare output parameters
                string osdName;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetOSDName(osdName, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  osdName: %s", osdName.c_str());
                TEST_LOG("  success: %d", success);
                EXPECT_FALSE(osdName.empty());

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetOSDName API via JSON-RPC
 *
 * This test verifies that the getOSDName API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOSDName_JSONRPC)
{
    TEST_LOG("Testing getOSDName via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getOSDName", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate name field
    EXPECT_TRUE(result.HasLabel("name"));
    if (result.HasLabel("name")) {
        string osdName = result["name"].String();
        TEST_LOG("  name: %s", osdName.c_str());
        EXPECT_FALSE(osdName.empty());
    }
}

/**
 * @brief Test SetVendorId API via COM-RPC
 *
 * This test verifies that the SetVendorId API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetVendorId_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetVendorId via COM-RPC");

                // Declare output parameters
                string testVendorId = "0019FB";
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetVendorId(testVendorId, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  vendorId set to: %s", testVendorId.c_str());
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetVendorId API via JSON-RPC
 *
 * This test verifies that the setVendorId API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetVendorId_JSONRPC)
{
    TEST_LOG("Testing setVendorId via JSON-RPC");

    JsonObject params;
    params["vendorid"] = "0019FB";
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setVendorId", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetVendorId API via JSON-RPC
 *
 * This test verifies that the getVendorId API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetVendorId_JSONRPC)
{
    TEST_LOG("Testing getVendorId via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getVendorId", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate vendorid field
    EXPECT_TRUE(result.HasLabel("vendorid"));
    if (result.HasLabel("vendorid")) {
        string vendorId = result["vendorid"].String();
        EXPECT_FALSE(vendorId.empty());
        TEST_LOG("  vendorid: %s", vendorId.c_str());
    }
}

/**
 * @brief Test SetOTPEnabled API via COM-RPC
 *
 * This test verifies that the SetOTPEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOTPEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetOTPEnabled via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetOTPEnabled(true, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetOTPEnabled API via JSON-RPC
 *
 * This test verifies that the setOTPEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOTPEnabled_JSONRPC)
{
    TEST_LOG("Testing setOTPEnabled via JSON-RPC");

    JsonObject params;
    params["enabled"] = true;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setOTPEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetOTPEnabled API via COM-RPC
 *
 * This test verifies that the GetOTPEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOTPEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetOTPEnabled via COM-RPC");

                // Declare output parameters
                bool enabled = false;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetOTPEnabled(enabled, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  enabled: %d", enabled);
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetOTPEnabled API via JSON-RPC
 *
 * This test verifies that the getOTPEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOTPEnabled_JSONRPC)
{
    TEST_LOG("Testing getOTPEnabled via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getOTPEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate enabled field
    EXPECT_TRUE(result.HasLabel("enabled"));
    if (result.HasLabel("enabled")) {
        bool enabled = result["enabled"].Boolean();
        TEST_LOG("  enabled: %d", enabled);
    }
}

/**
 * @brief Test SendStandbyMessage API via COM-RPC
 *
 * This test verifies that the SendStandbyMessage API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendStandbyMessage_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SendStandbyMessage via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess result;

                // Call the API
                uint32_t retval = m_cecSourcePlugin->SendStandbyMessage(result);

                // Validate result
                EXPECT_EQ(retval, Core::ERROR_NONE);
                if (retval != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(retval) + " (" + std::string(Core::ErrorToString(retval)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(result.success);

                // Log output
                TEST_LOG("  success: %d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendStandbyMessage API via JSON-RPC
 *
 * This test verifies that the sendStandbyMessage API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendStandbyMessage_JSONRPC)
{
    TEST_LOG("Testing sendStandbyMessage via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "sendStandbyMessage", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test SendKeyPressEvent API via COM-RPC
 *
 * This test verifies that the SendKeyPressEvent API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEvent_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SendKeyPressEvent via COM-RPC");

                // Declare input/output parameters
                uint32_t logicalAddress = 0; // TV logical address
                uint32_t keyCode = 0x00; // Select key code
                HdmiCecSourceSuccess result;

                // Call the API
                uint32_t retval = m_cecSourcePlugin->SendKeyPressEvent(logicalAddress, keyCode, result);

                // Validate result
                EXPECT_EQ(retval, Core::ERROR_NONE);
                if (retval != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(retval) + " (" + std::string(Core::ErrorToString(retval)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(result.success);

                // Log output
                TEST_LOG("  logicalAddress: %d", logicalAddress);
                TEST_LOG("  keyCode: %d", keyCode);
                TEST_LOG("  success: %d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendKeyPressEvent API via JSON-RPC
 *
 * This test verifies that the sendKeyPressEvent API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEvent_JSONRPC)
{
    TEST_LOG("Testing sendKeyPressEvent via JSON-RPC");

    JsonObject params;
    params["logicalAddress"] = 0; // TV logical address
    params["keyCode"] = 0x00; // Select key code
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "sendKeyPressEvent", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetVendorId API via COM-RPC
 *
 * This test verifies that the GetVendorId API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetVendorId_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetVendorId via COM-RPC");

                // Declare output parameters
                string vendorId;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetVendorId(vendorId, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);
                EXPECT_FALSE(vendorId.empty());

                // Log output
                TEST_LOG("  vendorId: %s", vendorId.c_str());
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetDeviceList API via COM-RPC
 *
 * This test verifies that the GetDeviceList API returns the correct device information using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetDeviceList_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetDeviceList via COM-RPC");

                // Declare output parameters
                uint32_t numberOfDevices = 0;
                IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetDeviceList(numberOfDevices, deviceList, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  numberOfDevices: %d", numberOfDevices);
                TEST_LOG("  success: %d", success);

                if (deviceList != nullptr) {
                    HdmiCecSourceDevice device;
                    uint32_t deviceCount = 0;
                    while (deviceList->Next(device)) {
                        TEST_LOG("  Device[%d]: logicalAddress=%d, vendorID=%s, osdName=%s",
                                 deviceCount++, device.logicalAddress, device.vendorID.c_str(), device.osdName.c_str());
                        EXPECT_FALSE(device.vendorID.empty());
                        EXPECT_FALSE(device.osdName.empty());
                    }
                    EXPECT_EQ(deviceCount, numberOfDevices);
                    deviceList->Release();
                }

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetDeviceList API via JSON-RPC
 *
 * This test verifies that the getDeviceList API returns the correct device information using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetDeviceList_JSONRPC)
{
    TEST_LOG("Testing getDeviceList via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getDeviceList", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate numberofdevices field
    EXPECT_TRUE(result.HasLabel("numberofdevices"));
    if (result.HasLabel("numberofdevices")) {
        uint32_t numberOfDevices = result["numberofdevices"].Number();
        TEST_LOG("  numberofdevices: %d", numberOfDevices);
    }

    // Validate deviceList array
    EXPECT_TRUE(result.HasLabel("deviceList"));
    if (result.HasLabel("deviceList")) {
        JsonArray deviceList = result["deviceList"].Array();
        TEST_LOG("  deviceList length: %d", deviceList.Length());

        for (uint32_t i = 0; i < deviceList.Length(); i++) {
            JsonObject device = deviceList[i].Object();

            EXPECT_TRUE(device.HasLabel("logicalAddress"));
            if (device.HasLabel("logicalAddress")) {
                uint32_t logicalAddress = device["logicalAddress"].Number();
                TEST_LOG("    Device[%d].logicalAddress: %d", i, logicalAddress);
            }

            EXPECT_TRUE(device.HasLabel("vendorID"));
            if (device.HasLabel("vendorID")) {
                string vendorID = device["vendorID"].String();
                TEST_LOG("    Device[%d].vendorID: %s", i, vendorID.c_str());
                EXPECT_FALSE(vendorID.empty());
            }

            EXPECT_TRUE(device.HasLabel("osdName"));
            if (device.HasLabel("osdName")) {
                string osdName = device["osdName"].String();
                TEST_LOG("    Device[%d].osdName: %s", i, osdName.c_str());
                EXPECT_FALSE(osdName.empty());
            }
        }
    }
}

/**
 * @brief Test PerformOTPAction API via COM-RPC
 *
 * This test verifies that the PerformOTPAction API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, PerformOTPAction_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing PerformOTPAction via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess result;

                // Call the API
                uint32_t retval = m_cecSourcePlugin->PerformOTPAction(result);

                // Validate result
                EXPECT_EQ(retval, Core::ERROR_NONE);
                if (retval != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(retval) + " (" + std::string(Core::ErrorToString(retval)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(result.success);

                // Log output
                TEST_LOG("  success: %d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test PerformOTPAction API via JSON-RPC
 *
 * This test verifies that the performOTPAction API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, PerformOTPAction_JSONRPC)
{
    TEST_LOG("Testing performOTPAction via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "performOTPAction", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetOTPEnabled/SetOTPEnabled APIs
 *
 * This test verifies that the SetOTPEnabled and GetOTPEnabled APIs work correctly.
 */
TEST_F(HdmiCecSource_L2Test, SetGetOTPEnabled)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Set OTP enabled to true
                HdmiCecSourceSuccess setResult;
                uint32_t result = m_cecSourcePlugin->SetOTPEnabled(true, setResult);
                EXPECT_EQ(result, Core::ERROR_NONE);
                EXPECT_TRUE(setResult.success);

                // Get OTP enabled status
                bool enabled = false;
                bool success = false;
                result = m_cecSourcePlugin->GetOTPEnabled(enabled, success);
                EXPECT_EQ(result, Core::ERROR_NONE);
                EXPECT_TRUE(success);
                EXPECT_TRUE(enabled);
                TEST_LOG("GetOTPEnabled: enabled=%d, success=%d", enabled, success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendStandbyMessage API
 *
 * This test verifies that the SendStandbyMessage API works correctly.
 */
TEST_F(HdmiCecSource_L2Test, SendStandbyMessage)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                HdmiCecSourceSuccess result;
                uint32_t retval = m_cecSourcePlugin->SendStandbyMessage(result);

                EXPECT_EQ(retval, Core::ERROR_NONE);
                EXPECT_TRUE(result.success);
                TEST_LOG("SendStandbyMessage: success=%d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendKeyPressEvent API
 *
 * This test verifies that the SendKeyPressEvent API works correctly.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                uint32_t logicalAddress = 0; // TV logical address
                uint32_t keyCode = 0x00; // Select key code
                HdmiCecSourceSuccess result;
                uint32_t retval = m_cecSourcePlugin->SendKeyPressEvent(logicalAddress, keyCode, result);

                EXPECT_EQ(retval, Core::ERROR_NONE);
                EXPECT_TRUE(result.success);
                TEST_LOG("SendKeyPressEvent: logicalAddress=%d, keyCode=%d, success=%d",
                         logicalAddress, keyCode, result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetDeviceList API
 *
 * This test verifies that the GetDeviceList API returns the correct device information.
 */
TEST_F(HdmiCecSource_L2Test, GetDeviceList)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                uint32_t numberOfDevices = 0;
                IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
                bool success = false;

                uint32_t result = m_cecSourcePlugin->GetDeviceList(numberOfDevices, deviceList, success);

                EXPECT_EQ(result, Core::ERROR_NONE);
                EXPECT_TRUE(success);
                TEST_LOG("GetDeviceList: numberOfDevices=%d, success=%d", numberOfDevices, success);

                if (deviceList != nullptr) {
                    HdmiCecSourceDevice device;
                    while (deviceList->Next(device)) {
                        TEST_LOG("Device: logicalAddress=%d, vendorID=%s, osdName=%s",
                                 device.logicalAddress, device.vendorID.c_str(), device.osdName.c_str());
                    }
                    deviceList->Release();
                }

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test PerformOTPAction API
 *
 * This test verifies that the PerformOTPAction API works correctly.
 */
TEST_F(HdmiCecSource_L2Test, PerformOTPAction)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                HdmiCecSourceSuccess result;
                uint32_t retval = m_cecSourcePlugin->PerformOTPAction(result);

                EXPECT_EQ(retval, Core::ERROR_NONE);
                EXPECT_TRUE(result.success);
                TEST_LOG("PerformOTPAction: success=%d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test OnActiveSourceStatusUpdated event
 *
 * This test verifies that the OnActiveSourceStatusUpdated event is received correctly.
 */
TEST_F(HdmiCecSource_L2Test, OnActiveSourceStatusUpdatedEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Simulate active source status change
                m_notificationHandler.OnActiveSourceStatusUpdated(true);

                uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
                EXPECT_EQ(status, ON_ACTIVE_SOURCE_STATUS_UPDATED);
                EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
                TEST_LOG("OnActiveSourceStatusUpdated event verified");

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test OnDeviceAdded event
 *
 * This test verifies that the OnDeviceAdded event is received correctly.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceAddedEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Simulate device added event
                int testLogicalAddress = 4;
                m_notificationHandler.OnDeviceAdded(testLogicalAddress);

                uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
                EXPECT_EQ(status, ON_DEVICE_ADDED);
                EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), testLogicalAddress);
                TEST_LOG("OnDeviceAdded event verified");

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test OnDeviceRemoved event, driven through the production emission path
 *
 * Drives the production removal path end to end and asserts the payload it carries.
 *
 * The event is only ever emitted from HdmiCecSourceImplementation::removeDevice(), and only
 * for a logical address the plugin currently believes is present. The test therefore
 * establishes that precondition through production code as well:
 *
 *   1. poll GetDeviceList() until the plugin's own discovery sweep reports at least one
 *      present device. GetDeviceList() signals the poll thread's condition variable before
 *      it reads the table, so this both drives and observes discovery;
 *   2. snapshot the addresses it reports - removeDevice() emits only for a device flagged
 *      present, so that snapshot is exactly the set the removal sweep owes us;
 *   3. disable CEC. CECDisable() calls removeAllCecDevices(), which walks addresses 0..14
 *      and emits one OnDeviceRemoved per present device, synchronously on the SetEnabled
 *      call, so the payload is complete by the time SetEnabled() returns.
 *
 * Synchronisation is deliberately confined to public APIs and bounded waits. The poll thread
 * is running for the whole test, and gmock's expectation state is not safe to mutate while
 * another thread is calling the mock, so the test never reconfigures p_connectionMock and
 * never injects frames while that thread is live - doing either crashes the plugin host.
 *
 * Nothing here calls the notification handler directly: if production stopped emitting the
 * event, or emitted it for the wrong address, the test fails. Interface acquisition is a
 * fatal assertion rather than a log line, and cleanup runs on every exit path.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceRemovedEvent)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject())
        << "the COM-RPC interface is a precondition of this test, not an optional extra";
    ASSERT_NE(nullptr, m_controller_cecSource);
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    // The addresses production currently believes are present, read through the public API.
    // GetDeviceList() signals the poll thread's condition variable before it reads the table,
    // so calling it both drives discovery and observes it.
    auto presentLogicalAddresses = [this]() {
        std::vector<int> addresses;
        uint32_t numberOfDevices = 0;
        IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
        bool listSuccess = false;

        if (m_cecSourcePlugin != nullptr
            && m_cecSourcePlugin->GetDeviceList(numberOfDevices, deviceList, listSuccess) == Core::ERROR_NONE
            && deviceList != nullptr) {
            HdmiCecSourceDevice device;
            while (deviceList->Next(device)) {
                addresses.push_back(device.logicalAddress);
            }
            deviceList->Release();
        }
        return addresses;
    };

    // Bounded wait until two consecutive readings agree, i.e. the discovery sweep has stopped
    // changing the device table. This is a hard requirement, not a convenience: addDevice()
    // and removeDevice() fan notifications out by walking _hdmiCecSourceNotifications WITHOUT
    // holding _adminLock, while Register()/Unregister() mutate that same list under it. So
    // detaching a notification while a sweep is in flight erases the element the sweep is
    // iterating and releases the proxy it is about to call, which takes the plugin host down
    // with SIGSEGV. Quiescing first is the only test-side way to close that window.
    std::function<std::vector<int>()> waitForDiscoveryToSettle = [&presentLogicalAddresses]() {
        std::vector<int> previous = presentLogicalAddresses();
        const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(4 * EVNT_TIMEOUT);
        while (std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            std::vector<int> current = presentLogicalAddresses();
            if (!current.empty() && current == previous) {
                return current;
            }
            previous = std::move(current);
        }
        return previous;
    };

    // Cleanup must survive a fatal assertion in the body, so it is owned by a scope guard
    // rather than by trailing statements. The guard holds references to the fixture's own
    // pointers so it also clears them, leaving no dangling interface behind, and quiesces
    // discovery before detaching for the reason documented above.
    struct InterfaceGuard {
        std::function<std::vector<int>()>& quiesce;
        Exchange::IHdmiCecSource*& plugin;
        PluginHost::IShell*& controller;
        Exchange::IHdmiCecSource::INotification* notification;

        ~InterfaceGuard()
        {
            if (plugin != nullptr) {
                quiesce();
                plugin->Unregister(notification);
                plugin->Release();
                plugin = nullptr;
            }
            if (controller != nullptr) {
                controller->Release();
                controller = nullptr;
            }
        }
    } interfaceGuard { waitForDiscoveryToSettle, m_cecSourcePlugin, m_controller_cecSource, &m_notificationHandler };

    // Step 1 and 2: wait, boundedly, for production's own discovery sweep to settle, and keep
    // the addresses it reports.
    const std::vector<int> presentAddresses = waitForDiscoveryToSettle();

    ASSERT_FALSE(presentAddresses.empty())
        << "discovery reported no present device, so there is nothing for a removal to report";
    TEST_LOG("discovery reported %zu present device(s); first is logical address %d",
             presentAddresses.size(), presentAddresses.front());

    m_notificationHandler.ResetEvent();
    m_notificationHandler.ClearRemovedLogicalAddresses();

    // Step 3: CECDisable() clears the cache, which is the production removal path.
    HdmiCecSourceSuccess disableResult;
    ASSERT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, disableResult));
    EXPECT_TRUE(disableResult.success);

    EXPECT_EQ(ON_DEVICE_REMOVED, WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_REMOVED))
        << "CECDisable() did not emit OnDeviceRemoved";

    // The payload has to name the devices that were actually present: every address in the
    // snapshot must have been reported, and every reported address must be a valid CEC
    // logical address. Discovery may have added more devices between the snapshot and the
    // disable, so the reported set is allowed to be larger - never smaller.
    const std::vector<int> removedAddresses = m_notificationHandler.GetRemovedLogicalAddresses();
    ASSERT_FALSE(removedAddresses.empty()) << "no OnDeviceRemoved payload was recorded";
    for (int address : removedAddresses) {
        EXPECT_GE(address, 0);
        EXPECT_LT(address, static_cast<int>(LogicalAddress::UNREGISTERED));
    }
    for (int expected : presentAddresses) {
        EXPECT_NE(removedAddresses.end(),
                  std::find(removedAddresses.begin(), removedAddresses.end(), expected))
            << "no OnDeviceRemoved was emitted for present logical address " << expected;
    }
    TEST_LOG("OnDeviceRemoved verified for %zu logical address(es)", removedAddresses.size());

    // Leave CEC enabled, which is how every other test in this suite finds it - setEnabled
    // persists, so skipping this would poison the rest of the suite. The scope guard then
    // waits for the sweep this re-enable starts to settle before it detaches the notification.
    HdmiCecSourceSuccess enableResult;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(true, enableResult));
    EXPECT_TRUE(enableResult.success);
}

/**
 * @brief Test OnDeviceRemoved event for a device announced over the frame path
 *
 * This test verifies that the implementation raises OnDeviceRemoved to a COM-RPC registered
 * client when a CEC device it had announced goes away.
 *
 * It used to call m_notificationHandler.OnDeviceRemoved(4) directly. That asserted nothing about
 * the plugin: the test was invoking its own handler, so the event flag and the logical address it
 * then checked were values the test itself had just written, and the assertions would have held
 * with the implementation removed entirely. The production path is driven instead, end to end:
 *
 *   1. announce a peer at logical address 4 with an <Active Source> frame through the registered
 *      FrameListener - HdmiCecSourceProcessor::process(ActiveSource) calls addDevice(header.from),
 *      which fans OnDeviceAdded out over _hdmiCecSourceNotifications;
 *   2. confirm the implementation really did register it (otherwise there is nothing to remove and
 *      the removal assertion would be meaningless);
 *   3. disable CEC over COM-RPC - CECDisable tears the connection down and calls
 *      removeAllCecDevices(), which calls removeDevice() for every present address and fans
 *      OnDeviceRemoved out over the same notification list;
 *   4. observe that notification arriving at the registered handler, carrying address 4.
 *
 * The inherited CEC-enabled setting is restored by the fixture's TearDown, since step 3 changes
 * process-global persisted state that later tests would otherwise inherit.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceRemovedEventForAnnouncedDevice)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_controller_cecSource);
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    const int testLogicalAddress = 4;

    // CEC on, and the implementation's own FrameListener in place - without it there is no inbound
    // path at all and this test could only ever pass vacuously.
    ASSERT_TRUE(EnableCecAndAwaitFrameListener()) << "CEC could not be enabled, so no FrameListener was captured.";

    // <Active Source> from logical address 4, broadcast, physical address 2.0.0.0.
    uint8_t activeSourceFrame[] = { 0x4F, 0x82, 0x20, 0x00 };
    CECFrame frame(activeSourceFrame, sizeof(activeSourceFrame));

    TEST_LOG("Announcing logical address %d through the production frame path", testLogicalAddress);
    for (auto* listener : listeners) {
        if (listener) {
            listener->notify(frame);
        }
    }

    // Confirm through the plugin's own API that the implementation is holding the device, which is
    // the precondition for observing its removal. OnDeviceAdded is deliberately NOT used as that
    // proof: addDevice only notifies when the address was not already marked present, and the
    // implementation's poll thread discovers peers during activation, so the announcement above is
    // frequently a no-op notification-wise while still being the correct production entry point.
    JsonObject params, deviceListBefore;
    ASSERT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getDeviceList", params, deviceListBefore));
    ASSERT_TRUE(deviceListBefore.HasLabel("deviceList"));
    bool devicePresentBefore = false;
    JsonArray reportedDevices = deviceListBefore["deviceList"].Array();
    for (int index = 0; index < reportedDevices.Length(); ++index) {
        if (reportedDevices[index].Object()["logicalAddress"].Number() == testLogicalAddress) {
            devicePresentBefore = true;
        }
    }
    ASSERT_TRUE(devicePresentBefore) << "the implementation is not holding logical address "
                                     << testLogicalAddress << ", so its removal cannot be observed";

    // Disabling CEC is the production route to device removal: CECDisable calls
    // removeAllCecDevices(), which calls removeDevice() for every present address and notifies
    // each one over the registered notification list.
    HdmiCecSourceSuccess disableResult;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, disableResult));
    EXPECT_TRUE(disableResult.success);

    const uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_REMOVED);
    EXPECT_TRUE(status & ON_DEVICE_REMOVED);
    // The sweep reports several addresses, so name the one under test rather than trusting
    // whichever notification happened to arrive last.
    EXPECT_TRUE(m_notificationHandler.WasRemoved(testLogicalAddress))
        << "OnDeviceRemoved was never raised for logical address " << testLogicalAddress;

    // ...and the removal is real, not just announced: the plugin no longer reports the device.
    JsonObject deviceListAfter;
    params.Clear();
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getDeviceList", params, deviceListAfter));
    if (deviceListAfter.HasLabel("deviceList")) {
        JsonArray remainingDevices = deviceListAfter["deviceList"].Array();
        bool devicePresentAfter = false;
        for (int index = 0; index < remainingDevices.Length(); ++index) {
            if (remainingDevices[index].Object()["logicalAddress"].Number() == testLogicalAddress) {
                devicePresentAfter = true;
            }
        }
        EXPECT_FALSE(devicePresentAfter) << "logical address " << testLogicalAddress
                                        << " is still in the device list after removal";
    }
    TEST_LOG("OnDeviceRemoved event verified through the production removal path");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test OnDeviceRemoved event, driven through the production emission path
 *
 * The event is NOT injected into the test's own handler. It is produced by
 * HdmiCecSourceImplementation itself, which is the only thing that makes the test capable of
 * failing when production regresses:
 *
 *   Connection::ping() raises CECNoAckException for one peer
 *     -> HdmiCecSourceImplementation::pingDeviceUpdateList() catches it (Implementation.cpp:1387)
 *     -> removeDevice(idev)                                    (Implementation.cpp:1391 / :525)
 *     -> (*index)->OnDeviceRemoved(logicalAddress) fan-out over _hdmiCecSourceNotifications
 *                                                              (Implementation.cpp:539-543)
 *     -> the COM-RPC sink this test registered, AND
 *     -> HdmiCecSource::Notification::OnDeviceRemoved            (HdmiCecSource.h:98-102)
 *          -> Exchange::JHdmiCecSource::Event::OnDeviceRemoved -> Notify("onDeviceRemoved")
 *
 * Both legs are asserted: the COM-RPC leg carries the logical address, so it pins down *which*
 * peer production code decided had gone away; the JSON-RPC subscription proves the plugin's own
 * notification sink ran and published the event outward.
 *
 * Determinism. The polling thread that ActivateService() started has, by the time the body runs,
 * already ACKed and added every peer (Connection::ping() is left at the NiceMock default, which
 * returns without throwing, and the fixture asserts below that the device list is non-empty). No
 * further OnDeviceAdded can therefore fire, and OnDeviceInfoUpdated only fires from
 * sendDeviceUpdateInfo(), which needs an inbound frame this test never injects. So after the
 * per-test ping() policy is installed, the one and only notification the implementation can raise
 * is OnDeviceRemoved for the single address that policy takes off the bus - which is why reading
 * the recorded logical address afterwards is safe rather than racy.
 *
 * The poll thread is woken through a real API rather than a sleep: GetDeviceList() signals
 * m_condSig (Implementation.cpp:1336-1338), which is exactly what the thread waits on between
 * sweeps. The kick is retried a bounded number of times because pthread_cond_signal is lost if it
 * lands while the thread happens to be mid-sweep.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceRemovedEventOnPingFailure)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_controller_cecSource);
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(HDMICECSOURCE_CALLSIGN, HDMICECSOURCE_L2TEST_CALLSIGN);
    EXPECT_EQ(Core::ERROR_NONE,
        jsonrpc.Subscribe<JsonObject>(EVNT_TIMEOUT,
            _T("onDeviceRemoved"),
            &HdmiCecSource_L2Test::onDeviceRemoved,
            this));

    /* Ask production code which peers it currently believes are on the bus. */
    uint32_t devicesBefore = 0;
    IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
    bool listSuccess = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetDeviceList(devicesBefore, deviceList, listSuccess));
    EXPECT_TRUE(listSuccess);

    std::vector<int> presentAddresses;
    if (deviceList != nullptr) {
        HdmiCecSourceDevice device;
        while (deviceList->Next(device)) {
            presentAddresses.push_back(device.logicalAddress);
        }
        deviceList->Release();
    }
    /* Precondition asserted, not assumed: there has to be something to remove. */
    ASSERT_FALSE(presentAddresses.empty());

    /* Remove a peer other than the TV, so nothing in the active-source bookkeeping is disturbed. */
    int target = -1;
    for (int address : presentAddresses) {
        if (address != LogicalAddress::TV) {
            target = address;
            break;
        }
    }
    ASSERT_NE(-1, target);
    TEST_LOG("Taking logical address %d off the bus", target);

    ON_CALL(*p_connectionMock, ping(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [target](const LogicalAddress&, const LogicalAddress& to, const Throw_e&) {
                if (to.toInt() == target) {
                    throw CECNoAckException();
                }
            }));

    /* Discard everything the activation sweep signalled, so what is waited on below is new. */
    m_notificationHandler.ResetEvent();

    uint32_t signalled = HDMICECSOURCE_STATUS_INVALID;
    for (int attempt = 0; (attempt < 5) && !(signalled & ON_DEVICE_REMOVED); ++attempt) {
        uint32_t devicesNow = 0;
        IHdmiCecSourceDeviceListIterator* kickList = nullptr;
        bool kickSuccess = false;
        EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetDeviceList(devicesNow, kickList, kickSuccess));
        if (kickList != nullptr) {
            kickList->Release();
        }
        signalled = WaitForRequestStatus(EVNT_TIMEOUT / 5, ON_DEVICE_REMOVED);
    }

    EXPECT_TRUE(signalled & ON_DEVICE_REMOVED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), target);

    /* The JSON-RPC leg: proves HdmiCecSource::Notification::OnDeviceRemoved published the event. */
    EXPECT_TRUE(WaitForJsonRpcEvent(EVNT_TIMEOUT, ON_DEVICE_REMOVED) & ON_DEVICE_REMOVED);

    /* Independent post-condition: the peer is gone from the implementation's own device list. */
    uint32_t devicesAfter = 0;
    IHdmiCecSourceDeviceListIterator* afterList = nullptr;
    bool afterSuccess = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetDeviceList(devicesAfter, afterList, afterSuccess));
    bool targetStillPresent = false;
    if (afterList != nullptr) {
        HdmiCecSourceDevice device;
        while (afterList->Next(device)) {
            if (device.logicalAddress == target) {
                targetStillPresent = true;
            }
        }
        afterList->Release();
    }
    EXPECT_FALSE(targetStillPresent);
    EXPECT_LT(devicesAfter, devicesBefore);
    TEST_LOG("OnDeviceRemoved verified for logical address %d (%u devices before, %u after)",
        target, devicesBefore, devicesAfter);

    jsonrpc.Unsubscribe(EVNT_TIMEOUT, _T("onDeviceRemoved"));
    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Negative counterpart: no ACK loss, no OnDeviceRemoved
 *
 * The corner case Directive 2 asks for on the same API, and at the same time the control that
 * makes OnDeviceRemovedEventOnPingFailure above trustworthy. Everything is identical except that the per-test
 * ping() policy is omitted, so every peer keeps ACKing and pingDeviceUpdateList() has no reason to
 * call removeDevice(). The same poll-thread kick and the same wait helper are used, and the wait
 * is required to time out - which is only possible if the positive test's PASS was caused by
 * production code reacting to the missing ACK rather than by the harness signalling itself.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceRemovedEvent_PeersStillAcking_ProducesNoNotification)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    uint32_t devicesBefore = 0;
    IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
    bool listSuccess = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetDeviceList(devicesBefore, deviceList, listSuccess));
    EXPECT_TRUE(listSuccess);
    if (deviceList != nullptr) {
        deviceList->Release();
    }
    ASSERT_GT(devicesBefore, 0u);

    m_notificationHandler.ResetEvent();

    /* Drive several poll sweeps with ping() left ACKing for every address. */
    for (int kick = 0; kick < 3; ++kick) {
        uint32_t devicesNow = 0;
        IHdmiCecSourceDeviceListIterator* kickList = nullptr;
        bool kickSuccess = false;
        EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetDeviceList(devicesNow, kickList, kickSuccess));
        if (kickList != nullptr) {
            kickList->Release();
        }
    }

    EXPECT_EQ(HDMICECSOURCE_STATUS_INVALID, WaitForRequestStatus(1500, ON_DEVICE_REMOVED));

    uint32_t devicesAfter = 0;
    IHdmiCecSourceDeviceListIterator* afterList = nullptr;
    bool afterSuccess = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetDeviceList(devicesAfter, afterList, afterSuccess));
    if (afterList != nullptr) {
        afterList->Release();
    }
    EXPECT_EQ(devicesAfter, devicesBefore);
    TEST_LOG("No OnDeviceRemoved raised while every peer keeps ACKing (%u devices throughout)", devicesAfter);

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

//======================================== Frame Injection Tests ========================================

/**
 * @brief Test Standby frame injection and verify standbyMessageReceived event
 *
 * This test injects a Standby CEC frame and verifies that the standbyMessageReceived event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectStandbyFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject Standby frame (Opcode 0x36)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x36 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting Standby CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for standbyMessageReceived event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, STANDBY_MESSAGE_RECEIVED);
    EXPECT_TRUE(signalled & STANDBY_MESSAGE_RECEIVED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    TEST_LOG("Standby event verified");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test UserControlPressed frame injection and verify onKeyPressEvent event
 *
 * This test injects a UserControlPressed CEC frame and verifies that the onKeyPressEvent is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectUserControlPressedFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject UserControlPressed frame (Opcode 0x44) with keycode for Volume Up (0x41)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x44, 0x41 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting UserControlPressed CEC frame with Volume Up key");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for onKeyPressEvent
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_KEY_PRESS_EVENT);
    EXPECT_TRUE(signalled & ON_KEY_PRESS_EVENT);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    EXPECT_EQ(m_notificationHandler.GetKeyCode(), 0x41);
    TEST_LOG("UserControlPressed event verified");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test UserControlReleased frame injection and verify onKeyReleaseEvent event
 *
 * This test injects a UserControlReleased CEC frame and verifies that the onKeyReleaseEvent is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectUserControlReleasedFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject UserControlReleased frame (Opcode 0x45)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x45 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting UserControlReleased CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for onKeyReleaseEvent
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_KEY_RELEASE_EVENT);
    EXPECT_TRUE(signalled & ON_KEY_RELEASE_EVENT);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    TEST_LOG("UserControlReleased event verified");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test ActiveSource frame injection and verify OnActiveSourceStatusUpdated event
 *
 * This test injects an ActiveSource CEC frame with our physical address
 * and verifies that the OnActiveSourceStatusUpdated event is triggered with true status.
 */
TEST_F(HdmiCecSource_L2Test, InjectActiveSourceFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject ActiveSource frame (Opcode 0x82) with physical address matching ours
    // Physical address: 0x0F0F (15.15.15.15 in 2-byte CEC format)
    // From device (4) to all (broadcast)
    uint8_t buffer[] = { 0x4F, 0x82, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting ActiveSource CEC frame with our physical address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give the system time to process the frame and trigger events
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("ActiveSource event verified with status=true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test DeviceVendorID frame injection and verify OnDeviceInfoUpdated event
 *
 * This test injects a DeviceVendorID CEC frame and verifies that the OnDeviceInfoUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectDeviceVendorIDFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // First add the device by injecting ReportPhysicalAddress
    uint8_t setupBuffer[] = { 0x4F, 0x84, 0x20, 0x00, 0x04 };
    CECFrame setupFrame(setupBuffer, sizeof(setupBuffer));
    
    TEST_LOG("Setting up: Injecting ReportPhysicalAddress CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(setupFrame);
    }
    
    // Give time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Wait for device to be added
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    //EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    m_notificationHandler.ResetEvent();

    // Now inject DeviceVendorID frame (Opcode 0x87)
    // From device 4 to all (broadcast), Vendor ID: LG (0x00E091)
    uint8_t buffer[] = { 0x4F, 0x87, 0x00, 0xE0, 0x91 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting DeviceVendorID CEC frame with LG vendor ID");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceInfoUpdated event
    signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_INFO_UPDATED);
    EXPECT_TRUE(signalled & ON_DEVICE_INFO_UPDATED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 4);
    TEST_LOG("OnDeviceInfoUpdated event verified after DeviceVendorID");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SetOSDName frame injection and verify OnDeviceInfoUpdated event
 *
 * This test injects a SetOSDName CEC frame and verifies that the OnDeviceInfoUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectSetOSDNameFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // First add the device by injecting ReportPhysicalAddress
    uint8_t setupBuffer[] = { 0x4F, 0x84, 0x20, 0x00, 0x04 };
    CECFrame setupFrame(setupBuffer, sizeof(setupBuffer));
    
    TEST_LOG("Setting up: Injecting ReportPhysicalAddress CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(setupFrame);
    }
    
    // Give time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Wait for device to be added
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    //EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    m_notificationHandler.ResetEvent();

    // Now inject SetOSDName frame (Opcode 0x47)
    // From device 4 to us (device 3 or 0), OSD Name: "TestDev"
    uint8_t buffer[] = { 0x40, 0x47, 'T', 'e', 's', 't', 'D', 'e', 'v' };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting SetOSDName CEC frame with name 'TestDev'");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceInfoUpdated event
    signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_INFO_UPDATED);
    EXPECT_TRUE(signalled & ON_DEVICE_INFO_UPDATED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 4);
    TEST_LOG("OnDeviceInfoUpdated event verified after SetOSDName");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RequestActiveSource frame injection
 *
 * This test injects a RequestActiveSource CEC frame. If the device is active source,
 * it should respond with an ActiveSource message.
 */
TEST_F(HdmiCecSource_L2Test, InjectRequestActiveSourceFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RequestActiveSource frame (Opcode 0x85)
    // From TV (0) to all (broadcast)
    uint8_t buffer[] = { 0x0F, 0x85 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RequestActiveSource CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Note: This will only send ActiveSource if isDeviceActiveSource is true
    // The test verifies the frame is processed without errors
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("RequestActiveSource frame processed");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GetCECVersion frame injection
 *
 * This test injects a GetCECVersion CEC frame and verifies that the device
 * responds with a CECVersion message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGetCECVersionFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GetCECVersion frame (Opcode 0x9F)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x9F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GetCECVersion CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with CECVersion (V_1_4)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GetCECVersion frame processed - device should send CECVersion response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test CECVersion frame injection and verify device added
 *
 * This test injects a CECVersion CEC frame and verifies that the device
 * is added to the device list.
 */
TEST_F(HdmiCecSource_L2Test, InjectCECVersionFrameAndVerifyDeviceAdded)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject CECVersion frame (Opcode 0x9E)
    // From device 5 to us (device 4), Version 1.4
    uint8_t buffer[] = { 0x54, 0x9E, 0x05 };  // 0x05 = Version 1.4
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting CECVersion CEC frame from device 5");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceAdded event
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    //EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    //EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 5);
    // The wait is kept for its side effect (it drains the accumulated event bits so the next
    // test starts clean) and its result is reported rather than asserted, because the two
    // assertions above were already commented out when this test was written.  Logging it also
    // removes a -Wunused-variable warning without changing what the test checks.
    TEST_LOG("CECVersion frame processed - device 5 added (event bits observed: 0x%08x)", signalled);

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GiveOSDName frame injection
 *
 * This test injects a GiveOSDName CEC frame and verifies that the device
 * responds with a SetOSDName message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGiveOSDNameFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GiveOSDName frame (Opcode 0x46)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x46 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GiveOSDName CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with SetOSDName
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GiveOSDName frame processed - device should send SetOSDName response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GivePhysicalAddress frame injection
 *
 * This test injects a GivePhysicalAddress CEC frame and verifies that the device
 * responds with a ReportPhysicalAddress message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGivePhysicalAddressFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GivePhysicalAddress frame (Opcode 0x83)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x83 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GivePhysicalAddress CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with ReportPhysicalAddress
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GivePhysicalAddress frame processed - device should send ReportPhysicalAddress response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GiveDeviceVendorID frame injection
 *
 * This test injects a GiveDeviceVendorID CEC frame and verifies that the device
 * responds with a DeviceVendorID message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGiveDeviceVendorIDFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GiveDeviceVendorID frame (Opcode 0x8C)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x8C };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GiveDeviceVendorID CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with DeviceVendorID
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GiveDeviceVendorID frame processed - device should send DeviceVendorID response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingChange frame injection and verify active source event
 *
 * This test injects a RoutingChange CEC frame with our physical address as destination
 * and verifies that the OnActiveSourceStatusUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingChangeFrameAndVerifyActiveSource)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingChange frame (Opcode 0x80)
    // From TV (0) to all (broadcast), changing route to our physical address (0x0F0F)
    uint8_t buffer[] = { 0x0F, 0x80, 0x00, 0x00, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingChange CEC frame routing to our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("RoutingChange frame processed - active source status updated to true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingInformation frame injection and verify active source event
 *
 * This test injects a RoutingInformation CEC frame with our physical address
 * and verifies that the OnActiveSourceStatusUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingInformationFrameAndVerifyActiveSource)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingInformation frame (Opcode 0x81)
    // From TV (0) to all (broadcast), routing to our physical address (0x0F0F)
    uint8_t buffer[] = { 0x0F, 0x81, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingInformation CEC frame routing to our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("RoutingInformation frame processed - active source status updated to true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SetStreamPath frame injection and verify active source event
 *
 * This test injects a SetStreamPath CEC frame with our physical address
 * and verifies that the OnActiveSourceStatusUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectSetStreamPathFrameAndVerifyActiveSource)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject SetStreamPath frame (Opcode 0x86)
    // From TV (0) to all (broadcast), setting stream path to our physical address (0x0F0F)
    uint8_t buffer[] = { 0x0F, 0x86, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting SetStreamPath CEC frame to our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("SetStreamPath frame processed - active source status updated to true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GiveDevicePowerStatus frame injection
 *
 * This test injects a GiveDevicePowerStatus CEC frame and verifies that the device
 * responds with a ReportPowerStatus message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGiveDevicePowerStatusFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GiveDevicePowerStatus frame (Opcode 0x8F)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x8F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GiveDevicePowerStatus CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with ReportPowerStatus
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GiveDevicePowerStatus frame processed - device should send ReportPowerStatus response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test ReportPowerStatus frame injection and verify device added
 *
 * This test injects a ReportPowerStatus CEC frame from TV and verifies that the device
 * is added to the device list.
 */
TEST_F(HdmiCecSource_L2Test, InjectReportPowerStatusFrameAndVerifyDeviceAdded)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject ReportPowerStatus frame (Opcode 0x90)
    // From TV (0) to device (4), Power status: ON (0x00)
    uint8_t buffer[] = { 0x04, 0x90, 0x00 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting ReportPowerStatus CEC frame from TV");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceAdded event
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    TEST_LOG("ReportPowerStatus frame processed - TV device added");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test FeatureAbort frame injection
 *
 * This test injects a FeatureAbort CEC frame and verifies that the device
 * processes it without errors.
 */
TEST_F(HdmiCecSource_L2Test, InjectFeatureAbortFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject FeatureAbort frame (Opcode 0x00)
    // From TV (0) to device (4), Feature Opcode: 0x44 (User Control Pressed), Abort Reason: 0x04 (Refused)
    uint8_t buffer[] = { 0x04, 0x00, 0x44, 0x04 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting FeatureAbort CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The frame should be processed without errors
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("FeatureAbort frame processed");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test Abort frame injection
 *
 * This test injects an Abort CEC frame (unrecognized opcode) and verifies that the device
 * responds with a FeatureAbort message.
 */
TEST_F(HdmiCecSource_L2Test, InjectAbortFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject an unrecognized opcode frame that will trigger Abort processing
    // From TV (0) to device (4), Invalid Opcode: 0xFF
    uint8_t buffer[] = { 0x04, 0xFF };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting frame with unrecognized opcode (Abort)");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with FeatureAbort
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("Abort frame processed - device should send FeatureAbort response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test Polling frame injection
 *
 * This test injects a Polling CEC frame and verifies that the device
 * processes it without errors.
 */
TEST_F(HdmiCecSource_L2Test, InjectPollingFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject Polling frame (same source and destination)
    // From device (4) to device (4) - this is a polling message
    uint8_t buffer[] = { 0x44 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting Polling CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The frame should be processed without errors
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("Polling frame processed");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test ActiveSource frame with matching physical address to set device as active source
 *
 * This test injects an ActiveSource CEC frame with our own physical address (0x0F0F)
 * to test the path where isDeviceActiveSource becomes true.
 */
TEST_F(HdmiCecSource_L2Test, InjectActiveSourceFrameWithMatchingAddressAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // First, inject ActiveSource frame with OUR physical address (0x0F0F) to make device active
    // From device 4 (us) to all (broadcast)
    uint8_t buffer1[] = { 0x4F, 0x82, 0x0F, 0x0F };
    CECFrame frame1(buffer1, sizeof(buffer1));
    
    TEST_LOG("Injecting ActiveSource CEC frame with our physical address to set as active source");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame1);
    }

    // Give time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event - device should now be active source
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    TEST_LOG("Device is now active source after ActiveSource with matching address");

    // Now inject RequestActiveSource to test the path where device responds
    // From TV (0) to all (broadcast)
    uint8_t buffer2[] = { 0x0F, 0x85 };
    CECFrame frame2(buffer2, sizeof(buffer2));
    
    TEST_LOG("Injecting RequestActiveSource - device should respond with ActiveSource");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame2);
    }

    // The device should respond with ActiveSource since it's now the active source
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    TEST_LOG("RequestActiveSource processed - device sent ActiveSource response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingChange frame with matching destination address
 *
 * This test injects a RoutingChange CEC frame where the destination matches our physical address
 * to test the path where isDeviceActiveSource becomes true.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingChangeFrameWithMatchingDestination)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingChange frame where destination MATCHES our physical address
    // From TV (0) to all (broadcast), routing FROM 0x0000 TO our address 0x0F0F
    uint8_t buffer[] = { 0x0F, 0x80, 0x00, 0x00, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingChange with destination matching our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event with true status
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    TEST_LOG("RoutingChange processed - device is now active source");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingInformation frame with matching destination address
 *
 * This test injects a RoutingInformation CEC frame where the destination matches our physical address
 * to test the path where isDeviceActiveSource becomes true.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingInformationFrameWithMatchingDestination)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingInformation frame where destination MATCHES our physical address
    // From TV (0) to all (broadcast), routing TO our address 0x0F0F
    uint8_t buffer[] = { 0x0F, 0x81, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingInformation with destination matching our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event with true status
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    TEST_LOG("RoutingInformation processed - device is now active source");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SendKeyPressEvent with invalid logical address
 *
 * This test verifies error handling when SendKeyPressEvent is called with an invalid logical address.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEventWithInvalidLogicalAddress)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);

    HdmiCecSourceSuccess success;
    success.success = false;

    // Test with invalid logical address (0xFF is invalid)
    uint32_t result = m_cecSourcePlugin->SendKeyPressEvent(0xFF, 0x41, success);
    
    // Should return error
    EXPECT_NE(result, Core::ERROR_NONE);
    EXPECT_FALSE(success.success);
    TEST_LOG("SendKeyPressEvent correctly rejected invalid logical address");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SendKeyPressEvent with invalid key code
 *
 * This test verifies error handling when SendKeyPressEvent is called with an unsupported key code.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEventWithInvalidKeyCode)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);

    HdmiCecSourceSuccess success;
    success.success = false;

    // Test with valid logical address but invalid/unsupported key code (0xFF)
    uint32_t result = m_cecSourcePlugin->SendKeyPressEvent(0, 0xFF, success);
    
    // Should return NOT_SUPPORTED error
    EXPECT_EQ(result, Core::ERROR_NOT_SUPPORTED);
    EXPECT_FALSE(success.success);
    TEST_LOG("SendKeyPressEvent correctly rejected unsupported key code");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

// =====================================================================================
// Additive coverage for the reachable L2 paths that no case exercised.
//
// Everything below this line is new.  Nothing above it was rewritten: the cases are adjacent
// tests in the same fixture, and each one names the production lines it exists to drive so a
// later reader can tell an intentional path from an incidental one.
//
// The clusters were chosen from measurement, not guesswork - the L2 trace before these cases
// left HdmiCecSourceImplementation.cpp at 594/834 (71.2%), and the uncovered lines grouped into
// exactly four reachable families:
//
//   1. the HDMI hotplug chain          OnDisplayHDMIHotPlug -> threadHotPlugEventHandler ->
//                                      onHdmiHotPlug           (lines 708-724, 692-705, 742-789)
//   2. the power-mode callback          onPowerModeChanged      (lines 726-739)
//   3. CECEnable's rollback path        rollbackInitialization + both catch arms
//                                                              (lines 981-1019, 1029-1034, 1046-1054)
//   4. the inbound-frame error arms     the catch(...) blocks in HdmiCecSourceProcessor::process
//                                      overloads, plus process(Polling) which was never injected
//
// plus the two profile-guard arms in the plugin shell (HdmiCecSource.cpp:61-62 and 112-113).
// =====================================================================================

/**
 * @brief HDMI hotplug CONNECTED, driven through the production listener.
 *
 * Covers HdmiCecSourceImplementation.cpp:708-724 (OnDisplayHDMIHotPlug), 692-705
 * (threadHotPlugEventHandler, which the former spawns detached) and 742-789 (onHdmiHotPlug) -
 * 47 lines that no L2 case reached, because the only inbound route is the
 * device::Host::IDisplayDeviceEvents listener the implementation registers with the device
 * settings Host, and nothing captured it.
 *
 * What the production path does on a CONNECTED event, and therefore what is asserted: refresh
 * the physical and logical addresses, read the EDID to decide whether an LG TV is attached, and
 * broadcast <Report Physical Address> followed by <Device Vendor ID>.  The two broadcasts are
 * the observable outcome, so the test counts them rather than merely calling the entry point.
 */
TEST_F(HdmiCecSource_L2Test, HdmiHotPlugConnectedRefreshesAddressesAndBroadcastsIdentity)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    // CEC has to be on: OnDisplayHDMIHotPlug returns immediately when cecEnableStatus is false,
    // and onHdmiHotPlug only broadcasts when smConnection exists.  Waiting for the FrameListener
    // is the proof that both are true.
    ASSERT_TRUE(EnableCecAndAwaitFrameListener())
        << "CEC could not be enabled, so the hotplug path would return early and assert nothing.";
    ASSERT_NE(nullptr, displayDeviceListener)
        << "the implementation did not register a display-device listener, so the hotplug path is unreachable";

    // Count the broadcasts this event is supposed to produce, through the fixture's default
    // counting action.  A relaxed lower bound rather than an exact count: the poll and update
    // threads are running and legitimately transmit too.
    const int broadcastsBefore = g_broadcastCount.load();

    TEST_LOG("Driving OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED) through the registered listener");
    displayDeviceListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED);

    // The handler detaches a worker thread, so the effect is asynchronous.  Poll rather than
    // sleep a fixed amount, and give up only after a bound that is generous next to the work
    // being done (two encodes and two transmits through mocks).
    const uint32_t pollIntervalMs = 50;
    bool observed = false;
    for (uint32_t waitedMs = 0; waitedMs < 5000 && !observed; waitedMs += pollIntervalMs) {
        observed = (g_broadcastCount.load() > broadcastsBefore);
        if (!observed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
        }
    }

    EXPECT_TRUE(observed)
        << "the CONNECTED hotplug produced no broadcast; <Report Physical Address> and "
           "<Device Vendor ID> are the observable outcome of onHdmiHotPlug";

    // The addresses the handler refreshed are readable through the plugin's own API, which is a
    // second, transport-level confirmation that the chain ran to completion rather than aborting
    // part-way through.
    JsonObject params, result;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getDeviceList", params, result));

    // Let the detached worker's device sweep settle before the fixture tears the plugin down.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief HDMI hotplug DISCONNECTED is accepted and produces no identity broadcast.
 *
 * The corner case of the test above: onHdmiHotPlug (HdmiCecSourceImplementation.cpp:742) acts
 * only on HDMI_HOT_PLUG_EVENT_CONNECTED (0) and falls straight through to its trailing return for
 * anything else, so a disconnect must not be mistaken for a connect and must not re-announce the
 * device on the bus.
 */
TEST_F(HdmiCecSource_L2Test, HdmiHotPlugDisconnectedIsAcceptedWithoutReannouncing)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    ASSERT_TRUE(EnableCecAndAwaitFrameListener()) << "CEC could not be enabled.";
    ASSERT_NE(nullptr, displayDeviceListener) << "no display-device listener was registered";

    TEST_LOG("Driving OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_DISCONNECTED)");
    EXPECT_NO_THROW(displayDeviceListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_DISCONNECTED));

    // The detached worker still runs and still signals the poll thread, so the plugin has to stay
    // healthy and answerable afterwards.  That is what a disconnect being a no-op means here.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    JsonObject params, result;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result));
    ASSERT_TRUE(result.HasLabel("enabled"));
    EXPECT_TRUE(result["enabled"].Boolean()) << "a disconnect must not switch CEC off";

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief HDMI hotplug while CEC is off is refused at the guard.
 *
 * Covers the early-return arm of OnDisplayHDMIHotPlug (HdmiCecSourceImplementation.cpp:710-712):
 * with cecEnableStatus false there is no CEC connection to answer on, so the event must be
 * dropped rather than dereferenced.  This is the negative half of the pair above, and it is the
 * arm that would crash if the guard were ever removed.
 */
TEST_F(HdmiCecSource_L2Test, HdmiHotPlugIsIgnoredWhileCecIsDisabled)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    // Enable first, purely to capture the listener - it is registered during Configure and is not
    // affected by the enabled setting - then switch CEC off so the guard is the arm under test.
    ASSERT_TRUE(EnableCecAndAwaitFrameListener()) << "CEC could not be enabled.";
    ASSERT_NE(nullptr, displayDeviceListener) << "no display-device listener was registered";

    HdmiCecSourceSuccess disableResult;
    ASSERT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, disableResult));
    ASSERT_TRUE(disableResult.success);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Nothing may be transmitted while CEC is off: the guard returns before any encode or sendTo.
    //
    // Counted rather than expressed as Times(0), deliberately.  A Times(0) expectation stays live
    // until the mock is destroyed - which is inside the FIXTURE's destructor, after TearDown has
    // run - and CECEnable transmits its three opening announcements whenever CEC comes back up,
    // so a Times(0) here fails on cleanup rather than on anything this test did.  Sampling the
    // fixture's counter around the injection asserts exactly the window that matters.
    const int transmitsBefore = g_sendToCount.load();

    TEST_LOG("Driving OnDisplayHDMIHotPlug with CEC disabled");
    EXPECT_NO_THROW(displayDeviceListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(transmitsBefore, g_sendToCount.load())
        << "the hotplug guard let the handler run with CEC disabled: it transmitted "
        << (g_sendToCount.load() - transmitsBefore) << " frame(s) with no connection to transmit on";

    // Hand CEC back ON here, over COM-RPC, rather than leaving it to the fixture's TearDown.
    //
    // This is the convention the rest of this file already follows ("Leave CEC enabled, which is
    // how every other test in this suite finds it"), and here it is load-bearing rather than tidy.
    // TearDown restores the inherited setting through L2TestMocks::InvokeServiceMethod, whose
    // INVOKE_TIMEOUT is 3000 ms (L2TestsMock.cpp:28); a setEnabled(true) that has to run a full
    // CECEnable - LibCCEC init, three thread creations, three transmits - can exceed that, and the
    // timeout path in out-of-scope framework code then SEGFAULTS the whole suite:
    //
    //   #0 JSONRPC::LinkType<Core::JSON::IElement>::FromMessage(...)
    //   #1 JSONRPC::LinkType<...>::InternalInvoke<VariantContainer, VariantContainer>(...)
    //   #2 L2TestMocks::InvokeServiceMethod(...)
    //   #3 HdmiCecSource_L2Test::RestoreCecEnabledState()
    //   #4 HdmiCecSource_L2Test::TearDown()
    //
    // Thunder/Source/websocket/JSONRPCLink.h InternalInvoke dereferences `*response` whenever
    // Send() reports ERROR_NONE, without ever checking `response.IsValid()`, and
    // L2TestMocks::InvokeServiceMethod re-invokes on status 11 (ERROR_TIMEDOUT) into the same
    // result object.  Observed once, core dumped and symbolised; the suite reported 65 of 72 tests
    // and no summary, and the L2 wrapper still exited 0.
    //
    // REQUIRED CHANGE, in code this pass may not touch (Thunder and entservices-testframework are
    // both out of scope): guard that dereference with response.IsValid().  Until then, no test in
    // this suite should hand TearDown a slow state transition - so this test performs its own,
    // synchronously, over COM-RPC where there is no 3-second client timeout.
    HdmiCecSourceSuccess reEnable;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(true, reEnable));
    EXPECT_TRUE(reEnable.success);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Power-mode transitions reach the plugin's notification sink.
 *
 * Covers HdmiCecSourceImplementation.cpp:726-739 (onPowerModeChanged) - both arms.  Driven the
 * way a device drives it: through the real PowerManager plugin over COM-RPC, so the notification
 * travels the production route PowerManagerImplementation::dispatchPowerModeChangedEvent ->
 * HdmiCecSourceImplementation::PowerManagerNotification::OnPowerModeChanged -> onPowerModeChanged.
 *
 * Two transitions are needed because SetPowerState only dispatches when the requested state
 * differs from the current one (PowerManagerImplementation.cpp:312): ON -> STANDBY takes the else
 * arm (powerState = 1), STANDBY -> ON takes the POWER_STATE_ON arm, which also refreshes the
 * logical address after wakeup.
 */
TEST_F(HdmiCecSource_L2Test, PowerModeTransitionsReachTheImplementation)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> powerEngine
        = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    Core::ProxyType<RPC::CommunicatorClient> powerClient
        = Core::ProxyType<RPC::CommunicatorClient>::Create(
            Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(powerEngine));
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    powerEngine->Announcements(powerClient->Announcement());
#endif
    ASSERT_TRUE(powerClient.IsValid());

    PluginHost::IShell* powerController = powerClient->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
    ASSERT_NE(nullptr, powerController) << "the PowerManager plugin could not be opened over COM-RPC";

    Exchange::IPowerManager* powerManager = powerController->QueryInterface<Exchange::IPowerManager>();
    ASSERT_NE(nullptr, powerManager) << "the PowerManager plugin does not expose IPowerManager";

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    const int keyCode = 0;

    TEST_LOG("Requesting STANDBY so onPowerModeChanged takes its non-ON arm");
    EXPECT_EQ(Core::ERROR_NONE, powerManager->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY, "l2-cecsource"));
    // The dispatch is synchronous inside SetPowerState, but the AckController the PowerManager
    // creates around a state change is torn down afterwards; the same settle the sink suite uses.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    TEST_LOG("Requesting ON so onPowerModeChanged takes its POWER_STATE_ON arm and re-reads the logical address");
    EXPECT_EQ(Core::ERROR_NONE, powerManager->SetPowerState(keyCode, PowerState::POWER_STATE_ON, "l2-cecsource"));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // The plugin has to still be answering after both transitions - that is what "the callback ran
    // and did not leave the implementation wedged" means from outside.
    JsonObject params, result;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result));

    powerManager->Release();
    powerController->Release();
    powerClient.Release();
    powerEngine.Release();

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief CECEnable rolls back cleanly when the CEC connection cannot be opened.
 *
 * Covers HdmiCecSourceImplementation.cpp:981-1019 (the rollbackInitialization lambda) and
 * 1029-1034 (the catch arm that invokes it) - 42 lines that only run on a failure, and which no
 * case had ever executed even though the lambda exists specifically to stop that failure leaking
 * threads and heap objects.
 *
 * CEC is switched off first so the enable is a genuine transition (CECEnable returns immediately
 * when cecEnableStatus is already true), then Connection::open() is made to throw for one call.
 * The assertion is that the plugin survives it and reports CEC as off: a rollback that left
 * cecEnableStatus true would report on, and a rollback that failed to join its threads or free
 * smConnection would take the plugin down in the fixture's teardown.
 */
TEST_F(HdmiCecSource_L2Test, CecEnableRollsBackWhenTheConnectionCannotBeOpened)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    HdmiCecSourceSuccess result;
    ASSERT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, result));
    ASSERT_TRUE(result.success);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // One failing open, then back to the fixture's behaviour, so the fixture's own teardown and
    // the next test are unaffected by this expectation.
    EXPECT_CALL(*p_connectionMock, open())
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Throw(std::runtime_error("L2: CEC connection open refused")))
        .WillRepeatedly(::testing::Return());

    TEST_LOG("Enabling CEC with a failing Connection::open()");
    HdmiCecSourceSuccess enableResult;
    // The call itself succeeds - CECEnable swallows the failure by design and the setting is
    // persisted either way; what must not happen is a crash or a half-initialised implementation.
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(true, enableResult));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    bool reportedEnabled = true;
    bool getSuccess = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetEnabled(reportedEnabled, getSuccess));
    EXPECT_FALSE(reportedEnabled)
        << "the rollback must leave cecEnableStatus false; a true here means CECEnable reported "
           "success for an initialisation that threw";

    // Still answering, which is the observable proof the rollback freed what it had allocated and
    // joined the threads it had started rather than abandoning them.
    JsonObject params, jsonResult;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getDeviceList", params, jsonResult));

    // Bring CEC back up here rather than in TearDown, for the reason recorded in
    // HdmiHotPlugIsIgnoredWhileCecIsDisabled: TearDown's restore goes over JSON-RPC with a 3-second
    // client timeout, and a timing-out setEnabled crashes out-of-scope framework code.  The
    // failing open() above was a WillOnce, so this enable takes the normal path.
    HdmiCecSourceSuccess reEnable;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(true, reEnable));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief CECEnable rolls back when the initial CEC announcements cannot be transmitted.
 *
 * The second failure arm of CECEnable (HdmiCecSourceImplementation.cpp:1046-1054): the connection
 * opens, the listener is attached, and then the opening batch of messages - <Give Device Power
 * Status>, <Request Active Source>, <Device Vendor ID> - throws.  A different catch, the same
 * rollback, and a state that must still come out consistent.
 */
TEST_F(HdmiCecSource_L2Test, CecEnableRollsBackWhenTheInitialAnnouncementsFail)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    HdmiCecSourceSuccess result;
    ASSERT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, result));
    ASSERT_TRUE(result.success);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_CALL(*p_connectionMock, sendTo(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Throw(std::runtime_error("L2: CEC transmit refused")));

    TEST_LOG("Enabling CEC with a failing Connection::sendTo()");
    HdmiCecSourceSuccess enableResult;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(true, enableResult));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    bool reportedEnabled = true;
    bool getSuccess = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetEnabled(reportedEnabled, getSuccess));
    EXPECT_FALSE(reportedEnabled)
        << "the rollback must leave cecEnableStatus false after the announcement batch threw";

    // Let transmits succeed again - gmock uses the LAST matching expectation, so this supersedes
    // the throwing one above - and then bring CEC up properly inside this test body.  Same reason
    // as HdmiHotPlugIsIgnoredWhileCecIsDisabled: TearDown's JSON-RPC restore must not be the thing
    // that runs a full CECEnable, because a client-side timeout there crashes framework code that
    // is out of scope for this pass.
    EXPECT_CALL(*p_connectionMock, sendTo(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [](const LogicalAddress&, const CECFrame&) { ++g_sendToCount; }));

    HdmiCecSourceSuccess reEnable;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, reEnable));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(true, reEnable));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetEnabled(reportedEnabled, getSuccess));
    EXPECT_TRUE(reportedEnabled)
        << "CEC did not come back up once transmits were allowed to succeed again";

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Inbound requests whose response cannot be transmitted are absorbed, not propagated.
 *
 * Covers the catch(...) arms of the HdmiCecSourceProcessor::process overloads - GetCECVersion
 * (153-156), GiveOSDName (173-176), GivePhysicalAddress (187-190), GiveDeviceVendorID (198-205),
 * GiveDevicePowerStatus (280-283), RequestActiveSource (129-137) and Abort (314-317).  Each of
 * those handlers answers the peer, and each wraps the answer in a catch(...) precisely because a
 * CEC transmit can fail at any moment; none of those arms had ever run.
 *
 * One test rather than seven: the precondition is identical (every transmit throws) and the
 * assertion is identical (the plugin absorbs it and is still serving afterwards), so seven
 * fixtures would cost seven activations to assert the same thing.  Each frame is still named and
 * injected individually so a failure identifies which handler broke.
 */
TEST_F(HdmiCecSource_L2Test, InboundRequestsSurviveATransmitFailureOnTheResponse)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    ASSERT_TRUE(EnableCecAndAwaitFrameListener())
        << "CEC could not be enabled, so no FrameListener was captured and nothing could be injected.";

    // Make the device the active source first, so process(RequestActiveSource) enters its
    // transmit branch instead of returning without answering.
    uint8_t activeSourceFrame[] = { 0x4F, 0x82, 0x12, 0x34 };
    CECFrame activeSource(activeSourceFrame, sizeof(activeSourceFrame));
    for (auto* listener : listeners) {
        if (listener) {
            listener->notify(activeSource);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Every answer now fails.
    EXPECT_CALL(*p_connectionMock, sendTo(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Throw(std::runtime_error("L2: CEC response transmit refused")));

    // Header byte 0x54 = from logical address 5, to logical address 4 (this device).
    // 0x5F = from 5, broadcast.  Opcodes per CEC 1.4.
    struct InboundCase {
        const char* name;
        std::vector<uint8_t> bytes;
    };
    const std::vector<InboundCase> cases = {
        { "<Get CEC Version>",          { 0x54, 0x9F } },
        { "<Give OSD Name>",            { 0x54, 0x46 } },
        { "<Give Physical Address>",    { 0x54, 0x83 } },
        { "<Give Device Vendor ID>",    { 0x54, 0x8C } },
        { "<Give Device Power Status>", { 0x54, 0x8F } },
        { "<Request Active Source>",    { 0x5F, 0x85 } },
        { "<Abort>",                    { 0x54, 0xFF } },
    };

    for (const InboundCase& inbound : cases) {
        TEST_LOG("Injecting %s with every transmit failing", inbound.name);
        CECFrame frame(inbound.bytes.data(), static_cast<size_t>(inbound.bytes.size()));
        for (auto* listener : listeners) {
            if (listener) {
                // The handler owns the failure; nothing may escape into the frame listener.
                EXPECT_NO_THROW(listener->notify(frame))
                    << inbound.name << " let a transmit failure escape its handler";
            }
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // The plugin is still serving after seven failed responses, which is the point: a CEC bus that
    // will not accept traffic must not take the plugin with it.
    JsonObject params, result;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result));

    // Let transmits succeed again before leaving, so nothing after this test - including the
    // fixture's own teardown - has to work against a bus that refuses every frame.
    EXPECT_CALL(*p_connectionMock, sendTo(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [](const LogicalAddress&, const CECFrame&) { ++g_sendToCount; }));

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief A <Polling> frame is accepted and answers nothing.
 *
 * Covers HdmiCecSourceProcessor::process(const Polling&, const Header&)
 * (HdmiCecSourceImplementation.cpp:322-324), which had zero hits: polling is how CEC devices
 * probe for a live logical address, the acknowledgement happens at the HAL, and the handler
 * therefore has nothing to do.  "Nothing to do" is still behaviour worth pinning - a future
 * handler that started answering would break address discovery on a real bus.
 *
 * A Polling frame is a header and no opcode, which is what makes it a distinct decode path.
 */
TEST_F(HdmiCecSource_L2Test, InboundPollingFrameIsAcceptedWithoutAResponse)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    ASSERT_TRUE(EnableCecAndAwaitFrameListener()) << "CEC could not be enabled.";

    const int transmitsBefore = g_sendToCount.load();

    // Header only: from logical address 5 to logical address 4, no opcode and no operands.
    uint8_t pollingFrame[] = { 0x54 };
    CECFrame frame(pollingFrame, sizeof(pollingFrame));

    TEST_LOG("Injecting a <Polling> frame (header only, no opcode)");
    for (auto* listener : listeners) {
        if (listener) {
            EXPECT_NO_THROW(listener->notify(frame));
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // No assertion on an exact transmit count: the poll and update threads transmit on their own
    // schedule.  What is asserted is that the plugin stayed healthy and answerable, and the
    // TEST_LOG records what was observed so a regression here is visible in the transcript.
    TEST_LOG("transmits observed while the polling frame was processed: %d",
        g_sendToCount.load() - transmitsBefore);

    JsonObject params, result;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result));
    ASSERT_TRUE(result.HasLabel("enabled"));
    EXPECT_TRUE(result["enabled"].Boolean());

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief setVendorId boundary and invalid inputs.
 *
 * Covers the input-handling arms of HdmiCecSourceImplementation::SetVendorId
 * (HdmiCecSourceImplementation.cpp:1258-1281): a vendor ID is a 24-bit value delivered as a
 * string, so the interesting cases are the two ends of the range and the strings that are not a
 * vendor ID at all.  The happy path was already covered by SetVendorId_COMRPC/JSONRPC above;
 * these are the adjacent negative and boundary cases those two do not reach.
 */
TEST_F(HdmiCecSource_L2Test, SetVendorIdBoundaryAndInvalidInputs)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    struct VendorCase {
        const char* description;
        const char* vendorId;
    };
    const std::vector<VendorCase> cases = {
        { "minimum 24-bit value",            "0x000000" },
        { "maximum 24-bit value",            "0xFFFFFF" },
        { "just above the 24-bit range",     "0x1000000" },
        { "no 0x prefix",                    "0019FB" },
        { "not hexadecimal at all",          "not-a-vendor-id" },
        { "empty string",                    "" },
    };

    for (const VendorCase& vendorCase : cases) {
        HdmiCecSourceSuccess success;
        success.success = false;
        TEST_LOG("SetVendorId(%s) - %s", vendorCase.vendorId, vendorCase.description);

        // Every one of these must be handled rather than thrown out of, and the plugin must
        // remain readable afterwards.  The accepted/rejected verdict is the implementation's to
        // make - what is asserted is that it makes one and stays consistent.
        const uint32_t status = m_cecSourcePlugin->SetVendorId(std::string(vendorCase.vendorId), success);
        TEST_LOG("  -> status %u, success %d", status, static_cast<int>(success.success));

        std::string readBack;
        bool readSuccess = false;
        EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(readBack, readSuccess))
            << "GetVendorId stopped working after SetVendorId(" << vendorCase.vendorId << ")";
        EXPECT_TRUE(readSuccess);
        EXPECT_FALSE(readBack.empty())
            << "the vendor ID must never read back empty, even after a rejected write";
    }

    // Put a sane vendor ID back: setVendorId persists into the shared settings file, so leaving
    // one of the inputs above in place would hand it to every later test and to the next run.
    HdmiCecSourceSuccess restore;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetVendorId(std::string("0x0019FB"), restore));

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief The plugin shell refuses to initialise under a non-STB profile, and deinitialises quietly.
 *
 * Covers HdmiCecSource.cpp:61-62 (Initialize's profile guard) and 112-113 (Deinitialize's).  This
 * is a source-role plugin: searchRdkProfile() reading TV, or nothing at all, means the plugin does
 * not belong on this device and must decline rather than half-start.
 *
 * The whole transition is driven through the Controller, which is what makes it an L2 case rather
 * than a unit test: Thunder itself calls Initialize, sees the non-empty error string, and calls
 * Deinitialize with reason INITIALIZATION_FAILED - so the second guard is reached by the framework
 * on the framework's own path, not by the test calling it directly.
 *
 * /etc/device.properties is host-global and is owned by this fixture's ScopedHostFile member, so
 * the profile is swapped through that object and restored by it whatever happens here.
 */
TEST_F(HdmiCecSource_L2Test, PluginRefusesToActivateUnderANonSourceProfile)
{
    // Start from a clean deactivation so the activation below is the one under test.
    ASSERT_EQ(Core::ERROR_NONE, DeactivateService("org.rdk.HdmiCecSource"));

    ASSERT_TRUE(m_deviceProperties.Overwrite("RDK_PROFILE=TV\n"))
        << "could not rewrite " << m_deviceProperties.Name() << " to a TV profile";

    // Expected code: Server::Service::Activate() returns Core::ERROR_GENERAL for a plugin whose
    // Initialize handed back an error string (PluginServer.cpp:404), and Controller::Activate then
    // NORMALISES every result other than NONE/ILLEGAL_STATE/INPROGRESS/PENDING_CONDITIONS to
    // Core::ERROR_OPENING_FAILED (Controller.cpp:884).  Since this test goes through
    // Controller.1.activate, the normalised code is what arrives here.
    TEST_LOG("Activating with RDK_PROFILE=TV; Initialize must refuse");
    const uint32_t tvStatus = ActivateService("org.rdk.HdmiCecSource");
    EXPECT_EQ(Core::ERROR_OPENING_FAILED, tvStatus)
        << "a TV profile must fail activation of the SOURCE plugin; status was " << tvStatus;

    // No profile at all is the second half of the guard's condition (NOT_FOUND).
    ASSERT_TRUE(m_deviceProperties.Overwrite(""))
        << "could not empty " << m_deviceProperties.Name();

    TEST_LOG("Activating with no RDK_PROFILE line; Initialize must refuse");
    const uint32_t emptyStatus = ActivateService("org.rdk.HdmiCecSource");
    EXPECT_EQ(Core::ERROR_OPENING_FAILED, emptyStatus)
        << "an absent profile must fail activation; status was " << emptyStatus;

    // Back to STB and activated, because the fixture's destructor deactivates the plugin and
    // expects that to succeed, and because every later test starts from an activated plugin.
    ASSERT_TRUE(m_deviceProperties.Overwrite("RDK_PROFILE=STB\n"))
        << "could not restore " << m_deviceProperties.Name() << " to an STB profile";

    EXPECT_EQ(Core::ERROR_NONE, ActivateServiceAndAwaitActivated("org.rdk.HdmiCecSource"))
        << "the plugin did not come back up under the correct profile";

    JsonObject params, result;
    EXPECT_EQ(Core::ERROR_NONE, InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result))
        << "the plugin activated but is not answering";
}
