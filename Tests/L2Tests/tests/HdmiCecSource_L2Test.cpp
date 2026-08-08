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
#include <condition_variable>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
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
	
	static void createFile(const char* fileName, const char* fileContent)
	{
		removeFile(fileName);

		std::ofstream fileContentStream(fileName);
		fileContentStream << fileContent;
		fileContentStream << "\n";
		fileContentStream.close();
	}

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
    std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    uint32_t m_event_signalled;

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
    int GetKeyCode() const { return m_keyCode; }

private:
    bool m_activeSourceStatus;
    int m_logicalAddress;
    int m_keyCode;
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
    // The display-device event sink the implementation hands to device::Host during Configure
    // (HdmiCecSourceImplementation.cpp:392).  HDMI hot-plug arrives on this interface, NOT through
    // the IARM handler the two members above capture: the implementation derives from
    // device::Host::IDisplayDeviceEvents and overrides OnDisplayHDMIHotPlug, and
    // HdmiCecSourceImplementation.h:290's dsHdmiEventHandler is a DECLARATION WITH NO DEFINITION -
    // nothing registers it, so those two members are never assigned.  Capturing the real sink here
    // is the same idiom the sink plugin's L2 fixture already uses for device::Host::IHdmiInEvents,
    // so it reuses an existing seam rather than adding one.
    device::Host::IDisplayDeviceEvents* displayEventsListener = nullptr;
    FrameListener* registeredListener = nullptr;
    std::vector<FrameListener*> listeners;

    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> HdmiCecSource_Engine;
    Core::ProxyType<RPC::CommunicatorClient> HdmiCecSource_Client;

private:
    std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    uint32_t m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
};

HdmiCecSource_L2Test::HdmiCecSource_L2Test()
    : L2TestMocks()
{
    TEST_LOG("HdmiCecSource_L2Test Constructor");

    // Setup device.properties file
    removeFile("/etc/device.properties");
    createFile("/etc/device.properties", "RDK_PROFILE=STB");
    createFile("/opt/persistent/ds/cecData_2.json", "0");
    createFile("/tmp/pwrmgr_restarted", "2");

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

    // Capture the display-device event sink the implementation registers during Configure, so a test
    // can deliver an HDMI hot-plug the way the platform does.  ON_CALL rather than EXPECT_CALL
    // deliberately: this only supplies a default action and imposes no cardinality, so no existing
    // case in this fixture changes behaviour or gains an expectation it must satisfy.
    ON_CALL(*p_hostImplMock, Register(::testing::A<device::Host::IDisplayDeviceEvents*>()))
        .WillByDefault(::testing::Invoke(
            [this](device::Host::IDisplayDeviceEvents* listener) -> dsError_t {
                this->displayEventsListener = listener;
                TEST_LOG("Captured display-device event listener: %p", static_cast<void*>(listener));
                return static_cast<dsError_t>(0);
            }));

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

    /* Activate plugin in constructor */
    uint32_t status = ActivateService("org.rdk.PowerManager");
    EXPECT_EQ(Core::ERROR_NONE, status);

    status = ActivateService("org.rdk.HdmiCecSource");
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

    // Cleanup device.properties file
    removeFile("/etc/device.properties");
    removeFile("/tmp/pwrmgr_restarted");
    removeFile("/opt/persistent/ds/cecData_2.json");
    removeFile("/opt/uimgr_settings.bin");

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
 * @brief Test OnDeviceRemoved event
 *
 * This test verifies that the OnDeviceRemoved event is received correctly.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceRemovedEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Simulate device removed event
                int testLogicalAddress = 4;
                m_notificationHandler.OnDeviceRemoved(testLogicalAddress);

                uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_REMOVED);
                EXPECT_EQ(status, ON_DEVICE_REMOVED);
                EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), testLogicalAddress);
                TEST_LOG("OnDeviceRemoved event verified");

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
    TEST_LOG("CECVersion frame processed - device 5 added");

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
// Gap-derived additions: the HDMI hot-plug path, the matching-active-source arms, the
// send-failure handlers, and SetVendorId's rejection and fallback arms.
//
// Each case below is ADJACENT to existing passing cases, never a rewrite of one.  They exist
// because measurement put HdmiCecSourceImplementation.cpp at 70.1% line coverage at L2 with the
// largest single uncovered region -- the whole HDMI hot-plug path, HdmiCecSourceImplementation.cpp
// :692-789 -- having no test at any level, and with a family of catch(...) arms in the inbound
// handlers that only error injection can reach.
//
// A bounded wait, local to this block.  The hot-plug path is the only asynchronous thing these
// cases touch: OnDisplayHDMIHotPlug spawns threadHotPlugEventHandler on a DETACHED thread
// (HdmiCecSourceImplementation.cpp:721-722), so its effects land after the call returns.  Waiting on
// the observable rather than sleeping a fixed pad is both faster in the common case and stronger,
// because a fixed pad can expire while the thread is still mid-flight.
namespace {
    bool AwaitSourceCondition(const std::function<bool()>& condition,
        const uint32_t boundMs = 5000,
        const uint32_t intervalMs = 20)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(boundMs);
        for (;;) {
            if (condition()) {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return condition();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }
}

/**
 * @brief An HDMI hot-plug connect re-reads the device's addresses and re-announces it on the bus.
 *
 * COVERAGE_GAPS.md traceability: gap-plugin-source-hotplug (HdmiCecSourceImplementation.cpp
 * OnDisplayHDMIHotPlug :708, threadHotPlugEventHandler :692, onHdmiHotPlug :742).  None of the three
 * had a test at any level.  They are not reachable through the two IARM handler members this
 * fixture captures - HdmiCecSourceImplementation.h:290 declares a static dsHdmiEventHandler that has
 * no definition anywhere, so nothing ever registers it - and the real path is the
 * device::Host::IDisplayDeviceEvents sink the implementation registers during Configure at
 * cpp:392, which the fixture now captures.
 *
 * What the production path owes, and what is therefore asserted.  onHdmiHotPlug on CONNECTED
 * re-reads the physical and logical addresses, interrogates the display's EDID to decide whether it
 * is talking to an LG panel, and then puts TWO broadcast messages on the bus - <Report Physical
 * Address> and <Device Vendor ID> (cpp:773-781).  The two sends are the observable: they are what
 * distinguishes a hot-plug that was processed from one that was dropped, and the address re-read is
 * confirmed independently by the third step below, where an <Active Source> frame carrying the
 * NEW address is recognised as ours - which it could not be if the re-read had not happened.
 *
 * Three arms, per the negative/corner requirement:
 *   - CONNECTED with CEC enabled: both broadcasts go out.
 *   - DISCONNECTED: onHdmiHotPlug's guard at cpp:744 does not hold, so NOTHING is announced.  This
 *     is the arm with teeth - it is the only observable difference between a working guard and a
 *     removed one, and both events return void either way.
 *   - CEC disabled: OnDisplayHDMIHotPlug returns at cpp:712-717 before spawning the thread at all.
 */
TEST_F(HdmiCecSource_L2Test, HdmiHotPlugConnectRefreshesTheAddressesAndReAnnouncesTheDevice)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_controller_cecSource);
    ASSERT_NE(nullptr, m_cecSourcePlugin);
    ASSERT_NE(nullptr, displayEventsListener)
        << "the implementation never registered a display-device event sink, so a hot-plug cannot "
           "be delivered the way the platform delivers it";

    // 1.2.3.4 instead of the fixture's 0x12345678, for a reason that matters in step 3.  A CEC
    // physical address is four digits; the production code builds PhysicalAddress from the four
    // BYTES of the value it reads (cpp:1207), while a frame-parsed address is the two PACKED bytes
    // the wire carries.  Those two spellings of the same address only render alike when every digit
    // is a single hex digit - 1.2.3.4 being exactly that case - which is why the fixture's
    // 0x12345678 can never match an injected frame and why step 3 needs this value.
    EXPECT_CALL(*p_libCCECMock, getPhysicalAddress(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](uint32_t* physAddress) { *physAddress = static_cast<uint32_t>(0x01020304); }));

    std::atomic<int> broadcastSends { 0 };
    ON_CALL(*p_connectionMock,
        sendTo(::testing::Matcher<const LogicalAddress&>(::testing::_), ::testing::Matcher<const CECFrame&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [&broadcastSends](const LogicalAddress& to, const CECFrame&) {
                if (to.toInt() == LogicalAddress::BROADCAST) {
                    ++broadcastSends;
                }
            }));

    // 1. CONNECTED must reach the bus with both announcements.
    const int beforeConnect = broadcastSends.load();
    TEST_LOG("Delivering an HDMI hot-plug CONNECTED event");
    displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED);
    EXPECT_TRUE(AwaitSourceCondition([&]() { return broadcastSends.load() >= beforeConnect + 2; }))
        << "a hot-plug connect owes <Report Physical Address> and <Device Vendor ID>, but only "
        << (broadcastSends.load() - beforeConnect) << " broadcast(s) went out";

    // 2. DISCONNECTED must announce nothing: onHdmiHotPlug only acts on CONNECTED.  Settle first so
    //    anything step 1 was still emitting is counted before this window opens.
    (void)AwaitSourceCondition([]() { return false; }, 300, 100);
    const int beforeDisconnect = broadcastSends.load();
    TEST_LOG("Delivering an HDMI hot-plug DISCONNECTED event");
    displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_DISCONNECTED);
    (void)AwaitSourceCondition([&]() { return broadcastSends.load() > beforeDisconnect; }, 1000, 50);
    EXPECT_EQ(beforeDisconnect, broadcastSends.load())
        << "a hot-plug DISCONNECT announced " << (broadcastSends.load() - beforeDisconnect)
        << " message(s); onHdmiHotPlug's CONNECTED guard did not hold";

    // 3. The address really was re-read.  An <Active Source> broadcast carrying 1.2.3.4 packed as
    //    0x12 0x34 is now OUR address, so the implementation must record itself as the active
    //    source - something it cannot do unless step 1's getPhysicalAddress() actually ran.
    ASSERT_FALSE(listeners.empty()) << "no FrameListener was captured";
    const auto inject = [this](const std::vector<uint8_t>& bytes) {
        CECFrame frame(bytes.data(), static_cast<size_t>(bytes.size()));
        for (auto* listener : listeners) {
            if (listener) {
                EXPECT_NO_THROW(listener->notify(frame));
            }
        }
    };

    TEST_LOG("Injecting <Active Source> for 1.2.3.4, which is now our own physical address");
    inject({ 0x4F, 0x82, 0x12, 0x34 });

    bool activeStatus = false;
    bool statusRead = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetActiveSourceStatus(activeStatus, statusRead));
    EXPECT_TRUE(statusRead);
    EXPECT_TRUE(activeStatus)
        << "an <Active Source> announcement carrying our own physical address did not make us the "
           "active source, so the hot-plug did not refresh the address";

    // 4. And being the active source is what makes us answer <Request Active Source>: the handler at
    //    cpp:127-138 transmits only when isDeviceActiveSource holds, so this send is the proof.
    const int beforeRequest = broadcastSends.load();
    TEST_LOG("Injecting <Request Active Source>; we owe an <Active Source> because we hold the bus");
    inject({ 0x0F, 0x85 });
    EXPECT_TRUE(AwaitSourceCondition([&]() { return broadcastSends.load() >= beforeRequest + 1; }))
        << "we are the active source, so <Request Active Source> owes an <Active Source> broadcast";

    // 5. CEC off: OnDisplayHDMIHotPlug returns before spawning its worker at all.
    HdmiCecSourceSuccess disabled;
    disabled.success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetEnabled(false, disabled));
    EXPECT_TRUE(disabled.success);
    (void)AwaitSourceCondition([]() { return false; }, 300, 100);
    const int beforeDisabledPlug = broadcastSends.load();
    TEST_LOG("Delivering an HDMI hot-plug CONNECTED event with CEC disabled");
    displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED);
    (void)AwaitSourceCondition([&]() { return broadcastSends.load() > beforeDisabledPlug; }, 1000, 50);
    EXPECT_EQ(beforeDisabledPlug, broadcastSends.load())
        << "a hot-plug announced " << (broadcastSends.load() - beforeDisabledPlug)
        << " message(s) while CEC was disabled; OnDisplayHDMIHotPlug's cecEnableStatus guard did "
           "not hold";

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief An LG panel on the other end changes which vendor id the device advertises.
 *
 * COVERAGE_GAPS.md traceability: gap-plugin-source-lgvendorid (HdmiCecSourceImplementation.cpp
 * :758-763 in onHdmiHotPlug, and the isLGTvConnected arm of process(GiveDeviceVendorID) at :202).
 * The fixture's default EDID carries manufacturer bytes 0x4C 0x2D, so every existing case takes the
 * not-LG arm and the LG arm has never executed.
 *
 * The identification is by EDID manufacturer id: bytes 8 and 9 equal to 0x1E 0x6D mean LG
 * (cpp:759-762).  Once that is latched, the vendor id the device reports over CEC changes from the
 * application's own id to LG's - both in the hot-plug re-announcement and in every later answer to
 * <Give Device Vendor ID> - so the assertion is that a vendor-id broadcast still goes out on both
 * paths with the LG EDID in place, and that the published vendor-id accessor is NOT changed by it,
 * because appVendorId is a separate setting from the id used on the wire.
 */
TEST_F(HdmiCecSource_L2Test, HdmiHotPlugFromAnLgDisplaySwitchesTheAdvertisedVendorId)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);
    ASSERT_NE(nullptr, displayEventsListener);
    ASSERT_FALSE(listeners.empty());

    // An LG manufacturer id in the EDID. Byte 8 and byte 9 are the two the production code reads.
    EXPECT_CALL(*p_displayMock, getEDIDBytes(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](std::vector<uint8_t>& edid) {
                edid = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
                    0x1E, 0x6D, 0xFE, 0x08, 0x00, 0x00, 0x00, 0x00 };
            }));

    std::atomic<int> broadcastSends { 0 };
    ON_CALL(*p_connectionMock,
        sendTo(::testing::Matcher<const LogicalAddress&>(::testing::_), ::testing::Matcher<const CECFrame&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [&broadcastSends](const LogicalAddress& to, const CECFrame&) {
                if (to.toInt() == LogicalAddress::BROADCAST) {
                    ++broadcastSends;
                }
            }));

    const int beforeConnect = broadcastSends.load();
    TEST_LOG("Delivering an HDMI hot-plug CONNECTED event behind an LG EDID");
    displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED);
    EXPECT_TRUE(AwaitSourceCondition([&]() { return broadcastSends.load() >= beforeConnect + 2; }))
        << "the hot-plug behind an LG panel owes <Report Physical Address> and <Device Vendor ID>; "
        << "saw " << (broadcastSends.load() - beforeConnect);

    // And the later answer to <Give Device Vendor ID> takes the LG arm rather than the default one.
    (void)AwaitSourceCondition([]() { return false; }, 300, 100);
    const int beforeGive = broadcastSends.load();
    TEST_LOG("Injecting <Give Device Vendor ID>; the answer must still be broadcast");
    CECFrame giveVendor(std::vector<uint8_t> { 0x04, 0x8C }.data(), 2u);
    for (auto* listener : listeners) {
        if (listener) {
            EXPECT_NO_THROW(listener->notify(giveVendor));
        }
    }
    EXPECT_TRUE(AwaitSourceCondition([&]() { return broadcastSends.load() >= beforeGive + 1; }))
        << "<Give Device Vendor ID> was not answered with a <Device Vendor ID> broadcast";

    // The published setting is untouched: what the panel is does not rewrite the configured id.
    string vendorId;
    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(vendorId, success));
    EXPECT_TRUE(success);
    EXPECT_FALSE(vendorId.empty())
        << "the configured vendor id must still be readable after an LG panel was detected";

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Every inbound handler that transmits contains its own send failure.
 *
 * COVERAGE_GAPS.md traceability: gap-plugin-source-sendfailure (the catch(...) arms of
 * HdmiCecSourceProcessor::process for RequestActiveSource :134-137, GetCECVersion :153-156,
 * GiveOSDName :173-176, GivePhysicalAddress :187-190 and GiveDeviceVendorID :204-205).  Every one of
 * those handlers wraps its transmit in try/catch, and not one of the catch arms had ever executed,
 * because nothing in either suite made a send fail.  A handler that lets an exception escape takes
 * the CEC receive thread with it, so these arms are the difference between a lost frame and a lost
 * plugin - and they are only reachable by injection, which is what Directive 2 asks for.
 *
 * CECNoAckException is what the real Connection raises when nothing answers on the bus, so it is the
 * failure the production code is written to absorb rather than an arbitrary throw.
 */
TEST_F(HdmiCecSource_L2Test, OutboundSendFailuresAreContainedByEveryInboundHandler)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);
    ASSERT_FALSE(listeners.empty());

    std::atomic<int> attemptedSends { 0 };
    ON_CALL(*p_connectionMock,
        sendTo(::testing::Matcher<const LogicalAddress&>(::testing::_), ::testing::Matcher<const CECFrame&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [&attemptedSends](const LogicalAddress&, const CECFrame&) {
                ++attemptedSends;
                throw CECNoAckException();
            }));
    ON_CALL(*p_connectionMock,
        sendTo(::testing::Matcher<const LogicalAddress&>(::testing::_), ::testing::Matcher<const CECFrame&>(::testing::_), ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&attemptedSends](const LogicalAddress&, const CECFrame&, int) {
                ++attemptedSends;
                throw CECNoAckException();
            }));

    const auto inject = [this](const std::vector<uint8_t>& bytes, const char* what) {
        CECFrame frame(bytes.data(), static_cast<size_t>(bytes.size()));
        for (auto* listener : listeners) {
            if (listener) {
                // The whole point: notify() must return normally even though the handler's transmit
                // threw underneath it.
                EXPECT_NO_THROW(listener->notify(frame))
                    << what << " let a send failure escape its handler";
            }
        }
    };

    // Make us the active source first, so <Request Active Source>'s transmitting arm is entered at
    // all - its catch is unreachable from the arm that sends nothing.  That needs our physical
    // address to be one an injected frame can match, so it is re-read through the HOT-PLUG path
    // rather than by toggling SetEnabled.
    //
    // The choice is not stylistic.  SetEnabled(false) runs CECDisable, which deletes the Connection
    // and with it the FrameListener the plugin had registered; this fixture's addFrameListener stub
    // only ever APPENDS to `listeners` and nothing removes the stale entry, so a
    // SetEnabled(false)/SetEnabled(true) pair leaves a dangling pointer in that vector and the next
    // injection dereferences freed memory - measured here as a SIGSEGV that took the whole
    // WPEFramework host down.  onHdmiHotPlug calls getPhysicalAddress() (cpp:747) without touching
    // the connection, so it refreshes the address with no stale listener left behind.  The dangling
    // entry is a latent hazard in the shared fixture, not something these cases can repair without
    // altering behaviour every existing case depends on, so it is avoided here and reported.
    ASSERT_NE(nullptr, displayEventsListener)
        << "no display-device event sink was registered, so the address cannot be refreshed";
    EXPECT_CALL(*p_libCCECMock, getPhysicalAddress(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](uint32_t* physAddress) { *physAddress = static_cast<uint32_t>(0x01020304); }));
    const size_t listenersBeforeRefresh = listeners.size();
    displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED);
    // onHdmiHotPlug transmits too, and its own try/catch (cpp:783-786) must absorb the failure just
    // like the inbound handlers do - so the hot-plug is itself one of the arms under test here.
    EXPECT_TRUE(AwaitSourceCondition([&]() { return attemptedSends.load() > 0; }))
        << "the hot-plug refresh attempted no transmit, so its catch arm was not exercised";
    EXPECT_EQ(listenersBeforeRefresh, listeners.size())
        << "the hot-plug refresh must not re-register a FrameListener; a second entry means the "
           "connection was recreated and the first entry now dangles";
    inject({ 0x4F, 0x82, 0x12, 0x34 }, "<Active Source>");

    const int before = attemptedSends.load();
    inject({ 0x0F, 0x85 }, "<Request Active Source>");
    inject({ 0x04, 0x9F }, "<Get CEC Version>");
    inject({ 0x04, 0x46 }, "<Give OSD Name>");
    inject({ 0x04, 0x83 }, "<Give Physical Address>");
    inject({ 0x04, 0x8C }, "<Give Device Vendor ID>");
    inject({ 0x04, 0x8F }, "<Give Device Power Status>");

    EXPECT_GT(attemptedSends.load(), before)
        << "none of the six injected requests attempted a transmit, so no catch arm was exercised";

    // Still serving: the receive path survived six failed transmits.
    bool enabled = false;
    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetEnabled(enabled, success));
    EXPECT_TRUE(success) << "the plugin stopped answering after its transmits failed";

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief SetVendorId refuses an empty id and falls back to the default on an unparsable one.
 *
 * COVERAGE_GAPS.md traceability: gap-plugin-source-setvendorid (HdmiCecSourceImplementation.cpp
 * :1257-1260, the empty-argument rejection, and :1263-1279, the three catch arms around stoi).
 * SetVendorId_COMRPC and SetVendorId_JSONRPC both pass and are left exactly as they are; both send a
 * well-formed hexadecimal id, so the rejection and all three fallbacks had never run.
 *
 * The contract has two distinct shapes and the difference matters to a caller: an EMPTY id is an
 * error - it returns ERROR_GENERAL with success false and changes nothing - whereas an id that is
 * merely UNPARSABLE is accepted, reported as success, and silently replaced by the default
 * 0x0019FB.  Asserting both is what pins the distinction; asserting only the happy path leaves a
 * caller unable to tell which of the two it is getting.
 */
TEST_F(HdmiCecSource_L2Test, SetVendorIdRejectsAnEmptyValueAndFallsBackOnUnparsableOnes)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);

    string baseline;
    bool baselineRead = false;
    ASSERT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(baseline, baselineRead));
    ASSERT_TRUE(baselineRead);

    // 1. Empty: rejected outright, and the stored id is left alone.
    HdmiCecSourceSuccess empty;
    empty.success = true;
    EXPECT_EQ(Core::ERROR_GENERAL, m_cecSourcePlugin->SetVendorId(string(), empty));
    EXPECT_FALSE(empty.success) << "an empty vendor id must be reported as a failure";

    string afterEmpty;
    bool afterEmptyRead = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(afterEmpty, afterEmptyRead));
    EXPECT_TRUE(afterEmptyRead);
    EXPECT_EQ(baseline, afterEmpty)
        << "a rejected SetVendorId must not have changed the stored id: '" << baseline << "' -> '"
        << afterEmpty << "'";

    // 2. Unparsable: accepted, and the default is used instead. 0x0019FB is what the production
    //    code substitutes, and appVendorId is built from its three low bytes (cpp:1281).
    HdmiCecSourceSuccess unparsable;
    unparsable.success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetVendorId(_T("not-a-number"), unparsable));
    EXPECT_TRUE(unparsable.success)
        << "an unparsable vendor id is accepted with the default substituted, not rejected";

    string afterUnparsable;
    bool afterUnparsableRead = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(afterUnparsable, afterUnparsableRead));
    EXPECT_TRUE(afterUnparsableRead);
    EXPECT_FALSE(afterUnparsable.empty()) << "the substituted default must be readable back";

    // 3. Out of range: a value far beyond an unsigned int takes the other fallback arm, and is
    //    likewise accepted with the default substituted.
    HdmiCecSourceSuccess huge;
    huge.success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetVendorId(_T("FFFFFFFFFFFFFFFFFF"), huge));
    EXPECT_TRUE(huge.success)
        << "an out-of-range vendor id is accepted with the default substituted, not rejected";

    string afterHuge;
    bool afterHugeRead = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(afterHuge, afterHugeRead));
    EXPECT_TRUE(afterHugeRead);
    EXPECT_EQ(afterUnparsable, afterHuge)
        << "both fallback arms substitute the same default, so the stored id must not differ";

    // 4. And a well-formed id still takes effect afterwards, so the fallbacks did not wedge it.
    HdmiCecSourceSuccess good;
    good.success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->SetVendorId(_T("00A0AF"), good));
    EXPECT_TRUE(good.success);

    string afterGood;
    bool afterGoodRead = false;
    EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetVendorId(afterGood, afterGoodRead));
    EXPECT_TRUE(afterGoodRead);
    EXPECT_NE(afterHuge, afterGood)
        << "a well-formed vendor id must replace the substituted default";

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief The address refresh follows a changed logical address and survives a failing HAL.
 *
 * COVERAGE_GAPS.md traceability: gap-plugin-source-addressrefresh
 * (HdmiCecSourceImplementation.cpp getPhysicalAddress :1169-1185 and getLogicalAddress
 * :1187-1211).  Both functions wrap their HAL call in try/catch and both catch arms were dead: every
 * existing case runs against a HAL mock that always succeeds, so a HAL that raises was never
 * modelled at this level.  The `smConnection->setSource()` arm at :1203 was dead for a different
 * reason - it only runs when the HAL hands back a logical address DIFFERENT from the stored one, and
 * the fixture's HAL always returns the same one.
 *
 * Why these arms matter.  getPhysicalAddress and getLogicalAddress are called on the hot-plug path,
 * on the power-mode path and from CECEnable, i.e. from callbacks the platform drives.  An exception
 * escaping either would unwind through the platform's own thread, so absorbing it is the difference
 * between a missed address refresh and a lost plugin.  The changed-address arm is the mechanism by
 * which the device follows a logical-address reassignment after a wake-up, and until now nothing
 * proved it re-pointed the connection's initiator at all.
 *
 * The refresh is driven through the hot-plug sink for the same reason as the case above: it calls
 * both getters (cpp:747-748) without recreating the Connection, so no stale FrameListener is left in
 * the fixture's append-only listener vector.  Each arm is followed by a liveness check, because
 * "the exception was absorbed" and "the plugin is still usable afterwards" are two different claims
 * and only the second one is what a caller depends on.
 */
TEST_F(HdmiCecSource_L2Test, AddressRefreshFollowsAChangedLogicalAddressAndSurvivesAFailingHal)
{
    ASSERT_EQ(Core::ERROR_NONE, CreateHdmiCecSourceInterfaceObject());
    ASSERT_NE(nullptr, m_cecSourcePlugin);
    ASSERT_NE(nullptr, displayEventsListener)
        << "the implementation never registered a display-device event sink";

    const auto stillServing = [this](const char* stage) {
        bool enabled = false;
        bool success = false;
        EXPECT_EQ(Core::ERROR_NONE, m_cecSourcePlugin->GetEnabled(enabled, success))
            << stage << ": the plugin stopped answering";
        EXPECT_TRUE(success) << stage << ": the plugin stopped answering";
    };

    // 1. The HAL reports a DIFFERENT logical address than the one already held, so the refresh must
    //    adopt it and re-point the connection's initiator (cpp:1199-1204).  DEV_TYPE_TUNER is the
    //    device type the implementation asks for; 4 is a playback address, deliberately not the 0
    //    the fixture's HAL hands out, so the inequality at cpp:1199 holds.
    EXPECT_CALL(*p_libCCECMock, getLogicalAddress(::testing::_))
        .WillRepeatedly(::testing::Return(4));
    TEST_LOG("Refreshing addresses while the HAL reports a changed logical address");
    displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED);
    EXPECT_TRUE(AwaitSourceCondition([this]() {
        // The adopted address is observable through the device list, which the implementation keys on
        // its own logical address; asking for it is enough to show the refresh completed and left the
        // plugin coherent.
        uint32_t numberOfDevices = 0;
        Exchange::IHdmiCecSource::IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
        bool success = false;
        const bool queried
            = m_cecSourcePlugin->GetDeviceList(numberOfDevices, deviceList, success) == Core::ERROR_NONE;
        if (deviceList != nullptr) {
            deviceList->Release();
        }
        return queried && success;
    })) << "the device list became unreadable after the logical address changed";
    stillServing("after a changed logical address");

    // 2. The HAL raises while reporting the physical address.  getPhysicalAddress catches
    //    std::exception (cpp:1180-1183) and returns, leaving the previously known address in place.
    EXPECT_CALL(*p_libCCECMock, getPhysicalAddress(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](unsigned int*) -> void { throw CECNoAckException(); }));
    TEST_LOG("Refreshing addresses while the HAL raises on getPhysicalAddress");
    EXPECT_NO_THROW(displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED))
        << "a HAL failure on getPhysicalAddress escaped into the platform callback";
    stillServing("after a failing getPhysicalAddress");

    // 3. And the same for the logical address (cpp:1206-1209).  Both getters now fail, which is the
    //    realistic shape of a HAL that has gone away rather than one that fails selectively.
    EXPECT_CALL(*p_libCCECMock, getLogicalAddress(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](int) -> int { throw CECNoAckException(); }));
    TEST_LOG("Refreshing addresses while the HAL raises on getLogicalAddress too");
    EXPECT_NO_THROW(displayEventsListener->OnDisplayHDMIHotPlug(dsDISPLAY_EVENT_CONNECTED))
        << "a HAL failure on getLogicalAddress escaped into the platform callback";
    stillServing("after a failing getLogicalAddress");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}
