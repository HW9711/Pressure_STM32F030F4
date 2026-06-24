import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const repoRoot = resolve(import.meta.dirname, "..", "..", "..");

function readRepoFile(...parts) {
  return readFileSync(resolve(repoRoot, ...parts), "utf8");
}

test("firmware keeps the 21-byte AA55 upload protocol unchanged", () => {
  const protocolHeader = readRepoFile("Core", "Inc", "protocol.h");

  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_HEADER0\s+0xAAU/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_HEADER1\s+0x55U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_TAIL0\s+0x55U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_TAIL1\s+0xAAU/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_VERSION\s+0x02U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_MSG_TYPE_REPORT\s+0x01U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_PAYLOAD_LEN\s+0x0CU/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_CRC_DATA_LEN\s+0x0FU/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_FRAME_SIZE\s+21U/);

  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_OFFSET_RAW_CS1237\s+6U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_OFFSET_WEIGHT_X10\s+10U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_OFFSET_THRESHOLD_G\s+14U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_OFFSET_DEVICE_CODE\s+16U/);
  assert.match(protocolHeader, /CS1237_UART_PROTOCOL_OFFSET_CRC16\s+17U/);
});

test("firmware filters CS1237 raw before placing it into the unchanged upload frame", () => {
  const mainSource = readRepoFile("Core", "Src", "main.c");

  assert.match(
    mainSource,
    /raw_value\s*=\s*CS1237_ReadMedian\s*\(\s*0U\s*\)\s*;/,
    "main loop should put median-filtered raw into the existing RawCs1237 field"
  );
  assert.doesNotMatch(
    mainSource,
    /raw_value\s*=\s*CS1237_ReadRawSigned\s*\(\s*\)\s*;/,
    "single-sample raw lets isolated CS1237 spikes block 0-point calibration"
  );
});

test("firmware supports host-written pressure threshold without changing upload frame", () => {
  const calibrationHeader = readRepoFile("Core", "Inc", "calibration_protocol.h");
  const calibrationSource = readRepoFile("Core", "Src", "calibration_protocol.c");
  const thresholdHeader = readRepoFile("Core", "Inc", "pressure_threshold_store.h");
  const mainSource = readRepoFile("Core", "Src", "main.c");

  assert.match(calibrationHeader, /CAL_PROTOCOL_CMD_SET_THRESHOLD\s+0x12U/);
  assert.match(calibrationHeader, /CAL_PROTOCOL_STATUS_BAD_VALUE\s+0x06U/);
  assert.match(calibrationSource, /case\s+CAL_PROTOCOL_CMD_SET_THRESHOLD\s*:/);
  assert.match(calibrationSource, /PressureThreshold_SetRuntimeAndSave\s*\(/);
  assert.match(thresholdHeader, /PRESSURE_THRESHOLD_FLASH_STORE_ENABLE\s+1U/);
  assert.match(mainSource, /PressureThreshold_GetRuntime\s*\(\s*\)/);
});
