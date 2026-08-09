#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "external_client/worker_client.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

template <typename Table, typename Query>
bool owns_endpoint(DWORD process_id, Query query) {
    ULONG bytes{};
    if (query(nullptr, &bytes) != ERROR_INSUFFICIENT_BUFFER)
        return true;
    std::vector<std::uint8_t> storage(bytes);
    if (query(storage.data(), &bytes) != NO_ERROR)
        return true;
    const auto *table = reinterpret_cast<const Table *>(storage.data());
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        if (table->table[index].dwOwningPid == process_id)
            return true;
    }
    return false;
}

bool has_network_endpoint(DWORD process_id) {
    const auto tcp4 = [](void *data, ULONG *bytes) {
        return GetExtendedTcpTable(data, bytes, FALSE, AF_INET,
                                   TCP_TABLE_OWNER_PID_ALL, 0);
    };
    const auto tcp6 = [](void *data, ULONG *bytes) {
        return GetExtendedTcpTable(data, bytes, FALSE, AF_INET6,
                                   TCP_TABLE_OWNER_PID_ALL, 0);
    };
    const auto udp4 = [](void *data, ULONG *bytes) {
        return GetExtendedUdpTable(data, bytes, FALSE, AF_INET,
                                   UDP_TABLE_OWNER_PID, 0);
    };
    const auto udp6 = [](void *data, ULONG *bytes) {
        return GetExtendedUdpTable(data, bytes, FALSE, AF_INET6,
                                   UDP_TABLE_OWNER_PID, 0);
    };
    return owns_endpoint<MIB_TCPTABLE_OWNER_PID>(process_id, tcp4) ||
           owns_endpoint<MIB_TCP6TABLE_OWNER_PID>(process_id, tcp6) ||
           owns_endpoint<MIB_UDPTABLE_OWNER_PID>(process_id, udp4) ||
           owns_endpoint<MIB_UDP6TABLE_OWNER_PID>(process_id, udp6);
}

int fail(int line, const std::string &detail = {}) {
    std::cerr << "worker runtime security failed at line " << line;
    if (!detail.empty())
        std::cerr << ": " << detail;
    std::cerr << '\n';
    return line;
}

#define CHECK(expression)          \
    do {                           \
        if (!(expression))         \
            return fail(__LINE__); \
    } while (false)

#define CHECK_ERROR(expression)           \
    do {                                  \
        if (!(expression))                \
            return fail(__LINE__, error); \
    } while (false)

int run(const std::filesystem::path &worker,
        const std::filesystem::path &runtime) {
    SetEnvironmentVariableW(L"ULTRARENDER_PLUGIN_PATH",
                            L"Z:\\hostile\\plugins");
    SetEnvironmentVariableW(L"ULTRARENDER_SCRIPT_PATH",
                            L"Z:\\hostile\\scripts");
    SetEnvironmentVariableW(L"ULTRARENDER_SOLVER_PATH",
                            L"Z:\\hostile\\solvers");
    SetEnvironmentVariableW(L"ULTRARENDER_MODEL_PATH",
                            L"Z:\\hostile\\models");
    std::string error;
    ure::contract_test::WorkerClient client;
    CHECK_ERROR(client.launch(worker, runtime, error));
    CHECK_ERROR(client.handshake(error));
    const DWORD process_id = GetProcessId(client.process());
    CHECK(process_id != 0);
    CHECK(!has_network_endpoint(process_id));
    CHECK_ERROR(client.shutdown(error));
    std::uint32_t exit_code{};
    CHECK(client.wait(5000, exit_code) && exit_code == 0);
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    return run(argv[1], argv[2]);
}
