/**
 * @file src/platform/windows/misc.cpp
 * @brief Miscellaneous definitions for Windows.
 */
// standard includes
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <vector>

// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>

// prevent clang format from "optimizing" the header include order
// clang-format off
#include <iphlpapi.h>
#include <iterator>
#include <timeapi.h>
#include <WinSock2.h>
#include <Windows.h>
#include <WinUser.h>
#include <WS2tcpip.h>
#include <sddl.h>
// clang-format on

// Boost overrides NTDDI_VERSION, so we re-override it here
#undef NTDDI_VERSION
/**
 * @def NTDDI_VERSION
 * @brief Macro for NTDDI VERSION.
 */
#define NTDDI_VERSION NTDDI_WIN10
#include <Shlwapi.h>

// local includes
#include "misc.h"
#include "src/entry_handler.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include "utf_utils.h"

// UDP_SEND_MSG_SIZE was added in the Windows 10 20H1 SDK
#ifndef UDP_SEND_MSG_SIZE
  /**
   * @def UDP_SEND_MSG_SIZE
   * @brief Macro for UDP SEND MSG SIZE.
   */
  #define UDP_SEND_MSG_SIZE 2
#endif

// PROC_THREAD_ATTRIBUTE_JOB_LIST is currently missing from MinGW headers

#include <qos2.h>
#include <winternl.h>
extern "C" {
  /**
   * @brief Dynamically resolve NtSetTimerResolution from ntdll.
   *
   * @param DesiredResolution Desired resolution.
   * @param SetResolution Set resolution.
   * @param CurrentResolution Current resolution.
   * @return NTSTATUS reported by the system timer-resolution request.
   */
  NTSTATUS NTAPI NtSetTimerResolution(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
}

namespace {

}  // namespace

using namespace std::literals;

namespace platf {
  std::unique_ptr<deinit_t> init() {
    return std::make_unique<deinit_t>();
  }

  /**
   * @brief Owning pointer for `GetAdaptersAddresses` results.
   */
  using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

  HANDLE qos_handle = nullptr;  ///< QoS handle.

  decltype(QOSCreateHandle) *fn_QOSCreateHandle = nullptr;  ///< Fn QoS create handle.
  decltype(QOSAddSocketToFlow) *fn_QOSAddSocketToFlow = nullptr;  ///< Fn QoS add socket to flow.
  decltype(QOSRemoveSocketFromFlow) *fn_QOSRemoveSocketFromFlow = nullptr;  ///< Fn QoS remove socket from flow.

  std::filesystem::path appdata() {
    WCHAR sunshine_path[MAX_PATH];
    GetModuleFileNameW(nullptr, sunshine_path, _countof(sunshine_path));
    return std::filesystem::path {sunshine_path}.remove_filename() / L"config"sv;
  }

  std::string from_sockaddr(const sockaddr *const socket_address) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = socket_address->sa_family;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
    } else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) socket_address)->sin_addr, data, INET_ADDRSTRLEN);
    }

    return std::string {data};
  }

  std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const ip_addr) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = ip_addr->sa_family;
    std::uint16_t port = 0;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
      port = ((sockaddr_in6 *) ip_addr)->sin6_port;
    } else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
      port = ((sockaddr_in *) ip_addr)->sin_port;
    }

    return {port, std::string {data}};
  }

  /**
   * @brief Read Windows adapter addresses with automatic buffer sizing.
   *
   * @return Adapter-address list populated by GetAdaptersAddresses.
   */
  adapteraddrs_t get_adapteraddrs() {
    adapteraddrs_t info {nullptr};
    ULONG size = 0;

    while (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, info.get(), &size) == ERROR_BUFFER_OVERFLOW) {
      info.reset((PIP_ADAPTER_ADDRESSES) malloc(size));
    }

    return info;
  }

  std::string get_mac_address(const std::string_view &address) {
    adapteraddrs_t info = get_adapteraddrs();
    for (auto adapter_pos = info.get(); adapter_pos != nullptr; adapter_pos = adapter_pos->Next) {
      for (auto addr_pos = adapter_pos->FirstUnicastAddress; addr_pos != nullptr; addr_pos = addr_pos->Next) {
        if (adapter_pos->PhysicalAddressLength != 0 && address == from_sockaddr(addr_pos->Address.lpSockaddr)) {
          std::stringstream mac_addr;
          mac_addr << std::hex;
          for (int i = 0; i < adapter_pos->PhysicalAddressLength; i++) {
            if (i > 0) {
              mac_addr << ':';
            }
            mac_addr << std::setw(2) << std::setfill('0') << (int) adapter_pos->PhysicalAddress[i];
          }
          return mac_addr.str();
        }
      }
    }
    BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
    return "00:00:00:00:00:00"s;
  }

  /**
   * @brief Synchronize thread desktop.
   */
  HDESK syncThreadDesktop() {
    auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
    if (!hDesk) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to Open Input Desktop [0x"sv << util::hex(err).to_string_view() << ']';

      return nullptr;
    }

    if (!SetThreadDesktop(hDesk)) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to sync desktop to thread [0x"sv << util::hex(err).to_string_view() << ']';
    }

    CloseDesktop(hDesk);

    return hDesk;
  }

  /**
   * @brief Write status details to the log.
   */
  void print_status(const std::string_view &prefix, HRESULT status) {
    char err_string[1024];

    DWORD bytes = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, status, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err_string, sizeof(err_string), nullptr);

    BOOST_LOG(error) << prefix << ": "sv << std::string_view {err_string, bytes};
  }

  /**
   * @brief Check if the current process is running with system-level privileges.
   * @return `true` if the current process has system-level privileges, `false` otherwise.
   */
  bool is_running_as_system() {
    BOOL ret;
    PSID SystemSid;
    DWORD dwSize = SECURITY_MAX_SID_SIZE;

    // Allocate memory for the SID structure
    SystemSid = LocalAlloc(LMEM_FIXED, dwSize);
    if (SystemSid == nullptr) {
      BOOST_LOG(error) << "Failed to allocate memory for the SID structure: " << GetLastError();
      return false;
    }

    // Create a SID for the local system account
    ret = CreateWellKnownSid(WinLocalSystemSid, nullptr, SystemSid, &dwSize);
    if (ret) {
      // Check if the current process token contains this SID
      if (!CheckTokenMembership(nullptr, SystemSid, &ret)) {
        BOOST_LOG(error) << "Failed to check token membership: " << GetLastError();
        ret = false;
      }
    } else {
      BOOST_LOG(error) << "Failed to create a SID for the local system account. This may happen if the system is out of memory or if the SID buffer is too small: " << GetLastError();
    }

    // Free the memory allocated for the SID structure
    LocalFree(SystemSid);
    return ret;
  }

  void adjust_thread_priority(thread_priority_e priority) {
    int win32_priority;

    switch (priority) {
      case thread_priority_e::low:
        win32_priority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
      case thread_priority_e::normal:
        win32_priority = THREAD_PRIORITY_NORMAL;
        break;
      case thread_priority_e::high:
        win32_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
      case thread_priority_e::critical:
        win32_priority = THREAD_PRIORITY_HIGHEST;
        break;
      default:
        BOOST_LOG(error) << "Unknown thread priority: "sv << (int) priority;
        return;
    }

    if (!SetThreadPriority(GetCurrentThread(), win32_priority)) {
      auto winerr = GetLastError();
      BOOST_LOG(warning) << "Unable to set thread priority to "sv << win32_priority << ": "sv << winerr;
    }
  }

  void set_thread_name(std::string_view name) {
    std::wstring wname = utf_utils::from_utf8(std::string {name});
    HRESULT hr = SetThreadDescription(GetCurrentThread(), wname.c_str());
    if (FAILED(hr)) {
      BOOST_LOG(error) << "SetThreadDescription failed: " << hr;
    }
  }

  void restart_on_exit() {
    STARTUPINFOEXW startup_info {};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    WCHAR executable[MAX_PATH];
    if (GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable)) == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(fatal) << "Failed to get Sunshine path: "sv << winerr;
      return;
    }

    PROCESS_INFORMATION process_info;
    if (!CreateProcessW(executable, GetCommandLineW(), nullptr, nullptr, false, CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, (LPSTARTUPINFOW) &startup_info, &process_info)) {
      auto winerr = GetLastError();
      BOOST_LOG(fatal) << "Unable to restart Sunshine: "sv << winerr;
      return;
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
  }

  void restart() {
    // If we're running standalone, we have to respawn ourselves via CreateProcess().
    // If we're running from the service, we should just exit and let it respawn us.
    if (GetConsoleWindow() != nullptr) {
      // Avoid racing with the new process by waiting until we're exiting to start it.
      atexit(restart_on_exit);
    }

    // We use an async exit call here because we can't block the HTTP thread or we'll hang shutdown.
    lifetime::exit_sunshine(0, true);
  }

  /**
   * @brief Stores state while enumerating top-level Windows windows.
   */
  struct enum_wnd_context_t {
    std::set<DWORD> process_ids;  ///< Process ids.
    bool requested_exit;  ///< Whether a close request was observed while enumerating windows.
  };

  static BOOL CALLBACK prgrp_enum_windows(HWND hwnd, LPARAM lParam) {
    auto enum_ctx = (enum_wnd_context_t *) lParam;

    // Find the owner PID of this window
    DWORD wnd_process_id;
    if (!GetWindowThreadProcessId(hwnd, &wnd_process_id)) {
      // Continue enumeration
      return TRUE;
    }

    // Check if this window is owned by a process we want to terminate
    if (enum_ctx->process_ids.find(wnd_process_id) != enum_ctx->process_ids.end()) {
      // Send an async WM_CLOSE message to this window
      if (SendNotifyMessageW(hwnd, WM_CLOSE, 0, 0)) {
        BOOST_LOG(debug) << "Sent WM_CLOSE to PID: "sv << wnd_process_id;
        enum_ctx->requested_exit = true;
      } else {
        auto error = GetLastError();
        BOOST_LOG(warning) << "Failed to send WM_CLOSE to PID ["sv << wnd_process_id << "]: " << error;
      }
    }

    // Continue enumeration
    return TRUE;
  }

  SOCKADDR_IN to_sockaddr(boost::asio::ip::address_v4 address, uint16_t port) {
    SOCKADDR_IN saddr_v4 = {};

    saddr_v4.sin_family = AF_INET;
    saddr_v4.sin_port = htons(port);

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v4.sin_addr, addr_bytes.data(), sizeof(saddr_v4.sin_addr));

    return saddr_v4;
  }

  SOCKADDR_IN6 to_sockaddr(boost::asio::ip::address_v6 address, uint16_t port) {
    SOCKADDR_IN6 saddr_v6 = {};

    saddr_v6.sin6_family = AF_INET6;
    saddr_v6.sin6_port = htons(port);
    saddr_v6.sin6_scope_id = address.scope_id();

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v6.sin6_addr, addr_bytes.data(), sizeof(saddr_v6.sin6_addr));

    return saddr_v6;
  }

  // Use UDP segmentation offload if it is supported by the OS. If the NIC is capable, this will use
  // hardware acceleration to reduce CPU usage. Support for USO was introduced in Windows 10 20H1.
  bool send_batch(batched_send_info_t &send_info) {
    WSAMSG msg;

    // Convert the target address into a SOCKADDR
    SOCKADDR_IN taddr_v4;
    SOCKADDR_IN6 taddr_v6;
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v6;
      msg.namelen = sizeof(taddr_v6);
    } else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v4;
      msg.namelen = sizeof(taddr_v4);
    }

    auto const max_bufs_per_msg = send_info.payload_buffers.size() + (send_info.headers ? 1 : 0);

    std::vector<WSABUF> bufs((send_info.headers ? send_info.block_count : 1) * max_bufs_per_msg);
    DWORD bufcount = 0;
    if (send_info.headers) {
      // Interleave buffers for headers and payloads
      for (auto i = 0; i < send_info.block_count; i++) {
        bufs[bufcount].buf = (char *) &send_info.headers[(send_info.block_offset + i) * send_info.header_size];
        bufs[bufcount].len = send_info.header_size;
        bufcount++;
        auto payload_desc = send_info.buffer_for_payload_offset((send_info.block_offset + i) * send_info.payload_size);
        bufs[bufcount].buf = (char *) payload_desc.buffer;
        bufs[bufcount].len = send_info.payload_size;
        bufcount++;
      }
    } else {
      // Translate buffer descriptors into WSABUFs
      auto payload_offset = send_info.block_offset * send_info.payload_size;
      auto payload_length = payload_offset + (send_info.block_count * send_info.payload_size);
      while (payload_offset < payload_length) {
        auto payload_desc = send_info.buffer_for_payload_offset(payload_offset);
        bufs[bufcount].buf = (char *) payload_desc.buffer;
        bufs[bufcount].len = std::min(payload_desc.size, payload_length - payload_offset);
        payload_offset += bufs[bufcount].len;
        bufcount++;
      }
    }

    msg.lpBuffers = bufs.data();
    msg.dwBufferCount = bufcount;
    msg.dwFlags = 0;

    // At most, one DWORD option and one PKTINFO option
    char cmbuf[WSA_CMSG_SPACE(sizeof(DWORD)) + std::max(WSA_CMSG_SPACE(sizeof(IN6_PKTINFO)), WSA_CMSG_SPACE(sizeof(IN_PKTINFO)))] = {};
    ULONG cmbuflen = 0;

    msg.Control.buf = cmbuf;
    msg.Control.len = sizeof(cmbuf);

    auto cm = WSA_CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      IN6_PKTINFO pktInfo;

      SOCKADDR_IN6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IPV6;
      cm->cmsg_type = IPV6_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    } else {
      IN_PKTINFO pktInfo;

      SOCKADDR_IN saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_addr = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IP;
      cm->cmsg_type = IP_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }

    if (send_info.block_count > 1) {
      cmbuflen += WSA_CMSG_SPACE(sizeof(DWORD));

      cm = WSA_CMSG_NXTHDR(&msg, cm);
      cm->cmsg_level = IPPROTO_UDP;
      cm->cmsg_type = UDP_SEND_MSG_SIZE;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(DWORD));
      *((DWORD *) WSA_CMSG_DATA(cm)) = send_info.header_size + send_info.payload_size;
    }

    msg.Control.len = cmbuflen;

    // If USO is not supported, this will fail and the caller will fall back to unbatched sends.
    DWORD bytes_sent;
    return WSASendMsg((SOCKET) send_info.native_socket, &msg, 0, &bytes_sent, nullptr, nullptr) != SOCKET_ERROR;
  }

  bool send(send_info_t &send_info) {
    WSAMSG msg;

    // Convert the target address into a SOCKADDR
    SOCKADDR_IN taddr_v4;
    SOCKADDR_IN6 taddr_v6;
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v6;
      msg.namelen = sizeof(taddr_v6);
    } else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v4;
      msg.namelen = sizeof(taddr_v4);
    }

    WSABUF bufs[2];
    DWORD bufcount = 0;
    if (send_info.header) {
      bufs[bufcount].buf = (char *) send_info.header;
      bufs[bufcount].len = send_info.header_size;
      bufcount++;
    }
    bufs[bufcount].buf = (char *) send_info.payload;
    bufs[bufcount].len = send_info.payload_size;
    bufcount++;

    msg.lpBuffers = bufs;
    msg.dwBufferCount = bufcount;
    msg.dwFlags = 0;

    char cmbuf[std::max(WSA_CMSG_SPACE(sizeof(IN6_PKTINFO)), WSA_CMSG_SPACE(sizeof(IN_PKTINFO)))] = {};
    ULONG cmbuflen = 0;

    msg.Control.buf = cmbuf;
    msg.Control.len = sizeof(cmbuf);

    auto cm = WSA_CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      IN6_PKTINFO pktInfo;

      SOCKADDR_IN6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IPV6;
      cm->cmsg_type = IPV6_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    } else {
      IN_PKTINFO pktInfo;

      SOCKADDR_IN saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_addr = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IP;
      cm->cmsg_type = IP_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }

    msg.Control.len = cmbuflen;

    DWORD bytes_sent;
    if (WSASendMsg((SOCKET) send_info.native_socket, &msg, 0, &bytes_sent, nullptr, nullptr) == SOCKET_ERROR) {
      auto winerr = WSAGetLastError();
      BOOST_LOG(warning) << "WSASendMsg() failed: "sv << winerr;
      return false;
    }

    return true;
  }

  /**
   * @brief Owns platform QoS state that is restored during cleanup.
   */
  class qos_t: public deinit_t {
  public:
    /**
     * @brief Store a Windows QoS flow ID for cleanup on destruction.
     *
     * @param flow_id Flow ID.
     */
    qos_t(QOS_FLOWID flow_id):
        flow_id(flow_id) {
    }

    virtual ~qos_t() {
      if (!fn_QOSRemoveSocketFromFlow(qos_handle, (SOCKET) nullptr, flow_id, 0)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "QOSRemoveSocketFromFlow() failed: "sv << winerr;
      }
    }

  private:
    QOS_FLOWID flow_id;
  };

  /**
   * @brief Enables QoS on the given socket for traffic to the specified destination.
   */
  std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type, bool dscp_tagging) {
    SOCKADDR_IN saddr_v4;
    SOCKADDR_IN6 saddr_v6;
    PSOCKADDR dest_addr;
    bool using_connect_hack = false;

    // Windows doesn't support any concept of traffic priority without DSCP tagging
    if (!dscp_tagging) {
      return nullptr;
    }

    static std::once_flag load_qwave_once_flag;
    std::call_once(load_qwave_once_flag, []() {
      // qWAVE is not installed by default on Windows Server, so we load it dynamically
      HMODULE qwave = LoadLibraryExA("qwave.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!qwave) {
        BOOST_LOG(debug) << "qwave.dll is not available on this OS"sv;
        return;
      }

      fn_QOSCreateHandle = (decltype(fn_QOSCreateHandle)) GetProcAddress(qwave, "QOSCreateHandle");
      fn_QOSAddSocketToFlow = (decltype(fn_QOSAddSocketToFlow)) GetProcAddress(qwave, "QOSAddSocketToFlow");
      fn_QOSRemoveSocketFromFlow = (decltype(fn_QOSRemoveSocketFromFlow)) GetProcAddress(qwave, "QOSRemoveSocketFromFlow");

      if (!fn_QOSCreateHandle || !fn_QOSAddSocketToFlow || !fn_QOSRemoveSocketFromFlow) {
        BOOST_LOG(error) << "qwave.dll is missing exports?"sv;

        fn_QOSCreateHandle = nullptr;
        fn_QOSAddSocketToFlow = nullptr;
        fn_QOSRemoveSocketFromFlow = nullptr;

        FreeLibrary(qwave);
        return;
      }

      QOS_VERSION qos_version {1, 0};
      if (!fn_QOSCreateHandle(&qos_version, &qos_handle)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "QOSCreateHandle() failed: "sv << winerr;
        return;
      }
    });

    // If qWAVE is unavailable, just return
    if (!fn_QOSAddSocketToFlow || !qos_handle) {
      return nullptr;
    }

    auto disconnect_fg = util::fail_guard([&]() {
      if (using_connect_hack) {
        SOCKADDR_IN6 empty = {};
        empty.sin6_family = AF_INET6;
        if (connect((SOCKET) native_socket, (PSOCKADDR) &empty, sizeof(empty)) < 0) {
          auto wsaerr = WSAGetLastError();
          BOOST_LOG(error) << "qWAVE dual-stack workaround failed: "sv << wsaerr;
        }
      }
    });

    if (address.is_v6()) {
      auto address_v6 = address.to_v6();

      saddr_v6 = to_sockaddr(address_v6, port);
      dest_addr = (PSOCKADDR) &saddr_v6;

      // qWAVE doesn't properly support IPv4-mapped IPv6 addresses, nor does it
      // correctly support IPv4 addresses on a dual-stack socket (despite MSDN's
      // claims to the contrary). To get proper QoS tagging when hosting in dual
      // stack mode, we will temporarily connect() the socket to allow qWAVE to
      // successfully initialize a flow, then disconnect it again so WSASendMsg()
      // works later on.
      if (address_v6.is_v4_mapped()) {
        if (connect((SOCKET) native_socket, (PSOCKADDR) &saddr_v6, sizeof(saddr_v6)) < 0) {
          auto wsaerr = WSAGetLastError();
          BOOST_LOG(error) << "qWAVE dual-stack workaround failed: "sv << wsaerr;
        } else {
          BOOST_LOG(debug) << "Using qWAVE connect() workaround for QoS tagging"sv;
          using_connect_hack = true;
          dest_addr = nullptr;
        }
      }
    } else {
      saddr_v4 = to_sockaddr(address.to_v4(), port);
      dest_addr = (PSOCKADDR) &saddr_v4;
    }

    QOS_TRAFFIC_TYPE traffic_type;
    switch (data_type) {
      case qos_data_type_e::audio:
        traffic_type = QOSTrafficTypeVoice;
        break;
      case qos_data_type_e::video:
        traffic_type = QOSTrafficTypeAudioVideo;
        break;
      default:
        BOOST_LOG(error) << "Unknown traffic type: "sv << (int) data_type;
        return nullptr;
    }

    QOS_FLOWID flow_id = 0;
    if (!fn_QOSAddSocketToFlow(qos_handle, (SOCKET) native_socket, dest_addr, traffic_type, QOS_NON_ADAPTIVE_FLOW, &flow_id)) {
      auto winerr = GetLastError();
      BOOST_LOG(warning) << "QOSAddSocketToFlow() failed: "sv << winerr;
      return nullptr;
    }

    return std::make_unique<qos_t>(flow_id);
  }

  /**
   * @brief Read the current Windows high-resolution performance counter.
   */
  int64_t qpc_counter() {
    LARGE_INTEGER performance_counter;
    if (QueryPerformanceCounter(&performance_counter)) {
      return performance_counter.QuadPart;
    }
    return 0;
  }

  /**
   * @brief Convert the difference between two QPC readings to nanoseconds.
   */
  std::chrono::nanoseconds qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2) {
    auto get_frequency = []() {
      LARGE_INTEGER frequency;
      frequency.QuadPart = 0;
      QueryPerformanceFrequency(&frequency);
      return frequency.QuadPart;
    };
    static const double frequency = get_frequency();
    if (frequency) {
      return std::chrono::nanoseconds((int64_t) ((performance_counter1 - performance_counter2) * frequency / std::nano::den));
    }
    return {};
  }

  std::string get_host_name() {
    WCHAR hostname[256];
    if (GetHostNameW(hostname, ARRAYSIZE(hostname)) == SOCKET_ERROR) {
      BOOST_LOG(error) << "GetHostNameW() failed: "sv << WSAGetLastError();
      return "Sunshine"s;
    }
    return utf_utils::to_utf8(hostname);
  }

  /**
   * @brief Implements the high-precision timer using Win32 waitable timers.
   */
  class win32_high_precision_timer: public high_precision_timer {
  public:
    win32_high_precision_timer() {
      // Use CREATE_WAITABLE_TIMER_HIGH_RESOLUTION if supported (Windows 10 1809+)
      timer = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
      if (!timer) {
        timer = CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (!timer) {
          BOOST_LOG(error) << "Unable to create high_precision_timer, CreateWaitableTimerEx() failed: " << GetLastError();
        }
      }
    }

    ~win32_high_precision_timer() {
      if (timer) {
        CloseHandle(timer);
      }
    }

    void sleep_for(const std::chrono::nanoseconds &duration) override {
      if (!timer) {
        BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with uninitialized timer";
        return;
      }
      if (duration < 0s) {
        BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with negative duration";
        return;
      }
      if (duration > 5s) {
        BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with unexpectedly large duration (>5s)";
        return;
      }

      LARGE_INTEGER due_time;
      due_time.QuadPart = duration.count() / -100;
      SetWaitableTimer(timer, &due_time, 0, nullptr, nullptr, false);
      WaitForSingleObject(timer, INFINITE);
    }

    operator bool() override {
      return timer != nullptr;
    }

  private:
    HANDLE timer = nullptr;
  };

  std::unique_ptr<high_precision_timer> create_high_precision_timer() {
    return std::make_unique<win32_high_precision_timer>();
  }

}  // namespace platf
