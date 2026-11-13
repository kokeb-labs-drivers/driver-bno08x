/**
 * @file ports.cpp
 * @brief The Porting page's SPI and UART ports, compiled by the host build (never run).
 */
#include <cstddef>
#include <cstdint>

#include "bno08x.hpp"

bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx,
                           std::size_t len) noexcept; // CS low .. high
void platform_wake(bool high) noexcept;
bool platform_int_low_spi() noexcept;
std::size_t platform_uart_read(uint8_t* data,
                               std::size_t cap) noexcept; // bytes waiting, never blocks
bool platform_uart_put(uint8_t byte) noexcept;            // false if the FIFO is full
uint32_t platform_now_us() noexcept;
bool platform_ready() noexcept;

// --8<-- [start:spi]
class MySpiPort : public bno08x::HubInterface<MySpiPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::Spi;
  bool EnsureInitialized() noexcept {
    return platform_ready();
  }
  uint32_t NowUs() noexcept {
    return platform_now_us();
  }
  // One full-duplex transfer with chip select held low for all `len` bytes (SPI mode 3, up to 3
  // MHz).
  bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }
  // WAKE (PS0) is driven low to wake the hub and high otherwise.
  bool SetWake(bool level) noexcept {
    platform_wake(level);
    return true;
  }
  bool IntAsserted() noexcept {
    return platform_int_low_spi();
  }
};
// --8<-- [end:spi]

// --8<-- [start:uart]
class MyUartPort : public bno08x::HubInterface<MyUartPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::UartShtp;
  bool EnsureInitialized() noexcept {
    return platform_ready();
  }
  uint32_t NowUs() noexcept {
    return platform_now_us();
  }
  // Whatever bytes have arrived, at most `cap`. Return 0 when there are none: never wait.
  std::size_t UartRead(uint8_t* data, std::size_t cap) noexcept {
    return platform_uart_read(data, cap);
  }
  // One byte at a time: the driver keeps the 100 us gap the hub needs between host bytes.
  bool UartWriteByte(uint8_t byte) noexcept {
    return platform_uart_put(byte);
  }
};
// --8<-- [end:uart]

// --8<-- [start:rvc]
class MyRvcPort : public bno08x::HubInterface<MyRvcPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::UartRvc;
  bool EnsureInitialized() noexcept {
    return platform_ready();
  }
  uint32_t NowUs() noexcept {
    return platform_now_us();
  }
  std::size_t UartRead(uint8_t* data, std::size_t cap) noexcept {
    return platform_uart_read(data, cap);
  }
};
// --8<-- [end:rvc]

static_assert(bno08x::HubPort<MySpiPort>);
static_assert(bno08x::HubPort<MyUartPort>);
static_assert(bno08x::HubPort<MyRvcPort>);
