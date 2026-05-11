#include "nfc_service.h"

#include "../board/hal_bridge.h"

#include <application.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hal {
namespace {

static const char* TAG = "NfcService";

std::mutex g_nfc_bus_mutex;

constexpr uint8_t kDeviceAddress = 0x50;
constexpr uint32_t kPollIntervalMs = 300;
constexpr uint32_t kRetryIntervalMs = 5000;
constexpr uint32_t kProbeRetryMs = 2000;
constexpr const char* kReaderId = "stackchan-head";
constexpr const char* kCardConfidence = "strong";

constexpr uint8_t REG_IO_CONFIGURATION_1 = 0x00;
constexpr uint8_t REG_IO_CONFIGURATION_2 = 0x01;
constexpr uint8_t REG_OPERATION_CONTROL = 0x02;
constexpr uint8_t REG_MODE_DEFINITION = 0x03;
constexpr uint8_t REG_BITRATE_DEFINITION = 0x04;
constexpr uint8_t REG_ISO14443A_SETTINGS = 0x05;
constexpr uint8_t REG_ISO14443B_AND_FELICA_SETTINGS = 0x07;
constexpr uint8_t REG_AUXILIARY_DEFINITION = 0x0A;
constexpr uint8_t REG_RECEIVER_CONFIGURATION_1 = 0x0B;
constexpr uint8_t REG_RECEIVER_CONFIGURATION_2 = 0x0C;
constexpr uint8_t REG_RECEIVER_CONFIGURATION_3 = 0x0D;
constexpr uint8_t REG_RECEIVER_CONFIGURATION_4 = 0x0E;
constexpr uint8_t REG_MASK_RECEIVER_TIMER = 0x0F;
constexpr uint8_t REG_NO_RESPONSE_TIMER_1 = 0x10;
constexpr uint8_t REG_NO_RESPONSE_TIMER_2 = 0x11;
constexpr uint8_t REG_TIMER_AND_EMV_CONTROL = 0x12;
constexpr uint8_t REG_MASK_MAIN_INTERRUPT = 0x16;
constexpr uint8_t REG_MASK_TIMER_AND_NFC_INTERRUPT = 0x17;
constexpr uint8_t REG_MASK_ERROR_AND_WAKEUP_INTERRUPT = 0x18;
constexpr uint8_t REG_MASK_PASSIVE_TARGET_INTERRUPT = 0x19;
constexpr uint8_t REG_MAIN_INTERRUPT = 0x1A;
constexpr uint8_t REG_TIMER_AND_NFC_INTERRUPT = 0x1B;
constexpr uint8_t REG_ERROR_AND_WAKEUP_INTERRUPT = 0x1C;
constexpr uint8_t REG_PASSIVE_TARGET_INTERRUPT = 0x1D;
constexpr uint8_t REG_FIFO_STATUS_1 = 0x1E;
constexpr uint8_t REG_FIFO_STATUS_2 = 0x1F;
constexpr uint8_t REG_COLLISION_DISPLAY = 0x20;
constexpr uint8_t REG_NUMBER_OF_TRANSMITTED_BYTES_1 = 0x22;
constexpr uint8_t REG_NUMBER_OF_TRANSMITTED_BYTES_2 = 0x23;
constexpr uint8_t REG_TX_DRIVER = 0x28;
constexpr uint8_t REG_PASSIVE_TARGET_MODULATION = 0x29;
constexpr uint8_t REG_EXTERNAL_FIELD_DETECTOR_ACTIVATION_THRESHOLD = 0x2A;
constexpr uint8_t REG_EXTERNAL_FIELD_DETECTOR_DEACTIVATION_THRESHOLD = 0x2B;
constexpr uint8_t REG_AUXILIARY_DISPLAY = 0x31;
constexpr uint8_t REG_IC_IDENTITY = 0x3F;

constexpr uint16_t REG_EMD_SUPPRESSION_CONFIGURATION = 0x0005;
constexpr uint16_t REG_SQUELCH_TIMER = 0x000F;
constexpr uint16_t REG_AUXILIARY_MODULATION_SETTING = 0x0028;
constexpr uint16_t REG_RESISTIVE_AM_MODULATION = 0x002A;
constexpr uint16_t REG_REGULATOR_DISPLAY = 0x002C;
constexpr uint16_t REG_OVERSHOOT_PROTECTION_CONFIGURATION_1 = 0x0030;
constexpr uint16_t REG_OVERSHOOT_PROTECTION_CONFIGURATION_2 = 0x0031;
constexpr uint16_t REG_UNDERSHOOT_PROTECTION_CONFIGURATION_1 = 0x0032;
constexpr uint16_t REG_UNDERSHOOT_PROTECTION_CONFIGURATION_2 = 0x0033;

constexpr uint8_t CMD_SET_DEFAULT = 0xC1;
constexpr uint8_t CMD_STOP_ALL_ACTIVITIES = 0xC2;
constexpr uint8_t CMD_TRANSMIT_WITH_CRC = 0xC4;
constexpr uint8_t CMD_TRANSMIT_WITHOUT_CRC = 0xC5;
constexpr uint8_t CMD_TRANSMIT_REQA = 0xC6;
constexpr uint8_t CMD_TRANSMIT_WUPA = 0xC7;
constexpr uint8_t CMD_NFC_INITIAL_FIELD_ON = 0xC8;
constexpr uint8_t CMD_RESET_RX_GAIN = 0xD5;
constexpr uint8_t CMD_ADJUST_REGULATORS = 0xD6;
constexpr uint8_t CMD_CLEAR_FIFO = 0xDB;
constexpr uint8_t CMD_REGISTER_SPACEB_ACCESS = 0xFB;
constexpr uint8_t CMD_TEST_ACCESS = 0xFC;

constexpr uint8_t VALID_IDENTIFY_TYPE = 0x05;
constexpr uint8_t OP_TRAILER_MASK = 0x3F;
constexpr uint8_t OP_WRITE_REGISTER = 0x00;
constexpr uint8_t OP_READ_REGISTER = 0x40;
constexpr uint8_t OP_LOAD_FIFO = 0x80;
constexpr uint8_t OP_READ_FIFO = 0x9F;
constexpr uint16_t PREFIX_SPACE_B = static_cast<uint16_t>(CMD_REGISTER_SPACEB_ACCESS) << 8;

constexpr uint8_t sup3v = 0x80;
constexpr uint8_t aat_en = 0x20;
constexpr uint8_t io_drv_lvl = 0x04;
constexpr uint8_t miso_pd1 = 0x08;
constexpr uint8_t miso_pd2 = 0x10;
constexpr uint16_t i2c_thd016 = 0x1000;

constexpr uint8_t en = 0x80;
constexpr uint8_t rx_en = 0x40;
constexpr uint8_t tx_en = 0x08;
constexpr uint8_t wu = 0x04;
constexpr uint8_t nfc_ar8_auto = 0x01;
constexpr uint8_t antcl = 0x01;
constexpr uint8_t no_crc_rx = 0x80;
constexpr uint8_t dis_corr = 0x04;
constexpr uint8_t z_600k = 0x08;
constexpr uint8_t sqm_dyn = 0x20;
constexpr uint8_t agc_en = 0x08;
constexpr uint8_t agc_m = 0x04;
constexpr uint8_t agc6_3 = 0x01;
constexpr uint8_t osc_ok = 0x10;  // official ST25R3916 auxiliary-display osc_ok bit
constexpr uint8_t nrt_step = 0x01;

constexpr uint8_t I_osc = 0x80;
constexpr uint8_t I_wl = 0x40;
constexpr uint8_t I_rxs = 0x20;
constexpr uint8_t I_rxe = 0x10;
constexpr uint8_t I_txe = 0x08;
constexpr uint8_t I_col = 0x04;
constexpr uint8_t I_crc = 0x40;
constexpr uint8_t I_par = 0x20;
constexpr uint8_t I_err1 = 0x02;
constexpr uint8_t I_err2 = 0x01;
constexpr uint8_t I_nre = 0x40;

constexpr uint32_t I_wl32 = static_cast<uint32_t>(I_wl) << 24;
constexpr uint32_t I_rxs32 = static_cast<uint32_t>(I_rxs) << 24;
constexpr uint32_t I_rxe32 = static_cast<uint32_t>(I_rxe) << 24;
constexpr uint32_t I_txe32 = static_cast<uint32_t>(I_txe) << 24;
constexpr uint32_t I_col32 = static_cast<uint32_t>(I_col) << 24;
constexpr uint32_t I_crc32 = static_cast<uint32_t>(I_crc) << 8;
constexpr uint32_t I_par32 = static_cast<uint32_t>(I_par) << 8;
constexpr uint32_t I_err132 = static_cast<uint32_t>(I_err1) << 8;
constexpr uint32_t I_err232 = static_cast<uint32_t>(I_err2) << 8;
constexpr uint32_t I_nre32 = static_cast<uint32_t>(I_nre) << 16;

constexpr uint32_t TIMEOUT_REQ_WUP = 4;
constexpr uint32_t TIMEOUT_SELECT = 8;
constexpr uint32_t TIMEOUT_ANTICOLL = 8;
constexpr uint32_t TIMEOUT_HALT = 2;

constexpr uint8_t ACK_NIBBLE = 0x0A;

inline uint8_t to_read_reg(const uint8_t reg) {
    return (reg & OP_TRAILER_MASK) | OP_READ_REGISTER;
}

inline uint16_t to_read_reg(const uint16_t reg) {
    return (reg & OP_TRAILER_MASK) | PREFIX_SPACE_B | OP_READ_REGISTER;
}

inline uint8_t to_write_reg(const uint8_t reg) {
    return (reg & OP_TRAILER_MASK) | OP_WRITE_REGISTER;
}

inline uint16_t to_write_reg(const uint16_t reg) {
    return (reg & OP_TRAILER_MASK) | PREFIX_SPACE_B | OP_WRITE_REGISTER;
}

inline bool is_irq32_rxe(const uint32_t irq32) { return (irq32 & I_rxe32) != 0; }
inline bool is_irq32_rxs(const uint32_t irq32) { return (irq32 & I_rxs32) != 0; }
inline bool is_irq32_txe(const uint32_t irq32) { return (irq32 & I_txe32) != 0; }
inline bool is_irq32_collision(const uint32_t irq32) { return (irq32 & I_col32) != 0; }
inline bool has_irq32_error(const uint32_t irq32) { return (irq32 & (I_crc32 | I_par32 | I_err132 | I_err232 | I_nre32)) != 0; }
inline bool is_sak_completed_14443_4(const uint8_t sak) { return (sak & 0x24) == 0x20; }
inline bool is_sak_completed(const uint8_t sak) { return (sak & 0x24) == 0x00; }
inline bool has_sak_dependent_bit(const uint8_t sak) { return (sak & 0x04) != 0; }

uint16_t calculate_nrt(const uint32_t ms, const bool nrt_step_enabled) {
    constexpr uint32_t FC_HZ = 13560000;
    constexpr uint64_t STEP64_NUM = 64ULL * 1000000ULL;
    constexpr uint64_t STEP4096_NUM = 4096ULL * 1000000ULL;
    const uint64_t step_num = nrt_step_enabled ? STEP4096_NUM : STEP64_NUM;
    const uint64_t us = static_cast<uint64_t>(ms) * 1000ULL;
    uint64_t nrt = (us * FC_HZ + step_num - 1) / step_num;
    nrt = std::max<uint64_t>(1, std::min<uint64_t>(nrt, 0xFFFFu));
    return static_cast<uint16_t>(nrt);
}

uint8_t calculate_mrt(const uint32_t us) {
    constexpr uint32_t FC_HZ = 13560000;
    constexpr uint32_t STEP64_NUM = 64U * 1000000U;
    uint32_t mrt = (us * FC_HZ + STEP64_NUM - 1) / STEP64_NUM;
    mrt = std::max<uint32_t>(4, std::min<uint32_t>(mrt, 0xFFu));
    return static_cast<uint8_t>(mrt);
}

uint8_t calculate_bcc8(const uint8_t* data, const uint32_t len) {
    uint8_t bcc = 0;
    for (uint32_t i = 0; i < len; ++i) {
        bcc ^= data[i];
    }
    return bcc;
}

std::string BytesToHex(const uint8_t* data, size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

std::string Sha256Hex(const std::string& input) {
    std::array<uint8_t, 32> digest{};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char*>(input.data()), input.size());
    mbedtls_sha256_finish(&ctx, digest.data());
    mbedtls_sha256_free(&ctx);
    return BytesToHex(digest.data(), digest.size());
}

struct PiccUid {
    std::array<uint8_t, 10> uid{};
    uint8_t size = 0;
    uint8_t sak = 0;
    uint16_t atqa = 0;

    std::string ToHexString() const {
        return BytesToHex(uid.data(), size);
    }
};

class St25r3916Reader {
public:
    explicit St25r3916Reader(i2c_master_bus_handle_t bus) : bus_(bus) {}

    bool EnsureReady(std::string* detail = nullptr) {
        if (ready_) {
            return true;
        }
        if (bus_ == nullptr) {
            if (detail != nullptr) {
                *detail = "I2C bus not available";
            }
            ESP_LOGW(TAG, "NFC unit not ready: I2C bus not available");
            return false;
        }
        if (device_ == nullptr) {
            esp_err_t probe = i2c_master_probe(bus_, kDeviceAddress, pdMS_TO_TICKS(200));
            if (probe != ESP_OK) {
                if (detail != nullptr) {
                    *detail = std::string("NFC unit probe failed: ") + esp_err_to_name(probe);
                }
                ESP_LOGW(TAG, "NFC unit probe failed: %s", esp_err_to_name(probe));
                return false;
            }
            i2c_device_config_t cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = kDeviceAddress,
                .scl_speed_hz = 400 * 1000,
                .scl_wait_us = 0,
                .flags = {.disable_ack_check = 0},
            };
            ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &cfg, &device_));
        }
        ready_ = Begin(detail);
        if (ready_) {
            ESP_LOGI(TAG, "ST25R3916 initialized");
        } else if (detail != nullptr && detail->empty()) {
            *detail = "NFC init failed";
        }
        return ready_;
    }

    bool PollCard(std::string& uid_hex) {
        uid_hex.clear();
        if (!EnsureReady()) {
            return false;
        }

        PiccUid picc;
        if (!RequestAndSelect(picc)) {
            return false;
        }

        uid_hex = picc.ToHexString();
        Hlt();
        return !uid_hex.empty();
    }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    bool ready_ = false;
    uint32_t stored_irq_ = 0;

    bool Tx(const uint8_t* data, size_t len, uint32_t timeout_ms = 100) {
        return i2c_master_transmit(device_, data, len, timeout_ms) == ESP_OK;
    }

    bool TxRx(const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, uint32_t timeout_ms = 100) {
        return i2c_master_transmit_receive(device_, tx, tx_len, rx, rx_len, timeout_ms) == ESP_OK;
    }

    bool ReadReg8(uint8_t reg, uint8_t& value) {
        const uint8_t op = to_read_reg(reg);
        return TxRx(&op, 1, &value, 1);
    }

    bool ReadReg8(uint16_t reg, uint8_t& value) {
        const uint8_t op[2] = {static_cast<uint8_t>(to_read_reg(reg) >> 8), static_cast<uint8_t>(to_read_reg(reg) & 0xFF)};
        return TxRx(op, 2, &value, 1);
    }

    bool WriteReg8(uint8_t reg, uint8_t value) {
        const uint8_t buf[2] = {to_write_reg(reg), value};
        return Tx(buf, sizeof(buf));
    }

    bool WriteReg8(uint16_t reg, uint8_t value) {
        const uint8_t buf[3] = {static_cast<uint8_t>(to_write_reg(reg) >> 8), static_cast<uint8_t>(to_write_reg(reg) & 0xFF), value};
        return Tx(buf, sizeof(buf));
    }

    bool ReadReg16(uint8_t reg, uint16_t& value) {
        uint8_t buf[2]{};
        const uint8_t op = to_read_reg(reg);
        if (!TxRx(&op, 1, buf, sizeof(buf))) {
            return false;
        }
        value = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
        return true;
    }

    bool ReadReg32(uint8_t reg, uint32_t& value) {
        uint8_t buf[4]{};
        const uint8_t op = to_read_reg(reg);
        if (!TxRx(&op, 1, buf, sizeof(buf))) {
            return false;
        }
        value = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
                (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
        return true;
    }

    bool WriteReg16(uint8_t reg, uint16_t value) {
        const uint8_t buf[3] = {to_write_reg(reg), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
        return Tx(buf, sizeof(buf));
    }

    bool WriteReg32(uint8_t reg, uint32_t value) {
        const uint8_t buf[5] = {
            to_write_reg(reg),
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>(value & 0xFF),
        };
        return Tx(buf, sizeof(buf));
    }

    bool WriteDirectCommand(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
        std::vector<uint8_t> buf;
        buf.reserve(1 + len);
        buf.push_back(cmd);
        if (data != nullptr && len != 0) {
            buf.insert(buf.end(), data, data + len);
        }
        return Tx(buf.data(), buf.size());
    }

    bool ReadInterrupts(uint32_t& value) {
        value = 0;
        uint8_t error = 0;
        uint16_t main_nfc = 0;
        uint8_t passive = 0;
        if (ReadReg8(REG_ERROR_AND_WAKEUP_INTERRUPT, error) && ReadReg16(REG_MAIN_INTERRUPT, main_nfc) &&
            ReadReg8(REG_PASSIVE_TARGET_INTERRUPT, passive)) {
            value = (static_cast<uint32_t>(main_nfc) << 16) | (static_cast<uint32_t>(error) << 8) | passive;
            return true;
        }
        return false;
    }

    bool ClearInterrupts() {
        stored_irq_ = 0;
        uint32_t discard = 0;
        return ReadReg32(REG_MAIN_INTERRUPT, discard);
    }

    bool ModifyReg8(uint8_t reg, uint8_t set_mask, uint8_t clear_mask) {
        uint8_t value = 0;
        if (!ReadReg8(reg, value)) {
            return false;
        }
        const uint8_t next = (value & ~clear_mask) | set_mask;
        return next == value ? true : WriteReg8(reg, next);
    }

    bool ModifyReg8(uint16_t reg, uint8_t set_mask, uint8_t clear_mask) {
        uint8_t value = 0;
        if (!ReadReg8(reg, value)) {
            return false;
        }
        const uint8_t next = (value & ~clear_mask) | set_mask;
        return next == value ? true : WriteReg8(reg, next);
    }

    bool SetRegBits(uint8_t reg, uint8_t bits) {
        return ModifyReg8(reg, bits, 0x00);
    }

    bool ClearRegBits(uint8_t reg, uint8_t bits) {
        return ModifyReg8(reg, 0x00, bits);
    }

    bool WriteMaskInterrupts(uint32_t value) {
        return WriteReg32(REG_MASK_MAIN_INTERRUPT, value);
    }

    bool WriteNumberOfTransmittedBytes(uint16_t bytes, uint8_t bits) {
        const uint16_t value = static_cast<uint16_t>(((bytes & 0x01FF) << 3) | (bits & 0x07));
        return WriteReg16(REG_NUMBER_OF_TRANSMITTED_BYTES_1, value);
    }

    bool ReadFIFOSize(uint16_t& bytes, uint8_t& bits) {
        bytes = 0;
        bits = 0;
        uint16_t status = 0;
        if (!ReadReg16(REG_FIFO_STATUS_1, status)) {
            return false;
        }
        bytes = (status >> 8) | ((status & 0x00C0) << 2);
        bits = (status >> 1) & 0x07;
        return true;
    }

    bool ReadFIFO(uint16_t& actual, uint8_t* buf, uint16_t buf_size, uint16_t* fifo_bytes = nullptr, uint8_t* fifo_bits = nullptr) {
        actual = 0;
        uint16_t bytes = 0;
        uint8_t bits = 0;
        if (!ReadFIFOSize(bytes, bits)) {
            return false;
        }
        const uint16_t read_size = std::min<uint16_t>(bytes, buf_size);
        if (read_size == 0) {
            return false;
        }
        const uint8_t op = OP_READ_FIFO;
        if (!TxRx(&op, 1, buf, read_size)) {
            return false;
        }
        actual = read_size;
        if (fifo_bytes != nullptr) {
            *fifo_bytes = bytes;
        }
        if (fifo_bits != nullptr) {
            *fifo_bits = bits;
        }
        return true;
    }

    bool WriteFIFO(const uint8_t* buf, uint16_t len) {
        std::vector<uint8_t> tx;
        tx.reserve(1 + len);
        tx.push_back(OP_LOAD_FIFO);
        tx.insert(tx.end(), buf, buf + len);
        return Tx(tx.data(), tx.size());
    }

    uint32_t WaitForInterrupt(uint32_t bits, uint32_t timeout_ms) {
        const int64_t timeout_at = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        do {
            uint32_t irq = 0;
            if (ReadInterrupts(irq)) {
                stored_irq_ |= irq;
            }
            const uint32_t matched = stored_irq_ & bits;
            if (matched != 0) {
                const uint32_t ret = stored_irq_;
                stored_irq_ &= ~matched;
                return ret;
            }
            std::this_thread::yield();
        } while (esp_timer_get_time() <= timeout_at);
        return stored_irq_ | I_nre32;
    }

    bool WaitForFifo(uint32_t timeout_ms, uint16_t required_size) {
        const uint32_t irq = WaitForInterrupt(I_rxe32, timeout_ms);
        const uint16_t need = required_size == 0 ? 1 : required_size;

        if (is_irq32_rxe(irq)) {
            return true;
        }

        if (is_irq32_rxs(irq)) {
            const int64_t timeout_at = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
            uint16_t bytes = 0;
            uint8_t bits = 0;
            do {
                if (ReadFIFOSize(bytes, bits) && bytes >= need) {
                    break;
                }
                std::this_thread::yield();
            } while (esp_timer_get_time() <= timeout_at);
            return ReadFIFOSize(bytes, bits) && bytes >= need;
        }

        if (has_irq32_error(irq)) {
            ESP_LOGW(TAG, "FIFO wait error: 0x%08" PRIx32, irq);
        }
        return false;
    }

    bool WriteFwtTimer(uint32_t timeout_ms) {
        uint8_t timer_ctrl = 0;
        if (!ReadReg8(REG_TIMER_AND_EMV_CONTROL, timer_ctrl)) {
            return false;
        }
        return WriteReg16(REG_NO_RESPONSE_TIMER_1, calculate_nrt(timeout_ms, (timer_ctrl & nrt_step) != 0));
    }

    bool EnableOsc() {
        uint8_t op = 0;
        if (!ReadReg8(REG_OPERATION_CONTROL, op)) {
            return false;
        }
        if ((op & en) == 0) {
            if (!ModifyReg8(REG_MASK_MAIN_INTERRUPT, 0x00, I_osc) || !ClearInterrupts()) {
                return false;
            }
            if (!SetRegBits(REG_OPERATION_CONTROL, en)) {
                return false;
            }
            const uint32_t irq = WaitForInterrupt(static_cast<uint32_t>(I_osc) << 24, 10);
            if ((irq & (static_cast<uint32_t>(I_osc) << 24)) == 0) {
                return false;
            }
        }
        uint8_t aux = 0;
        return ReadReg8(REG_AUXILIARY_DISPLAY, aux) && ((aux & osc_ok) != 0);
    }

    bool NfcInitialFieldOn() {
        uint8_t value = 0;
        if (!ReadReg8(REG_OPERATION_CONTROL, value)) {
            return false;
        }
        if ((value & tx_en) != 0) {
            return true;
        }
        if (!WriteDirectCommand(CMD_NFC_INITIAL_FIELD_ON)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        return ModifyReg8(REG_OPERATION_CONTROL, tx_en | rx_en, 0x00);
    }

    bool ConfigureNfcA() {
        if (!WriteReg8(REG_MODE_DEFINITION, static_cast<uint8_t>((0x01 << 3) | nfc_ar8_auto)) ||
            !WriteReg8(REG_BITRATE_DEFINITION, 0x00) ||
            !WriteReg8(REG_ISO14443A_SETTINGS, 0x00)) {
            return false;
        }

        (void)ClearRegBits(REG_AUXILIARY_DEFINITION, dis_corr);
        (void)WriteReg8(REG_OVERSHOOT_PROTECTION_CONFIGURATION_1, 0x40);
        (void)WriteReg8(REG_OVERSHOOT_PROTECTION_CONFIGURATION_2, 0x03);
        (void)WriteReg8(REG_UNDERSHOOT_PROTECTION_CONFIGURATION_1, 0x40);
        (void)WriteReg8(REG_UNDERSHOOT_PROTECTION_CONFIGURATION_2, 0x03);
        (void)WriteReg8(static_cast<uint16_t>(0x000C), 0x47);  // REG_CORRELATOR_CONFIGURATION_1
        (void)WriteReg8(static_cast<uint16_t>(0x000D), 0x00);  // REG_CORRELATOR_CONFIGURATION_2

        const uint8_t recv3 = 0xD8;
        const uint8_t recv4 = 0x22;

        return WriteReg8(REG_RECEIVER_CONFIGURATION_1, z_600k) &&
               WriteReg8(REG_RECEIVER_CONFIGURATION_2, sqm_dyn | agc_en | agc_m | agc6_3) &&
               WriteReg8(REG_RECEIVER_CONFIGURATION_3, recv3) &&
               WriteReg8(REG_RECEIVER_CONFIGURATION_4, recv4) &&
               WriteDirectCommand(CMD_RESET_RX_GAIN) &&
               WriteMaskInterrupts(0) &&
               NfcInitialFieldOn();
    }

    bool ConfigureFelica(uint8_t bitrate) {
        if (!WriteDirectCommand(CMD_STOP_ALL_ACTIVITIES) ||
            !ModifyReg8(REG_OPERATION_CONTROL, 0x00, tx_en | rx_en)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
        if (!WriteReg8(REG_MODE_DEFINITION, static_cast<uint8_t>((0x03 << 3) | 0x04)) ||
            !WriteReg8(REG_BITRATE_DEFINITION, bitrate) ||
            !WriteReg8(REG_ISO14443B_AND_FELICA_SETTINGS, 0x00) ||
            !ModifyReg8(REG_OPERATION_CONTROL, 0x03, 0x00) ||
            !WriteReg8(REG_IO_CONFIGURATION_1, 0x07) ||
            !WriteReg8(REG_AUXILIARY_DEFINITION, 0x00) ||
            !WriteReg8(REG_RECEIVER_CONFIGURATION_1, 0x13) ||
            !WriteReg8(REG_RECEIVER_CONFIGURATION_2, 0x3D) ||
            !WriteReg8(REG_RECEIVER_CONFIGURATION_3, 0x00) ||
            !WriteReg8(REG_RECEIVER_CONFIGURATION_4, 0x00) ||
            !WriteReg8(static_cast<uint16_t>(0x000C), 0x54) ||  // REG_CORRELATOR_CONFIGURATION_1
            !WriteReg8(static_cast<uint16_t>(0x000D), 0x00) ||  // REG_CORRELATOR_CONFIGURATION_2
            !WriteMaskInterrupts(0) ||
            !NfcInitialFieldOn()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
        return true;
    }

    bool FelicaTransceive(const uint8_t* tx, uint16_t tx_len, uint8_t* rx, uint16_t& rx_len, uint32_t timeout_ms) {
        if (!tx || tx_len == 0 || !rx || rx_len == 0) {
            return false;
        }
        if (!WriteFwtTimer(timeout_ms) ||
            !WriteReg8(REG_ISO14443A_SETTINGS, 0x00) ||
            !ClearRegBits(REG_AUXILIARY_DEFINITION, no_crc_rx) ||
            !ClearInterrupts() ||
            !WriteDirectCommand(CMD_CLEAR_FIFO) ||
            !WriteFIFO(tx, tx_len) ||
            !WriteNumberOfTransmittedBytes(tx_len, 0) ||
            !WriteDirectCommand(CMD_TRANSMIT_WITH_CRC)) {
            return false;
        }

        const uint32_t tx_irq = WaitForInterrupt(I_txe32, timeout_ms);
        if (!is_irq32_txe(tx_irq)) {
            return false;
        }

        if (!WaitForFifo(timeout_ms, 1)) {
            return false;
        }
        uint16_t actual = 0;
        if (!ReadFIFO(actual, rx, rx_len)) {
            return false;
        }
        rx_len = actual;
        return true;
    }

    bool FelicaPoll(uint16_t system_code, uint8_t bitrate, std::array<uint8_t, 8>& idm, std::array<uint8_t, 8>& pmm) {
        if (!ConfigureFelica(bitrate)) {
            return false;
        }

        const uint8_t tx[5] = {0x00, static_cast<uint8_t>(system_code >> 8), static_cast<uint8_t>(system_code & 0xFF), 0x00, 0x00};
        uint8_t rx[32]{};
        uint16_t rx_len = sizeof(rx);
        if (!FelicaTransceive(tx, sizeof(tx), rx, rx_len, 50)) {
            return false;
        }
        if (rx_len < 18 || rx[1] != 0x01) {
            return false;
        }
        memcpy(idm.data(), rx + 2, 8);
        memcpy(pmm.data(), rx + 10, 8);
        return true;
    }

    bool FelicaReadWithoutEncryption(const std::array<uint8_t, 8>& idm,
                                     const uint8_t service_code[2],
                                     std::array<uint8_t, 16>& block,
                                     uint8_t bitrate) {
        if (!ConfigureFelica(bitrate)) {
            return false;
        }

        uint8_t tx[15] = {
            0x06,
        };
        memcpy(tx + 1, idm.data(), 8);
        tx[9] = 0x01;
        tx[10] = service_code[0];
        tx[11] = service_code[1];
        tx[12] = 0x01;
        tx[13] = 0x80;
        tx[14] = 0x00;

        uint8_t rx[64]{};
        uint16_t rx_len = sizeof(rx);
        if (!FelicaTransceive(tx, sizeof(tx), rx, rx_len, 30)) {
            return false;
        }
        if (rx_len < 29 || rx[1] != 0x07) {
            return false;
        }
        if (rx[10] != 0x00 || rx[11] != 0x00 || rx[12] < 1) {
            return false;
        }
        memcpy(block.data(), rx + 13, 16);
        return true;
    }
public:
    bool TryReadTransitIcBalance(TransitIcBalanceInfo& info) {
        info = {};

        std::array<uint8_t, 8> idm{};
        std::array<uint8_t, 8> pmm{};
        std::array<uint8_t, 16> block{};
        const std::array<uint16_t, 3> system_codes = {0x0003, 0x80DE, 0xFFFF};
        const std::array<std::array<uint8_t, 2>, 4> service_codes = {{{{0x8B, 0x00}}, {{0x00, 0x8B}}, {{0x0F, 0x09}}, {{0x09, 0x0F}}}};
        const std::array<uint8_t, 1> bitrates = {0x11};

        bool success = false;
        std::string last_error = "FeliCa card not found";

        for (uint8_t bitrate : bitrates) {
            for (const uint16_t system_code : system_codes) {
                if (!FelicaPoll(system_code, bitrate, idm, pmm)) {
                    char buf[64]{};
                    snprintf(buf, sizeof(buf), "polling failed (bitrate=212k, system=%04X)", system_code);
                    last_error = buf;
                    continue;
                }

                info.idm_hex = BytesToHex(idm.data(), idm.size());
                char sysbuf[8]{};
                snprintf(sysbuf, sizeof(sysbuf), "%04X", system_code);
                info.system_code_hex = sysbuf;
                info.detail = "FeliCa polling OK";

                for (const auto& service_code : service_codes) {
                    if (!FelicaReadWithoutEncryption(idm, service_code.data(), block, bitrate)) {
                        last_error = "read without encryption failed";
                        continue;
                    }

                    // M5Unit-NFC's Japanese transportation IC sample decodes the balance
                    // service (0x008B) from block offsets 11-12 as little-endian:
                    //   balance = (buf[12] << 8) | buf[11]
                    // Example: raw 2E 01 means 302 yen.
                    const uint16_t balance = static_cast<uint16_t>(block[11]) | (static_cast<uint16_t>(block[12]) << 8);

                    info.success = true;
                    info.balance_yen = balance;
                    info.detail = "IDm=" + info.idm_hex + " / raw_balance=" + BytesToHex(block.data() + 11, 2) + " / svc=" + BytesToHex(service_code.data(), 2);
                    success = true;
                    break;
                }

                if (success) {
                    break;
                }
            }

            if (success) {
                break;
            }
        }

        (void)ConfigureNfcA();
        if (!success) {
            info.detail = last_error;
        }
        return success;
    }

    bool Begin(std::string* detail = nullptr) {
        uint8_t ident = 0;

        if (!ReadReg8(REG_IC_IDENTITY, ident)) {
            ESP_LOGE(TAG, "Failed to read IC identity");
            if (detail != nullptr) {
                *detail = "Failed to read IC identity";
            }
            return false;
        }
        ESP_LOGI(TAG, "IC identity raw=0x%02x", ident);
        const uint8_t type = (ident >> 3) & 0x1F;
        const uint8_t rev = ident & 0x07;
        if (type != VALID_IDENTIFY_TYPE || rev == 0) {
            ESP_LOGE(TAG, "Unexpected ST25R3916 identity type=%u rev=%u raw=0x%02x", type, rev, ident);
            if (detail != nullptr) {
                *detail = "Unexpected ST25R3916 identity: raw=0x" + BytesToHex(&ident, 1);
            }
            return false;
        }

        if (!WriteDirectCommand(CMD_SET_DEFAULT)) {
            if (detail != nullptr) {
                *detail = "CMD_SET_DEFAULT failed";
            }
            return false;
        }
        const uint8_t protection_cmd[] = {0x04, 0x10};
        if (!WriteDirectCommand(CMD_TEST_ACCESS, protection_cmd, sizeof(protection_cmd))) {
            if (detail != nullptr) {
                *detail = "CMD_TEST_ACCESS failed";
            }
            return false;
        }

        const uint16_t io_config = i2c_thd016 | io_drv_lvl | sup3v;
        if (!WriteReg16(REG_IO_CONFIGURATION_1, io_config)) {
            if (detail != nullptr) {
                *detail = "REG_IO_CONFIGURATION_1 write failed";
            }
            return false;
        }
        if (!WriteReg8(REG_TX_DRIVER, 0x80)) {
            if (detail != nullptr) {
                *detail = "REG_TX_DRIVER write failed";
            }
            return false;
        }
        (void)ModifyReg8(REG_IO_CONFIGURATION_1, 0x07, 0x07);
        (void)WriteReg8(REG_RESISTIVE_AM_MODULATION, 0x80);
        (void)SetRegBits(REG_IO_CONFIGURATION_2, aat_en);
        (void)WriteReg8(REG_RESISTIVE_AM_MODULATION, 0x00);
        (void)WriteReg8(REG_EXTERNAL_FIELD_DETECTOR_ACTIVATION_THRESHOLD, 0x13);
        (void)WriteReg8(REG_EXTERNAL_FIELD_DETECTOR_DEACTIVATION_THRESHOLD, 0x02);
        (void)ModifyReg8(REG_MODE_DEFINITION, 0x50, 0xF0);  // matches source's FDT setup target register effect enough
        (void)WriteReg8(REG_PASSIVE_TARGET_MODULATION, 0x5F);
        (void)WriteReg8(REG_EMD_SUPPRESSION_CONFIGURATION, 0x40);
        (void)WriteReg8(static_cast<uint16_t>(0x0026), 0x82);
        (void)WriteReg8(static_cast<uint16_t>(0x0027), 0x82);
        (void)SetRegBits(REG_OPERATION_CONTROL, 0x03);
        (void)WriteDirectCommand(CMD_CLEAR_FIFO);

        if (!WriteMaskInterrupts(0xFFFF00FF) || !ClearInterrupts()) {
            if (detail != nullptr) {
                *detail = "Interrupt setup failed";
            }
            return false;
        }
        if (!EnableOsc()) {
            if (detail != nullptr) {
                *detail = "EnableOsc failed";
            }
            return false;
        }
        if (!WriteMaskInterrupts(0)) {
            if (detail != nullptr) {
                *detail = "Unmask interrupts failed";
            }
            return false;
        }
        if (!WriteDirectCommand(CMD_ADJUST_REGULATORS)) {
            if (detail != nullptr) {
                *detail = "CMD_ADJUST_REGULATORS failed";
            }
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));

        uint8_t reg_display = 0;
        if (ReadReg8(REG_REGULATOR_DISPLAY, reg_display)) {
            ESP_LOGI(TAG, "Regulator display: 0x%02x", reg_display);
        }

        if (!WriteReg8(REG_MASK_RECEIVER_TIMER, calculate_mrt(302))) {
            if (detail != nullptr) {
                *detail = "REG_MASK_RECEIVER_TIMER write failed";
            }
            return false;
        }
        if (!ConfigureNfcA()) {
            if (detail != nullptr && detail->empty()) {
                *detail = "ConfigureNfcA failed";
            }
            return false;
        }
        return true;
    }

    bool NfcaRequestWakeup(uint16_t& atqa, bool request_mode) {
        atqa = 0;
        if (!WriteFwtTimer(TIMEOUT_REQ_WUP) ||
            !WriteReg8(REG_ISO14443A_SETTINGS, antcl) ||
            !SetRegBits(REG_AUXILIARY_DEFINITION, no_crc_rx) ||
            !ClearInterrupts() ||
            !WriteDirectCommand(CMD_CLEAR_FIFO) ||
            !WriteDirectCommand(request_mode ? CMD_TRANSMIT_REQA : CMD_TRANSMIT_WUPA)) {
            return false;
        }

        uint32_t irq = WaitForInterrupt(I_rxe32 | I_rxs32 | I_col32, TIMEOUT_REQ_WUP);
        if (!is_irq32_rxe(irq) && is_irq32_rxs(irq)) {
            const int64_t timeout_at = esp_timer_get_time() + static_cast<int64_t>(TIMEOUT_REQ_WUP) * 1000;
            uint16_t bytes = 0;
            uint8_t bits = 0;
            do {
                if (ReadFIFOSize(bytes, bits) && bytes >= 2) {
                    irq |= I_rxe32;
                    break;
                }
                std::this_thread::yield();
            } while (esp_timer_get_time() <= timeout_at);
        }

        if (!is_irq32_rxe(irq)) {
            return false;
        }

        uint8_t buf[2]{};
        uint16_t actual = 0;
        if (!ReadFIFO(actual, buf, sizeof(buf)) || actual != 2) {
            return false;
        }
        atqa = (static_cast<uint16_t>(buf[1]) << 8) | buf[0];
        return true;
    }

    bool NfcaTransceive(uint8_t* rx, uint16_t& rx_len, const uint8_t* tx, uint16_t tx_len, uint32_t timeout_ms) {
        if (!tx || tx_len == 0 || !rx || rx_len == 0) {
            return false;
        }
        if (!WriteFwtTimer(timeout_ms) ||
            !WriteReg8(REG_ISO14443A_SETTINGS, 0x00) ||
            !ClearRegBits(REG_AUXILIARY_DEFINITION, no_crc_rx) ||
            !ClearInterrupts() ||
            !WriteDirectCommand(CMD_CLEAR_FIFO) ||
            !WriteFIFO(tx, tx_len) ||
            !WriteNumberOfTransmittedBytes(tx_len, 0) ||
            !WriteDirectCommand(CMD_TRANSMIT_WITH_CRC)) {
            return false;
        }
        if (!WaitForFifo(timeout_ms, 1)) {
            return false;
        }
        uint16_t actual = 0;
        if (!ReadFIFO(actual, rx, rx_len)) {
            return false;
        }
        rx_len = actual;
        return true;
    }

    bool NfcaAntiCollision(uint8_t rbuf[5], uint8_t level) {
        if (!rbuf || level < 1 || level > 3) {
            return false;
        }
        if (!WriteFwtTimer(TIMEOUT_ANTICOLL) ||
            !WriteReg8(REG_ISO14443A_SETTINGS, antcl) ||
            !ClearRegBits(REG_AUXILIARY_DEFINITION, no_crc_rx)) {
            return false;
        }

        uint8_t frame[7] = {static_cast<uint8_t>(0x91 + level * 2), 0x20};
        uint32_t sbytes = 2;
        uint32_t sbits = 0;
        uint8_t offset = 0;
        uint32_t guard = 32;
        bool collision = false;
        uint8_t coll_byte = 1;

        do {
            if (!ClearInterrupts() ||
                !WriteDirectCommand(CMD_CLEAR_FIFO) ||
                !WriteFIFO(frame, sbytes + (sbits != 0 ? 1 : 0)) ||
                !WriteNumberOfTransmittedBytes(sbytes, sbits) ||
                !WriteDirectCommand(CMD_TRANSMIT_WITHOUT_CRC)) {
                return false;
            }

            const uint32_t irq = WaitForInterrupt(I_rxe32 | I_col32, TIMEOUT_ANTICOLL);
            collision = is_irq32_collision(irq);
            if (!collision && !is_irq32_rxe(irq)) {
                return false;
            }

            uint16_t actual = 0;
            uint8_t cd = 0;
            if (!ReadFIFO(actual, rbuf + offset, 5 - offset) || actual == 0 || !ReadReg8(REG_COLLISION_DISPLAY, cd)) {
                return false;
            }

            if (collision) {
                const uint8_t cbytes = (cd >> 4) & 0x0F;
                const uint8_t cbits = (cd >> 1) & 0x07;
                if (actual != 0) {
                    coll_byte = rbuf[offset + actual - 1];
                    coll_byte |= static_cast<uint8_t>(1U << cbits);
                }
                sbytes = cbytes + (cbits == 0x07 ? 1 : 0);
                sbits = (cbits + 1) & 0x07;
                frame[1] = static_cast<uint8_t>((sbytes << 4) | sbits);
                memcpy(frame + 2 + offset, rbuf + offset, actual);
                frame[sbytes] = coll_byte;
                offset = actual - 1;
            }

            if (sbits != 0) {
                rbuf[offset] >>= sbits;
                rbuf[offset] <<= sbits;
                rbuf[offset] |= coll_byte;
            }
        } while (collision && guard-- > 0);

        return !collision;
    }

    bool NfcaSelectWithAnticollision(bool& completed, PiccUid& picc, uint8_t level) {
        completed = false;
        if (level < 1 || level > 3) {
            return false;
        }

        uint8_t rbuf[5]{};
        if (!NfcaAntiCollision(rbuf, level)) {
            return false;
        }

        memcpy(picc.uid.data() + (level - 1) * 3, rbuf + (rbuf[0] == 0x88 ? 1 : 0), 4 - (rbuf[0] == 0x88 ? 1 : 0));

        uint8_t select_frame[7] = {static_cast<uint8_t>(0x91 + level * 2), 0x70};
        memcpy(select_frame + 2, rbuf, sizeof(rbuf));

        uint16_t rx_len = 3;
        if (!NfcaTransceive(rbuf, rx_len, select_frame, sizeof(select_frame), TIMEOUT_SELECT) || rx_len != 3) {
            return false;
        }

        const uint8_t sak = rbuf[0];
        if (is_sak_completed_14443_4(sak) || is_sak_completed(sak)) {
            picc.size = 1 + level * 3;
            picc.sak = sak;
            completed = true;
            return true;
        }
        return has_sak_dependent_bit(sak);
    }

    bool Hlt() {
        const uint8_t frame[2] = {0x50, 0x00};
        if (!WriteFwtTimer(TIMEOUT_HALT) ||
            !WriteReg8(REG_ISO14443A_SETTINGS, 0x00) ||
            !ClearRegBits(REG_AUXILIARY_DEFINITION, no_crc_rx) ||
            !ClearInterrupts() ||
            !WriteDirectCommand(CMD_CLEAR_FIFO) ||
            !WriteFIFO(frame, sizeof(frame)) ||
            !WriteNumberOfTransmittedBytes(sizeof(frame), 0) ||
            !WriteDirectCommand(CMD_TRANSMIT_WITH_CRC)) {
            return false;
        }
        const uint32_t irq = WaitForInterrupt(I_txe32, TIMEOUT_HALT);
        return is_irq32_txe(irq);
    }

    bool RequestAndSelect(PiccUid& picc) {
        uint16_t atqa = 0;
        if (!NfcaRequestWakeup(atqa, true)) {
            return false;
        }
        picc = {};
        picc.atqa = atqa;

        bool completed = false;
        uint8_t level = 1;
        do {
            if (!NfcaSelectWithAnticollision(completed, picc, level)) {
                return false;
            }
            ++level;
        } while (!completed && level < 4);

        if (!completed || (picc.size != 4 && picc.size != 7 && picc.size != 10)) {
            return false;
        }
        return true;
    }
};

struct CardState {
    bool present = false;
    bool reported = false;
    int64_t last_report_attempt_us = 0;
    std::string uid_hex;
    std::string hash_hex;
};

class NfcServiceImpl {
public:
    void Start() {
        if (task_started_) {
            return;
        }
        task_started_ = true;
        xTaskCreate(&NfcServiceImpl::TaskEntry, "nfc_service", 8192, this, 4, nullptr);
    }

    bool TryReadTransitIcBalance(TransitIcBalanceInfo& info) {
        auto bus = hal_bridge::board_get_i2c_bus();
        St25r3916Reader reader(bus);
        std::string ready_detail;
        if (!reader.EnsureReady(&ready_detail)) {
            info.detail = ready_detail.empty() ? "NFC reader not ready" : ready_detail;
            return false;
        }
        return reader.TryReadTransitIcBalance(info);
    }

private:
    bool task_started_ = false;

    static void TaskEntry(void* arg) {
        static_cast<NfcServiceImpl*>(arg)->Run();
        vTaskDelete(nullptr);
    }

    void Run() {
        auto bus = hal_bridge::board_get_i2c_bus();
        St25r3916Reader reader(bus);
        CardState state;

        while (true) {
            if (!reader.EnsureReady()) {
                vTaskDelay(pdMS_TO_TICKS(kProbeRetryMs));
                continue;
            }

            std::string uid_hex;
            const bool detected = reader.PollCard(uid_hex);
            const int64_t now_us = esp_timer_get_time();

            if (detected) {
                const std::string hash_hex = Sha256Hex(uid_hex);
                if (!state.present || state.uid_hex != uid_hex) {
                    if (state.present && state.reported) {
                        (void)Application::GetInstance().SendCompanionNfcAuthRemoved(kReaderId, state.hash_hex);
                    }
                    state.present = true;
                    state.reported = false;
                    state.last_report_attempt_us = 0;
                    state.uid_hex = uid_hex;
                    state.hash_hex = hash_hex;
                    ESP_LOGI(TAG, "NFC card detected uid=%s", uid_hex.c_str());
                }

                if (!state.reported && (state.last_report_attempt_us == 0 || now_us - state.last_report_attempt_us >= static_cast<int64_t>(kRetryIntervalMs) * 1000)) {
                    state.last_report_attempt_us = now_us;
                    state.reported = Application::GetInstance().SendCompanionNfcAuthDetected(kReaderId, state.hash_hex, kCardConfidence, state.uid_hex, state.uid_hex);
                    ESP_LOGI(TAG, "NFC detected event send result=%d uid=%s", state.reported, state.uid_hex.c_str());
                }
            } else if (state.present) {
                if (state.reported) {
                    (void)Application::GetInstance().SendCompanionNfcAuthRemoved(kReaderId, state.hash_hex);
                    ESP_LOGI(TAG, "NFC card removed uid=%s", state.uid_hex.c_str());
                }
                state = {};
            }

            vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
        }
    }
};

NfcServiceImpl& Impl() {
    static NfcServiceImpl instance;
    return instance;
}

}  // namespace

NfcService& NfcService::GetInstance() {
    static NfcService instance;
    return instance;
}

void NfcService::Start() {
    Impl().Start();
}

bool NfcService::TryReadTransitIcBalance(TransitIcBalanceInfo& info) {
    std::lock_guard<std::mutex> lock(g_nfc_bus_mutex);
    return Impl().TryReadTransitIcBalance(info);
}

}  // namespace hal
