#include "calibration_protocol.h"
#include "pressure_threshold_store.h"
#include "protocol.h"

#define CAL_PROTOCOL_RX_BUF_SIZE          96U
#define CAL_PROTOCOL_RING_BUF_SIZE        128U
#define CAL_PROTOCOL_TX_BUF_SIZE          24U
#define CAL_PROTOCOL_MAX_PAYLOAD_SIZE     65U
#define CAL_PROTOCOL_FIXED_OVERHEAD       10U
#define CAL_PROTOCOL_ACK_PAYLOAD_SIZE     4U

static UART_HandleTypeDef *g_cal_uart = NULL;
static uint8_t g_cal_rx_byte = 0U;
static volatile uint8_t g_cal_ring_buf[CAL_PROTOCOL_RING_BUF_SIZE];
static volatile uint8_t g_cal_ring_head = 0U;
static volatile uint8_t g_cal_ring_tail = 0U;
static volatile uint8_t g_cal_ring_overflow = 0U;
static uint8_t g_cal_rx_buf[CAL_PROTOCOL_RX_BUF_SIZE];
static uint8_t g_cal_rx_len = 0U;
static uint8_t g_cal_tx_buf[CAL_PROTOCOL_TX_BUF_SIZE];
static uint8_t g_cal_tx_len = 0U;
static volatile uint8_t g_cal_tx_pending = 0U;

static uint16_t CalibrationProtocol_ReadU16Le(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void CalibrationProtocol_WriteU16Le(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value & 0xFFU);
  data[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void CalibrationProtocol_ResetRx(void)
{
  g_cal_rx_len = 0U;
}

static uint8_t CalibrationProtocol_RingPop(uint8_t *byte)
{
  if (g_cal_ring_tail == g_cal_ring_head) {
    return 0U;
  }

  *byte = g_cal_ring_buf[g_cal_ring_tail];
  g_cal_ring_tail = (uint8_t)((g_cal_ring_tail + 1U) % CAL_PROTOCOL_RING_BUF_SIZE);
  return 1U;
}

static void CalibrationProtocol_DropRxBytes(uint8_t count)
{
  uint8_t i;

  if (count >= g_cal_rx_len) {
    CalibrationProtocol_ResetRx();
    return;
  }

  for (i = 0U; i < (g_cal_rx_len - count); i++) {
    g_cal_rx_buf[i] = g_cal_rx_buf[i + count];
  }
  g_cal_rx_len = (uint8_t)(g_cal_rx_len - count);
}

static void CalibrationProtocol_BuildAck(uint8_t cmd, uint8_t seq, uint8_t status)
{
  uint16_t crc;

  g_cal_tx_buf[0] = CAL_PROTOCOL_HEADER0;
  g_cal_tx_buf[1] = CAL_PROTOCOL_HEADER1;
  g_cal_tx_buf[2] = CAL_PROTOCOL_VERSION;
  g_cal_tx_buf[3] = (uint8_t)(cmd | CAL_PROTOCOL_RSP_BASE);
  g_cal_tx_buf[4] = CAL_PROTOCOL_ACK_PAYLOAD_SIZE;
  g_cal_tx_buf[5] = seq;
  g_cal_tx_buf[6] = status;
  g_cal_tx_buf[7] = 0U;
  g_cal_tx_buf[8] = 0U;
  g_cal_tx_buf[9] = 0U;

  crc = CS1237UartProtocol_Crc16Modbus(&g_cal_tx_buf[2],
                                       (uint16_t)(4U + CAL_PROTOCOL_ACK_PAYLOAD_SIZE));
  CalibrationProtocol_WriteU16Le(&g_cal_tx_buf[10], crc);
  g_cal_tx_buf[12] = CAL_PROTOCOL_TAIL0;
  g_cal_tx_buf[13] = CAL_PROTOCOL_TAIL1;
  g_cal_tx_len = 14U;
  g_cal_tx_pending = 1U;
}

static uint8_t CalibrationProtocol_HandleSetThreshold(const uint8_t *payload, uint8_t payload_len)
{
  uint16_t threshold_g;

  if (payload_len != 2U) {
    return CAL_PROTOCOL_STATUS_BAD_LENGTH;
  }

  threshold_g = CalibrationProtocol_ReadU16Le(payload);
  if (PressureThreshold_IsValid(threshold_g) == 0U) {
    return CAL_PROTOCOL_STATUS_BAD_VALUE;
  }
  if (PressureThreshold_SetRuntimeAndSave(threshold_g) == 0U) {
    return CAL_PROTOCOL_STATUS_FLASH_FAIL;
  }

  return CAL_PROTOCOL_STATUS_OK;
}

static void CalibrationProtocol_HandleFrame(const uint8_t *frame, uint8_t frame_len)
{
  uint8_t cmd = frame[3];
  uint8_t payload_len = frame[4];
  uint8_t seq = frame[5];
  const uint8_t *payload = &frame[6];
  uint8_t status = CAL_PROTOCOL_STATUS_OK;

  (void)frame_len;

  switch (cmd) {
  case CAL_PROTOCOL_CMD_PING:
    if (payload_len != 0U) {
      status = CAL_PROTOCOL_STATUS_BAD_LENGTH;
    }
    break;
  case CAL_PROTOCOL_CMD_SET_TABLE:
  case CAL_PROTOCOL_CMD_CLEAR_TABLE:
    /* These legacy commands must never erase the factory-empty page. */
    status = CAL_PROTOCOL_STATUS_UNKNOWN_CMD;
    break;
  case CAL_PROTOCOL_CMD_SET_THRESHOLD:
    status = CalibrationProtocol_HandleSetThreshold(payload, payload_len);
    break;
  default:
    status = CAL_PROTOCOL_STATUS_UNKNOWN_CMD;
    break;
  }

  CalibrationProtocol_BuildAck(cmd, seq, status);
}

static void CalibrationProtocol_ParseRx(void)
{
  while (g_cal_rx_len >= 2U) {
    uint8_t payload_len;
    uint8_t frame_len;
    uint16_t expected_crc;
    uint16_t actual_crc;

    if (g_cal_rx_buf[0] != CAL_PROTOCOL_HEADER0) {
      CalibrationProtocol_DropRxBytes(1U);
      continue;
    }
    if (g_cal_rx_buf[1] != CAL_PROTOCOL_HEADER1) {
      CalibrationProtocol_DropRxBytes(1U);
      continue;
    }
    if (g_cal_rx_len < 6U) {
      return;
    }

    payload_len = g_cal_rx_buf[4];
    if (payload_len > CAL_PROTOCOL_MAX_PAYLOAD_SIZE) {
      CalibrationProtocol_DropRxBytes(1U);
      continue;
    }

    frame_len = (uint8_t)(payload_len + CAL_PROTOCOL_FIXED_OVERHEAD);
    if (g_cal_rx_len < frame_len) {
      return;
    }

    if ((g_cal_rx_buf[2] != CAL_PROTOCOL_VERSION) ||
        (g_cal_rx_buf[frame_len - 2U] != CAL_PROTOCOL_TAIL0) ||
        (g_cal_rx_buf[frame_len - 1U] != CAL_PROTOCOL_TAIL1)) {
      CalibrationProtocol_DropRxBytes(1U);
      continue;
    }

    expected_crc = CalibrationProtocol_ReadU16Le(&g_cal_rx_buf[6U + payload_len]);
    actual_crc = CS1237UartProtocol_Crc16Modbus(&g_cal_rx_buf[2], (uint16_t)(4U + payload_len));
    if (actual_crc != expected_crc) {
      CalibrationProtocol_DropRxBytes(1U);
      continue;
    }

    CalibrationProtocol_HandleFrame(g_cal_rx_buf, frame_len);
    CalibrationProtocol_DropRxBytes(frame_len);
  }
}

void CalibrationProtocol_Init(UART_HandleTypeDef *huart)
{
  g_cal_uart = huart;
  CalibrationProtocol_ResetRx();
  g_cal_ring_head = 0U;
  g_cal_ring_tail = 0U;
  g_cal_ring_overflow = 0U;
  g_cal_tx_pending = 0U;

  if (g_cal_uart != NULL) {
    (void)HAL_UART_Receive_IT(g_cal_uart, &g_cal_rx_byte, 1U);
  }
}

void CalibrationProtocol_Process(UART_HandleTypeDef *huart)
{
  uint8_t byte;

  if (g_cal_ring_overflow != 0U) {
    g_cal_ring_overflow = 0U;
    CalibrationProtocol_ResetRx();
  }

  while (CalibrationProtocol_RingPop(&byte) != 0U) {
    if (g_cal_rx_len >= CAL_PROTOCOL_RX_BUF_SIZE) {
      CalibrationProtocol_ResetRx();
    }
    g_cal_rx_buf[g_cal_rx_len] = byte;
    g_cal_rx_len++;
    CalibrationProtocol_ParseRx();
    if (g_cal_tx_pending != 0U) {
      break;
    }
  }

  if ((g_cal_tx_pending != 0U) && (huart != NULL) && (g_cal_tx_len > 0U)) {
    g_cal_tx_pending = 0U;
    (void)HAL_UART_Transmit(huart, g_cal_tx_buf, g_cal_tx_len, 100U);
  }
}

void CalibrationProtocol_OnRxByte(uint8_t byte)
{
  uint8_t next_head = (uint8_t)((g_cal_ring_head + 1U) % CAL_PROTOCOL_RING_BUF_SIZE);

  if (next_head == g_cal_ring_tail) {
    g_cal_ring_overflow = 1U;
    return;
  }

  g_cal_ring_buf[g_cal_ring_head] = byte;
  g_cal_ring_head = next_head;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((g_cal_uart != NULL) && (huart == g_cal_uart)) {
    CalibrationProtocol_OnRxByte(g_cal_rx_byte);
    (void)HAL_UART_Receive_IT(g_cal_uart, &g_cal_rx_byte, 1U);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((g_cal_uart != NULL) && (huart == g_cal_uart)) {
    (void)HAL_UART_Receive_IT(g_cal_uart, &g_cal_rx_byte, 1U);
  }
}
