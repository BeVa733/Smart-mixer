#include <Arduino.h>
#include <Wire.h>

#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_timer.h>
#include <lvgl.h>

namespace {

constexpr uint16_t SCREEN_WIDTH = 800;
constexpr uint16_t SCREEN_HEIGHT = 480;
constexpr uint32_t PIXEL_CLOCK_HZ = 24000000;
constexpr uint16_t BOUNCE_BUFFER_LINES = 10;
constexpr size_t RGB565_BYTES_PER_PIXEL = 2;
constexpr size_t FRAME_BUFFER_BYTES =
    SCREEN_WIDTH * SCREEN_HEIGHT * RGB565_BYTES_PER_PIXEL;
constexpr uint8_t BACKLIGHT_PIN = 2;

constexpr uint8_t I2C_SDA_PIN = 19;
constexpr uint8_t I2C_SCL_PIN = 20;
constexpr uint32_t I2C_FREQUENCY = 400000;

constexpr uint8_t PCA9557_ADDRESS = 0x18;
constexpr uint8_t PCA9557_OUTPUT_REGISTER = 0x01;
constexpr uint8_t PCA9557_CONFIG_REGISTER = 0x03;

constexpr uint8_t GT911_PRIMARY_ADDRESS = 0x5D;
constexpr uint8_t GT911_SECONDARY_ADDRESS = 0x14;
constexpr uint16_t GT911_STATUS_REGISTER = 0x814E;
constexpr uint16_t GT911_FIRST_POINT_REGISTER = 0x814F;

class CrowPanelRgbDisplay {
public:
  bool begin() {
    esp_lcd_rgb_panel_config_t config = {};
    config.clk_src = LCD_CLK_SRC_PLL160M;
    config.timings.pclk_hz = PIXEL_CLOCK_HZ;
    config.timings.h_res = SCREEN_WIDTH;
    config.timings.v_res = SCREEN_HEIGHT;
    config.timings.hsync_pulse_width = 48;
    config.timings.hsync_back_porch = 40;
    config.timings.hsync_front_porch = 40;
    config.timings.vsync_pulse_width = 31;
    config.timings.vsync_back_porch = 13;
    config.timings.vsync_front_porch = 1;
    config.timings.flags.hsync_idle_low = 0;
    config.timings.flags.vsync_idle_low = 0;
    config.timings.flags.de_idle_high = 0;
    config.timings.flags.pclk_active_neg = 1;
    config.timings.flags.pclk_idle_high = 0;

    config.data_width = 16;
    config.bits_per_pixel = 16;
    config.num_fbs = 2;
    config.bounce_buffer_size_px = SCREEN_WIDTH * BOUNCE_BUFFER_LINES;
    config.dma_burst_size = 64;
    config.hsync_gpio_num = 39;
    config.vsync_gpio_num = 40;
    config.de_gpio_num = 41;
    config.pclk_gpio_num = 0;
    config.disp_gpio_num = -1;

    const int8_t dataPins[16] = {
        15, 7,  6,  5,  4,     // B0-B4
        9,  46, 3,  8,  16, 1, // G0-G5
        14, 21, 47, 48, 45,    // R0-R4
    };
    for (uint8_t index = 0; index < 16; ++index) {
      config.data_gpio_nums[index] = dataPins[index];
    }
    config.flags.fb_in_psram = 1;

    esp_err_t result = esp_lcd_new_rgb_panel(&config, &panelHandle_);
    if (result == ESP_OK) {
      esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
      callbacks.on_vsync = onVSync;
      result = esp_lcd_rgb_panel_register_event_callbacks(panelHandle_,
                                                          &callbacks, this);
    }
    if (result == ESP_OK) {
      result = esp_lcd_panel_reset(panelHandle_);
    }
    if (result == ESP_OK) {
      result = esp_lcd_panel_init(panelHandle_);
    }
    if (result == ESP_OK) {
      result = esp_lcd_rgb_panel_get_frame_buffer(
          panelHandle_, 2, reinterpret_cast<void **>(&frameBuffers_[0]),
          reinterpret_cast<void **>(&frameBuffers_[1]));
    }
    if (result != ESP_OK) {
      Serial.printf("RGB panel initialization failed: %s\n",
                    esp_err_to_name(result));
      return false;
    }

    memset(frameBuffers_[0], 0, FRAME_BUFFER_BYTES);
    memset(frameBuffers_[1], 0, FRAME_BUFFER_BYTES);
    Serial.printf("RGB framebuffers: %p, %p (%u bytes each)\n",
                  frameBuffers_[0], frameBuffers_[1], FRAME_BUFFER_BYTES);
    return true;
  }

  uint8_t *frameBuffer(uint8_t index) const {
    return index < 2 ? frameBuffers_[index] : nullptr;
  }

  bool present(uint8_t *buffer, uint32_t timeoutMs = 50) {
    if (buffer != frameBuffers_[0] && buffer != frameBuffers_[1]) {
      return false;
    }

    const uint32_t startVSync = vsyncCount_;
    const esp_err_t result = esp_lcd_panel_draw_bitmap(
        panelHandle_, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, buffer);
    if (result != ESP_OK) {
      return false;
    }

    const int64_t timeoutAt =
        esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
    while (vsyncCount_ == startVSync) {
      if (esp_timer_get_time() >= timeoutAt) {
        return false;
      }
      taskYIELD();
    }
    return true;
  }

private:
  static bool IRAM_ATTR onVSync(esp_lcd_panel_handle_t,
                                const esp_lcd_rgb_panel_event_data_t *,
                                void *userContext) {
    auto *self = static_cast<CrowPanelRgbDisplay *>(userContext);
    self->vsyncCount_ = self->vsyncCount_ + 1;
    return false;
  }

  esp_lcd_panel_handle_t panelHandle_ = nullptr;
  uint8_t *frameBuffers_[2] = {nullptr, nullptr};
  volatile uint32_t vsyncCount_ = 0;
};

CrowPanelRgbDisplay display;
uint8_t touchAddress = 0;
bool touchPressed = false;
uint16_t touchX = 0;
uint16_t touchY = 0;
bool uiReady = false;
lv_obj_t *button = nullptr;
lv_obj_t *buttonLabel = nullptr;
lv_obj_t *counterLabel = nullptr;
uint32_t pressCount = 0;

bool i2cWrite8(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool gt911Read(uint16_t reg, uint8_t *data, size_t length) {
  if (touchAddress == 0 || data == nullptr || length == 0) {
    return false;
  }

  Wire.beginTransmission(touchAddress);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire.requestFrom(touchAddress, length, true);
  if (received != length) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool gt911Write8(uint16_t reg, uint8_t value) {
  if (touchAddress == 0) {
    return false;
  }

  Wire.beginTransmission(touchAddress);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initializeRevision3Signals() {
  // Rev 3 routes GT911 reset and interrupt selection through the PCA9557.
  if (!i2cWrite8(PCA9557_ADDRESS, PCA9557_CONFIG_REGISTER, 0x00)) {
    return false;
  }
  if (!i2cWrite8(PCA9557_ADDRESS, PCA9557_OUTPUT_REGISTER, 0x00)) {
    return false;
  }

  delay(20);

  if (!i2cWrite8(PCA9557_ADDRESS, PCA9557_OUTPUT_REGISTER, 0x01)) {
    return false;
  }

  delay(100);

  // IO0 remains an output driven high; IO1 becomes the GT911 interrupt input.
  return i2cWrite8(PCA9557_ADDRESS, PCA9557_CONFIG_REGISTER, 0x02);
}

bool initializeTouch() {
  if (i2cDevicePresent(GT911_PRIMARY_ADDRESS)) {
    touchAddress = GT911_PRIMARY_ADDRESS;
  } else if (i2cDevicePresent(GT911_SECONDARY_ADDRESS)) {
    touchAddress = GT911_SECONDARY_ADDRESS;
  } else {
    return false;
  }

  Serial.printf("GT911 detected at 0x%02X\n", touchAddress);
  return true;
}

void pollTouchController() {
  const bool wasTouchPressed = touchPressed;
  uint8_t status = 0;
  if (!gt911Read(GT911_STATUS_REGISTER, &status, 1)) {
    touchPressed = false;
    return;
  }

  if ((status & 0x80U) == 0) {
    return;
  }

  const uint8_t touchCount = status & 0x0FU;
  if (touchCount > 0 && touchCount <= 5) {
    uint8_t point[8] = {};
    if (gt911Read(GT911_FIRST_POINT_REGISTER, point, sizeof(point))) {
      const uint16_t rawX = static_cast<uint16_t>(point[1]) |
                            (static_cast<uint16_t>(point[2]) << 8);
      const uint16_t rawY = static_cast<uint16_t>(point[3]) |
                            (static_cast<uint16_t>(point[4]) << 8);

      // The official Rev 3 example's two inversions cancel each other, so the
      // final LVGL coordinates are the GT911's native X/Y coordinates.
      touchX = constrain(rawX, 0U, SCREEN_WIDTH - 1U);
      touchY = constrain(rawY, 0U, SCREEN_HEIGHT - 1U);
      touchPressed = true;
      if (!wasTouchPressed) {
        Serial.printf("Touch down at %u,%u\n", touchX, touchY);
      }
    } else {
      touchPressed = false;
    }
  } else {
    touchPressed = false;
  }

  gt911Write8(GT911_STATUS_REGISTER, 0);
}

void displayFlush(lv_display_t *lvDisplay, const lv_area_t *area,
                  uint8_t *pixelMap) {
  (void)area;
  if (!display.present(pixelMap)) {
    Serial.println("RGB framebuffer VSYNC switch timed out");
  }
  lv_display_flush_ready(lvDisplay);
}

void touchRead(lv_indev_t *, lv_indev_data_t *data) {
  pollTouchController();
  data->state = touchPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (touchPressed) {
    data->point.x = touchX;
    data->point.y = touchY;
  }
}

void buttonClicked(lv_event_t *) {
  ++pressCount;

  char text[32];
  snprintf(text, sizeof(text), "Touches: %lu",
           static_cast<unsigned long>(pressCount));
  lv_label_set_text(counterLabel, text);
  lv_label_set_text(buttonLabel, "TOUCH OK");

  const uint32_t color = (pressCount % 2U == 0U) ? 0x2563EB : 0x16A34A;
  lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);

  Serial.printf("Button pressed: %lu at %u,%u\n",
                static_cast<unsigned long>(pressCount), touchX, touchY);
}

void createTouchTestScreen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0F172A), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "SMART MIXER");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 55);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "CrowPanel Rev 3 touch test");
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 105);

  button = lv_button_create(screen);
  lv_obj_set_size(button, 300, 120);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, 15);
  lv_obj_set_style_radius(button, 20, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x2563EB), LV_PART_MAIN);
  lv_obj_add_event_cb(button, buttonClicked, LV_EVENT_CLICKED, nullptr);

  buttonLabel = lv_label_create(button);
  lv_label_set_text(buttonLabel, "PRESS ME");
  lv_obj_set_style_text_font(buttonLabel, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_center(buttonLabel);

  counterLabel = lv_label_create(screen);
  lv_label_set_text(counterLabel, "Touches: 0");
  lv_obj_set_style_text_color(counterLabel, lv_color_hex(0xE2E8F0),
                              LV_PART_MAIN);
  lv_obj_align(counterLabel, LV_ALIGN_BOTTOM_MID, 0, -65);
}

bool initializeLvgl() {
  lv_init();
  lv_tick_set_cb(millis);

  auto *frameBuffer0 = display.frameBuffer(0);
  auto *frameBuffer1 = display.frameBuffer(1);
  if (frameBuffer0 == nullptr || frameBuffer1 == nullptr) {
    Serial.println("RGB framebuffers are unavailable");
    return false;
  }

  lv_display_t *lvDisplay = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisplay, displayFlush);
  lv_display_set_buffers(lvDisplay, frameBuffer0, frameBuffer1,
                         FRAME_BUFFER_BYTES, LV_DISPLAY_RENDER_MODE_FULL);

  lv_indev_t *input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, touchRead);

  createTouchTestScreen();
  return true;
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nSmart Mixer CrowPanel Rev 3 starting");
  Serial.printf("PSRAM: %s, size: %u bytes\n", psramFound() ? "OK" : "missing",
                ESP.getPsramSize());

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, LOW);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQUENCY);
  if (!initializeRevision3Signals()) {
    Serial.println("PCA9557 initialization failed");
    return;
  }

  if (!display.begin()) {
    Serial.println("RGB display initialization failed");
    return;
  }

  if (!initializeTouch()) {
    Serial.println("GT911 was not found");
    return;
  }

  if (!initializeLvgl()) {
    Serial.println("LVGL initialization failed");
    return;
  }

  digitalWrite(BACKLIGHT_PIN, HIGH);
  lv_timer_handler();
  uiReady = true;
  Serial.println("Touch test ready");
}

void loop() {
  if (uiReady) {
    lv_timer_handler();
  }
  delay(5);
}
