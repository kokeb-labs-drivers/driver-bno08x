/**
 * @file esp32_bno08x_port.hpp
 * @brief ESP-IDF I2C port for the BNO08x driver: i2c_master for the bus, GPIO for NRST and H_INTN,
 * an interrupt timestamp for H_INTN and ESP_LOG for the log hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Bno08xPins; the defaults match
 * the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1). Not built or run here: CI
 * builds it with ESP-IDF, and a bench run on the board confirms it (not verified on hardware).
 */
#ifndef BNO08X_ESP32_BNO08X_PORT_HPP
#define BNO08X_ESP32_BNO08X_PORT_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bno08x.hpp"

/// The SCL clock of this example: the datasheet allows up to 400 kbit/s.
inline constexpr uint32_t kEsp32Bno08xClockHz = 400'000;
static_assert(kEsp32Bno08xClockHz <= bno08x::i2c::kMaxClockHz,
              "SCL above what the BNO08x data sheet allows");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32Bno08xPins {
  i2c_port_num_t port = I2C_NUM_0;
  gpio_num_t sda = GPIO_NUM_6;
  gpio_num_t scl = GPIO_NUM_7;
  gpio_num_t h_intn = GPIO_NUM_10; ///< H_INTN, input (required for I2C).
  gpio_num_t nrst =
      GPIO_NUM_11; ///< NRST, output. Tied high: use `Esp32Bno08xPortNoReset` (unused).
  uint32_t clock_hz = kEsp32Bno08xClockHz; ///< Up to 400 kHz.
  int32_t timeout_ms = 20; ///< Per transaction; above the hub's longest clock stretch.
};

/**
 * The BNO08x I2C port on ESP-IDF, with the interrupt-timestamp and log hooks, and the NRST hook
 * when `kNrstWired`. Without the NRST hook the driver joins a running hub and asks for its
 * advertisement; a hook that only pretended to reset would make it wait for an advertisement that
 * never comes.
 */
template <bool kNrstWired>
class Esp32Bno08xPortT : public bno08x::HubInterface<Esp32Bno08xPortT<kNrstWired>> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;

  explicit Esp32Bno08xPortT(const Esp32Bno08xPins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (bus_ != nullptr) {
      return true;
    }
    i2c_master_bus_config_t bus{};
    bus.i2c_port = pins_.port;
    bus.sda_io_num = pins_.sda;
    bus.scl_io_num = pins_.scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus, &bus_) != ESP_OK) {
      bus_ = nullptr;
      return false;
    }
    return configurePins();
  }

  uint32_t NowUs() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time()); // wraps at 2^32; the driver only subtracts
  }

  bool I2cWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) && (i2c_master_transmit(dev, data, len, pins_.timeout_ms) == ESP_OK);
  }

  bool I2cRead(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) && (i2c_master_receive(dev, data, len, pins_.timeout_ms) == ESP_OK);
  }

  bool IntAsserted() noexcept {
    return gpio_get_level(pins_.h_intn) == 0; // active low
  }

  // The time of the latest H_INTN falling edge, taken in the interrupt and consumed once.
  bool TakeIntTimestamp(uint32_t& us) noexcept {
    if (!edge_seen_.exchange(false)) {
      return false;
    }
    us = edge_us_.load();
    return true;
  }

  bool SetReset(bool level) noexcept
    requires(kNrstWired)
  {
    return gpio_set_level(pins_.nrst, level ? 1U : 0U) == ESP_OK;
  }

  void Log(bno08x::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = (level == bno08x::LogLevel::Error)     ? ESP_LOG_ERROR
                                      : (level == bno08x::LogLevel::Warning) ? ESP_LOG_WARN
                                      : (level == bno08x::LogLevel::Info)    ? ESP_LOG_INFO
                                                                             : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  bool configurePins() noexcept {
    if constexpr (kNrstWired) {
      gpio_config_t out{};
      out.pin_bit_mask = 1ULL << static_cast<uint32_t>(pins_.nrst);
      out.mode = GPIO_MODE_OUTPUT;
      // NRST is driven to its inactive level (high) before its output is enabled, so configuring
      // the pin never pulses a reset.
      if ((gpio_set_level(pins_.nrst, 1U) != ESP_OK) || (gpio_config(&out) != ESP_OK)) {
        return false;
      }
    }
    gpio_config_t in{};
    in.pin_bit_mask = 1ULL << static_cast<uint32_t>(pins_.h_intn);
    in.mode = GPIO_MODE_INPUT;
    in.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&in) != ESP_OK) {
      return false;
    }
    const esp_err_t svc = gpio_install_isr_service(0);
    if ((svc != ESP_OK) && (svc != ESP_ERR_INVALID_STATE)) { // already installed is fine
      return false;
    }
    return gpio_isr_handler_add(pins_.h_intn, &Esp32Bno08xPortT::onEdge, this) == ESP_OK;
  }

  static void IRAM_ATTR onEdge(void* self) {
    auto* port = static_cast<Esp32Bno08xPortT*>(self);
    port->edge_us_.store(static_cast<uint32_t>(esp_timer_get_time()));
    port->edge_seen_.store(true);
  }

  // The device handle for one 7-bit address, created on first use (one chip per port in this
  // example).
  i2c_master_dev_handle_t device(uint8_t addr) noexcept {
    if ((device_ != nullptr) && (device_addr_ == addr)) {
      return device_;
    }
    if (device_ != nullptr) {
      static_cast<void>(i2c_master_bus_rm_device(device_));
      device_ = nullptr;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = pins_.clock_hz;
    if (i2c_master_bus_add_device(bus_, &cfg, &device_) != ESP_OK) {
      device_ = nullptr;
      return nullptr;
    }
    device_addr_ = addr;
    return device_;
  }

  static constexpr const char* kTag = "bno08x";
  Esp32Bno08xPins pins_;
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  uint8_t device_addr_ = 0;
  std::atomic<uint32_t> edge_us_{0};
  std::atomic<bool> edge_seen_{false};
};

/// NRST wired to a GPIO: `Begin()` pulses it.
using Esp32Bno08xPort = Esp32Bno08xPortT<true>;
/// NRST tied high: no reset hook, `HardReset()` returns `Unsupported`.
using Esp32Bno08xPortNoReset = Esp32Bno08xPortT<false>;

#endif // BNO08X_ESP32_BNO08X_PORT_HPP
