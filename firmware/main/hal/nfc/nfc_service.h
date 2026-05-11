#pragma once

#include <cstdint>
#include <string>

namespace hal {

struct TransitIcBalanceInfo {
    bool success = false;
    uint32_t balance_yen = 0;
    std::string idm_hex;
    std::string system_code_hex;
    std::string detail;
};

class NfcService {
public:
    static NfcService& GetInstance();
    void Start();
    bool TryReadTransitIcBalance(TransitIcBalanceInfo& info);

private:
    NfcService() = default;
    NfcService(const NfcService&) = delete;
    NfcService& operator=(const NfcService&) = delete;
};

}  // namespace hal
