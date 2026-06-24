export const FRAME_SIZE = 21;
export const HEADER0 = 0xaa;
export const HEADER1 = 0x55;
export const TAIL0 = 0x55;
export const TAIL1 = 0xaa;
export const PROTOCOL_VERSION = 0x02;
export const MSG_TYPE_REPORT = 0x01;
export const PAYLOAD_LEN = 0x0c;
export const CRC_DATA_LEN = 0x0f;
export const CAL_HEADER0 = 0xa5;
export const CAL_HEADER1 = 0x5a;
export const CAL_TAIL0 = 0x5a;
export const CAL_TAIL1 = 0xa5;
export const CAL_VERSION = 0x01;
export const CAL_CMD_PING = 0x01;
export const CAL_CMD_SET_TABLE = 0x10;
export const CAL_CMD_CLEAR_TABLE = 0x11;
export const CAL_CMD_SET_THRESHOLD = 0x12;
export const CAL_RSP_BASE = 0x80;
export const CAL_ACK_PAYLOAD_SIZE = 4;
export const PRESSURE_THRESHOLD_MIN_G = 1;
export const PRESSURE_THRESHOLD_MAX_G = 50000;

const CAL_STATUS_TEXT = new Map([
  [0x00, "OK"],
  [0x01, "帧格式错误"],
  [0x02, "长度错误"],
  [0x03, "标定表非法"],
  [0x04, "Flash 写入失败"],
  [0x05, "未知命令"],
  [0x06, "参数值非法"]
]);

export function calibrationStatusText(status) {
  return CAL_STATUS_TEXT.get(status) || `状态 ${status}`;
}

export function crc16Modbus(bytes, offset = 0, length = bytes.length - offset) {
  let crc = 0xffff;
  for (let index = 0; index < length; index += 1) {
    crc ^= bytes[offset + index];
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 1) !== 0 ? ((crc >>> 1) ^ 0xa001) : (crc >>> 1);
    }
  }
  return crc & 0xffff;
}

export function readU16LE(bytes, offset) {
  return (bytes[offset] | (bytes[offset + 1] << 8)) >>> 0;
}

export function readU32LE(bytes, offset) {
  return (
    (bytes[offset]) |
    (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) |
    (bytes[offset + 3] << 24)
  ) >>> 0;
}

export function readI32LE(bytes, offset) {
  return readU32LE(bytes, offset) | 0;
}

export function writeU16LE(bytes, offset, value) {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = (value >>> 8) & 0xff;
}

export function writeU32LE(bytes, offset, value) {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = (value >>> 8) & 0xff;
  bytes[offset + 2] = (value >>> 16) & 0xff;
  bytes[offset + 3] = (value >>> 24) & 0xff;
}

export function formatHex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}

export function deviceCodeToBits(deviceCode) {
  return (deviceCode & 0x0f).toString(2).padStart(4, "0");
}

export function isKnownDeviceCode(deviceCode) {
  return [0x0f, 0x0e, 0x0c, 0x08, 0x09].includes(deviceCode & 0x0f);
}

export function parseReportFrame(frameBytes, timestamp = Date.now()) {
  if (!(frameBytes instanceof Uint8Array) || frameBytes.length !== FRAME_SIZE) {
    throw new Error(`帧长度错误：需要 ${FRAME_SIZE} 字节`);
  }
  if (frameBytes[0] !== HEADER0 || frameBytes[1] !== HEADER1) {
    throw new Error("帧头错误");
  }
  if (frameBytes[19] !== TAIL0 || frameBytes[20] !== TAIL1) {
    throw new Error("帧尾错误");
  }
  if (frameBytes[2] !== PROTOCOL_VERSION || frameBytes[3] !== MSG_TYPE_REPORT || frameBytes[4] !== PAYLOAD_LEN) {
    throw new Error("协议版本、消息类型或长度错误");
  }

  const expectedCrc = readU16LE(frameBytes, 17);
  const actualCrc = crc16Modbus(frameBytes, 2, CRC_DATA_LEN);
  if (actualCrc !== expectedCrc) {
    throw new Error(`CRC 错误：收到 0x${expectedCrc.toString(16)}, 计算 0x${actualCrc.toString(16)}`);
  }

  const raw = readI32LE(frameBytes, 6);
  const weightX10 = readU32LE(frameBytes, 10);
  const thresholdG = readU16LE(frameBytes, 14);
  const deviceCode = frameBytes[16] & 0x0f;

  return {
    timestamp,
    seq: frameBytes[5],
    raw,
    weightX10,
    weightG: weightX10 / 10,
    thresholdG,
    deviceCode,
    deviceBits: deviceCodeToBits(deviceCode),
    knownDevice: isKnownDeviceCode(deviceCode),
    crc16: expectedCrc,
    hex: formatHex(frameBytes)
  };
}

export function buildReportFrame({ seq = 0, raw = 0, weightX10 = 0, thresholdG = 400, deviceCode = 0x0f } = {}) {
  const bytes = new Uint8Array(FRAME_SIZE);
  bytes[0] = HEADER0;
  bytes[1] = HEADER1;
  bytes[2] = PROTOCOL_VERSION;
  bytes[3] = MSG_TYPE_REPORT;
  bytes[4] = PAYLOAD_LEN;
  bytes[5] = seq & 0xff;
  writeU32LE(bytes, 6, raw >>> 0);
  writeU32LE(bytes, 10, weightX10 >>> 0);
  writeU16LE(bytes, 14, thresholdG);
  bytes[16] = deviceCode & 0x0f;
  writeU16LE(bytes, 17, crc16Modbus(bytes, 2, CRC_DATA_LEN));
  bytes[19] = TAIL0;
  bytes[20] = TAIL1;
  return bytes;
}

export function buildCalibrationCommand(cmd, payload = new Uint8Array(), seq = 0) {
  const payloadBytes = payload instanceof Uint8Array ? payload : Uint8Array.from(payload);
  const bytes = new Uint8Array(10 + payloadBytes.length);
  bytes[0] = CAL_HEADER0;
  bytes[1] = CAL_HEADER1;
  bytes[2] = CAL_VERSION;
  bytes[3] = cmd & 0xff;
  bytes[4] = payloadBytes.length & 0xff;
  bytes[5] = seq & 0xff;
  bytes.set(payloadBytes, 6);
  writeU16LE(bytes, 6 + payloadBytes.length, crc16Modbus(bytes, 2, 4 + payloadBytes.length));
  bytes[8 + payloadBytes.length] = CAL_TAIL0;
  bytes[9 + payloadBytes.length] = CAL_TAIL1;
  return bytes;
}

export function buildCalibrationPingCommand(seq = 0) {
  return buildCalibrationCommand(CAL_CMD_PING, new Uint8Array(), seq);
}

export function buildCalibrationClearCommand(seq = 0) {
  return buildCalibrationCommand(CAL_CMD_CLEAR_TABLE, new Uint8Array(), seq);
}

export function buildCalibrationSetTableCommand(points, seq = 0) {
  if (!Array.isArray(points) || points.length < 2 || points.length > 8) {
    throw new Error("Calibration table requires 2..8 points.");
  }
  const payload = new Uint8Array(1 + (points.length * 8));
  payload[0] = points.length;
  points.forEach((point, index) => {
    const offset = 1 + (index * 8);
    writeU32LE(payload, offset, point.raw >>> 0);
    writeU32LE(payload, offset + 4, point.targetX10 >>> 0);
  });
  return buildCalibrationCommand(CAL_CMD_SET_TABLE, payload, seq);
}

export function buildCalibrationSetThresholdCommand(thresholdG, seq = 0) {
  const value = Math.round(Number(thresholdG));
  if (!Number.isFinite(value) || value < PRESSURE_THRESHOLD_MIN_G || value > PRESSURE_THRESHOLD_MAX_G) {
    throw new Error(`Pressure threshold must be ${PRESSURE_THRESHOLD_MIN_G}..${PRESSURE_THRESHOLD_MAX_G} g.`);
  }
  const payload = new Uint8Array(2);
  writeU16LE(payload, 0, value);
  return buildCalibrationCommand(CAL_CMD_SET_THRESHOLD, payload, seq);
}

export function parseCalibrationAckFrame(frameBytes, timestamp = Date.now()) {
  if (!(frameBytes instanceof Uint8Array) || frameBytes.length < 10) {
    throw new Error("ACK frame length error.");
  }
  if (frameBytes[0] !== CAL_HEADER0 || frameBytes[1] !== CAL_HEADER1) {
    throw new Error("ACK header error.");
  }
  const payloadLen = frameBytes[4];
  const frameLen = 10 + payloadLen;
  if (frameBytes.length !== frameLen) {
    throw new Error("ACK payload length error.");
  }
  if (frameBytes[2] !== CAL_VERSION || (frameBytes[3] & CAL_RSP_BASE) === 0) {
    throw new Error("ACK version or command error.");
  }
  if (frameBytes[frameLen - 2] !== CAL_TAIL0 || frameBytes[frameLen - 1] !== CAL_TAIL1) {
    throw new Error("ACK tail error.");
  }
  const expectedCrc = readU16LE(frameBytes, 6 + payloadLen);
  const actualCrc = crc16Modbus(frameBytes, 2, 4 + payloadLen);
  if (actualCrc !== expectedCrc) {
    throw new Error("ACK CRC error.");
  }
  return {
    timestamp,
    cmd: frameBytes[3] & 0x7f,
    seq: frameBytes[5],
    status: payloadLen > 0 ? frameBytes[6] : 0xff,
    runtimeEnabled: payloadLen > 1 ? frameBytes[7] : 0,
    pointCount: payloadLen > 2 ? frameBytes[8] : 0,
    rawReserved: payloadLen > 3 ? frameBytes[9] : 0,
    hex: formatHex(frameBytes)
  };
}

export function describeCalibrationAck(ack) {
  if (!ack || (ack.cmd !== CAL_CMD_SET_TABLE && ack.cmd !== CAL_CMD_SET_THRESHOLD)) {
    return null;
  }

  if (ack.cmd === CAL_CMD_SET_THRESHOLD) {
    if (ack.status === 0) {
      return {
        kind: "success",
        title: "写入压力阈值成功",
        message: "MCU 已保存压力阈值。"
      };
    }
    return {
      kind: "error",
      title: "写入压力阈值失败",
      message: ack.status === 6
        ? "参数值非法。请检查阈值后重试。"
        : `${calibrationStatusText(ack.status)}。请检查供电和连接后重试。`
    };
  }

  if (ack.status === 0) {
    if (ack.runtimeEnabled && ack.pointCount > 0) {
      return {
        kind: "success",
        title: "写入 MCU 标定表成功",
        message: `MCU 已启用 ${ack.pointCount} 点标定表。`
      };
    }
    return {
      kind: "warning",
      title: "MCU 返回 OK，但标定表未启用",
      message: `当前运行时表状态 ${ack.runtimeEnabled ? "已启用" : "未启用"}，点数 ${ack.pointCount}。`
    };
  }

  return {
    kind: "error",
    title: "写入 MCU 标定表失败",
    message: `${calibrationStatusText(ack.status)}。请检查供电和连接后重试。`
  };
}

export class Cs1237FrameParser {
  constructor() {
    this.buffer = [];
    this.dropCount = 0;
    this.crcErrorCount = 0;
    this.formatErrorCount = 0;
  }

  push(bytes, timestamp = Date.now()) {
    const events = [];
    for (const byte of bytes) {
      this.buffer.push(byte);
    }

    while (this.buffer.length >= FRAME_SIZE) {
      const headerIndex = this.buffer.findIndex((byte, index, buffer) => (
        byte === HEADER0 && buffer[index + 1] === HEADER1
      ));

      if (headerIndex < 0) {
        this.dropCount += this.buffer.length - 1;
        this.buffer = this.buffer.slice(-1);
        events.push({ type: "drop", count: this.dropCount });
        break;
      }

      if (headerIndex > 0) {
        this.dropCount += headerIndex;
        this.buffer.splice(0, headerIndex);
        events.push({ type: "drop", count: this.dropCount });
      }

      if (this.buffer.length < FRAME_SIZE) {
        break;
      }

      const candidate = Uint8Array.from(this.buffer.slice(0, FRAME_SIZE));
      try {
        const frame = parseReportFrame(candidate, timestamp);
        events.push({ type: "frame", frame });
        this.buffer.splice(0, FRAME_SIZE);
      } catch (error) {
        if (String(error.message).includes("CRC")) {
          this.crcErrorCount += 1;
          events.push({ type: "crc-error", error, count: this.crcErrorCount, hex: formatHex(candidate) });
        } else {
          this.formatErrorCount += 1;
          events.push({ type: "format-error", error, count: this.formatErrorCount, hex: formatHex(candidate) });
        }
        this.buffer.shift();
      }
    }

    return events;
  }
}

export class PressureHostFrameParser {
  constructor() {
    this.buffer = [];
    this.dropCount = 0;
  }

  push(bytes, timestamp = Date.now()) {
    const events = [];
    for (const byte of bytes) {
      this.buffer.push(byte);
    }

    while (this.buffer.length >= 2) {
      const reportIndex = this.buffer.findIndex((byte, index, buffer) => byte === HEADER0 && buffer[index + 1] === HEADER1);
      const ackIndex = this.buffer.findIndex((byte, index, buffer) => byte === CAL_HEADER0 && buffer[index + 1] === CAL_HEADER1);
      const headerIndex = [reportIndex, ackIndex].filter((index) => index >= 0).sort((a, b) => a - b)[0] ?? -1;

      if (headerIndex < 0) {
        this.dropCount += Math.max(0, this.buffer.length - 1);
        this.buffer = this.buffer.slice(-1);
        break;
      }
      if (headerIndex > 0) {
        this.dropCount += headerIndex;
        this.buffer.splice(0, headerIndex);
        events.push({ type: "drop", count: this.dropCount });
      }

      if (this.buffer[0] === HEADER0 && this.buffer[1] === HEADER1) {
        if (this.buffer.length < FRAME_SIZE) {
          break;
        }
        const candidate = Uint8Array.from(this.buffer.slice(0, FRAME_SIZE));
        try {
          events.push({ type: "frame", frame: parseReportFrame(candidate, timestamp) });
          this.buffer.splice(0, FRAME_SIZE);
        } catch (error) {
          events.push({ type: "report-error", error, hex: formatHex(candidate) });
          this.buffer.shift();
        }
        continue;
      }

      if (this.buffer[0] === CAL_HEADER0 && this.buffer[1] === CAL_HEADER1) {
        if (this.buffer.length < 6) {
          break;
        }
        const payloadLen = this.buffer[4];
        const ackLen = 10 + payloadLen;
        if (ackLen > 32) {
          events.push({ type: "ack-error", error: new Error("ACK length too large.") });
          this.buffer.shift();
          continue;
        }
        if (this.buffer.length < ackLen) {
          break;
        }
        const candidate = Uint8Array.from(this.buffer.slice(0, ackLen));
        try {
          events.push({ type: "ack", ack: parseCalibrationAckFrame(candidate, timestamp) });
          this.buffer.splice(0, ackLen);
        } catch (error) {
          events.push({ type: "ack-error", error, hex: formatHex(candidate) });
          this.buffer.shift();
        }
        continue;
      }
    }

    return events;
  }
}
