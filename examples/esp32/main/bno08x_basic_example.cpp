/**
 * @file bno08x_basic_example.cpp
 * @brief Start a BNO08x on I2C, enable the rotation vector at 50 Hz and log yaw, pitch and roll in
 * centidegrees.
 *
 * Wiring: examples/esp32/README.md. Expected output: a "ready" line, then one line of angles per
 * sample. Every Result is checked and logged by name. Not verified on hardware.
 */
#include <cinttypes>
#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bno08x.hpp"
#include "bno08x_kconfig.hpp"
#include "esp32_bno08x_port.hpp"

namespace {
constexpr const char* kTag = "bno08x_example";
}

extern "C" void app_main(void) {
  static Esp32Bno08xPort port; // static: the driver keeps a reference
  static bno08x::Bno08x<Esp32Bno08xPort, bno08x::KconfigLimits> hub(port); // no I/O

  ESP_LOGI(kTag, "BNO08x driver %s", bno08x::GetDriverVersion());
  const auto begin = hub.Begin(bno08x::KconfigDefaults());
  if (!begin.ok()) {
    ESP_LOGE(kTag, "Begin failed: %s", bno08x::ToString(begin.error));
    return;
  }

  bool enabled = false;
  while (true) {
    hub.Poll(static_cast<uint32_t>(esp_timer_get_time()));
    if (hub.GetState() == bno08x::State::Faulted) {
      ESP_LOGW(kTag, "start-up failed, trying again");
      static_cast<void>(hub.Begin(bno08x::KconfigDefaults()));
      enabled = false;
    } else if (hub.Ready() && !enabled) {
      bno08x::FeatureConfig fc{};
      fc.interval_us = 20000; // 50 Hz
      const auto r = hub.SetFeature(bno08x::SensorId::RotationVector, fc);
      enabled = r.ok();
      ESP_LOGI(kTag, "ready; enable rotation vector: %s", bno08x::ToString(r.error));
    } else {
      // Nothing to do this pass.
    }

    bno08x::Event e{};
    while (hub.PopEvent(e)) {
      if (e.kind == bno08x::EventKind::Sensor) {
        const auto a = bno08x::ToEulerCentiDeg(e.v[0], e.v[1], e.v[2], e.v[3]);
        ESP_LOGI(kTag,
                 "yaw %" PRId32 " pitch %" PRId32 " roll %" PRId32
                 " (centidegrees), accuracy %" PRIu32,
                 a.yaw, a.pitch, a.roll, static_cast<uint32_t>(e.accuracy));
      } else if (e.kind == bno08x::EventKind::OpDone) {
        ESP_LOGI(kTag, "operation %" PRIu32 ": %s", static_cast<uint32_t>(e.op),
                 bno08x::ToString(e.error));
      } else {
        // Other events are not used by this example.
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
