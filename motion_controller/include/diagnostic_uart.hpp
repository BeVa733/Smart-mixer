#pragma once

#include <Arduino.h>
#include <IRQManager.h>
#include <r_sci_uart.h>
#include <string.h>

#if !defined(ARDUINO_UNOR4_MINIMA)
#error "DiagnosticUart requires UNO R4 Minima D0/D1 (SCI2)"
#endif

// Owns the same physical SCI2/D0/D1 peripheral normally used by Serial1.
// Do not call Serial1.begin/write concurrently. Core 1.6.0 UART::write waits
// for an interrupt; use its underlying FSP API to avoid that busy wait.
class DiagnosticUart {
public:
  bool begin(uint32_t baud) {
    config_.channel = 2;
    config_.data_bits = UART_DATA_BITS_8;
    config_.parity = UART_PARITY_OFF;
    config_.stop_bits = UART_STOP_BITS_1;
    config_.txi_irq = config_.tei_irq = config_.rxi_irq = config_.eri_irq =
        FSP_INVALID_VECTOR;
    config_.p_callback = callback;
    config_.p_context = this;
    config_.p_extend = &extended_;
    extended_.clock = SCI_UART_CLOCK_INT;
    extended_.rx_edge_start = SCI_UART_START_BIT_FALLING_EDGE;
    extended_.noise_cancel = SCI_UART_NOISE_CANCELLATION_DISABLE;
    extended_.rx_fifo_trigger = SCI_UART_RX_FIFO_TRIGGER_MAX;
    extended_.p_baud_setting = &baud_;
    extended_.flow_control = SCI_UART_FLOW_CONTROL_RTS;
    extended_.flow_control_pin = static_cast<bsp_io_port_pin_t>(UINT16_MAX);
    extended_.rs485_setting.enable = SCI_UART_RS485_DISABLE;
    extended_.rs485_setting.de_control_pin =
        static_cast<bsp_io_port_pin_t>(UINT16_MAX);

    const uint32_t function =
        IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_SCI0_2_4_6_8;
    if (R_IOPORT_PinCfg(&g_ioport_ctrl, BSP_IO_PORT_03_PIN_01, function) !=
            FSP_SUCCESS ||
        R_IOPORT_PinCfg(&g_ioport_ctrl, BSP_IO_PORT_03_PIN_02, function) !=
            FSP_SUCCESS ||
        R_SCI_UART_BaudCalculate(baud, false, 3000, &baud_) != FSP_SUCCESS ||
        !IRQManager::getInstance().addPeripheral(IRQ_SCI_UART, &config_)) {
      ++errors_;
      return false;
    }
    ready_ = R_SCI_UART_Open(&controller_, &config_) == FSP_SUCCESS;
    if (!ready_)
      ++errors_;
    return ready_;
  }

  int available() { return rx_.available(); }
  int read() { return rx_.read_char(); }

  size_t tryWrite(const char *bytes, size_t length) {
    if (!ready_ || busy_ || length == 0)
      return 0;
    const size_t count = length < sizeof(tx_) ? length : sizeof(tx_);
    memcpy(tx_, bytes, count);
    busy_ = true;
    const fsp_err_t result =
        R_SCI_UART_Write(&controller_, tx_, static_cast<uint32_t>(count));
    if (result != FSP_SUCCESS) {
      busy_ = false;
      ++errors_;
      return 0;
    }
    // FSP retains tx_ until TX_COMPLETE; it is not reused while busy_.
    return count;
  }

  uint32_t errors() const { return errors_; }

private:
  static void callback(uart_callback_args_t *args) {
    auto *self =
        static_cast<DiagnosticUart *>(const_cast<void *>(args->p_context));
    if (args->event == UART_EVENT_RX_CHAR) {
      if (self->rx_.availableForStore())
        self->rx_.store_char(static_cast<uint8_t>(args->data));
      else
        self->errors_ = self->errors_ + 1;
    } else if (args->event == UART_EVENT_TX_COMPLETE) {
      self->busy_ = false;
    } else if (args->event == UART_EVENT_ERR_PARITY ||
               args->event == UART_EVENT_ERR_FRAMING ||
               args->event == UART_EVENT_ERR_OVERFLOW) {
      self->errors_ = self->errors_ + 1;
    }
  }

  sci_uart_instance_ctrl_t controller_ = {};
  uart_cfg_t config_ = {};
  sci_uart_extended_cfg_t extended_ = {};
  baud_setting_t baud_ = {};
  arduino::SafeRingBufferN<512> rx_;
  uint8_t tx_[32] = {};
  volatile bool busy_ = false;
  bool ready_ = false;
  volatile uint32_t errors_ = 0;
};
