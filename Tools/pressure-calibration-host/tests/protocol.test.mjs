import test from "node:test";
import assert from "node:assert/strict";
import {
  PressureHostFrameParser,
  buildCalibrationSetTableCommand,
  buildCalibrationSetThresholdCommand,
  buildReportFrame,
  crc16Modbus,
  describeCalibrationAck,
  parseReportFrame,
  readU16LE,
  readU32LE
} from "../src/protocol.js";

test("builds and parses one CS1237 report frame", () => {
  const frame = buildReportFrame({
    seq: 7,
    raw: -5446,
    weightX10: 218,
    thresholdG: 400,
    deviceCode: 0x0f
  });
  const parsed = parseReportFrame(frame, 123);
  assert.equal(parsed.timestamp, 123);
  assert.equal(parsed.seq, 7);
  assert.equal(parsed.raw, -5446);
  assert.equal(parsed.weightX10, 218);
  assert.equal(parsed.weightG, 21.8);
  assert.equal(parsed.thresholdG, 400);
  assert.equal(parsed.deviceBits, "1111");
  assert.equal(parsed.knownDevice, true);
});

test("stream parser resynchronizes after leading bytes", () => {
  const parser = new PressureHostFrameParser();
  const frame = buildReportFrame({ seq: 2, raw: 13082, weightX10: 268 });
  const chunk = Uint8Array.from([0x00, 0x13, ...frame.slice(0, 5)]);
  const first = parser.push(chunk);
  assert.equal(first.some((event) => event.type === "frame"), false);
  const second = parser.push(frame.slice(5));
  assert.equal(second.at(-1).type, "frame");
  assert.equal(second.at(-1).frame.raw, 13082);
});

test("crc16 detects changed payload bytes", () => {
  const frame = buildReportFrame({ raw: 817285, weightX10: 10218 });
  const originalCrc = crc16Modbus(frame, 2, 15);
  frame[6] ^= 0x01;
  assert.notEqual(crc16Modbus(frame, 2, 15), originalCrc);
  assert.throws(() => parseReportFrame(frame), /CRC/);
});

test("builds calibration set-table command payload", () => {
  const frame = buildCalibrationSetTableCommand([
    { raw: -100, targetX10: 218 },
    { raw: 500, targetX10: 268 }
  ], 9);
  assert.equal(frame[0], 0xa5);
  assert.equal(frame[3], 0x10);
  assert.equal(frame[4], 17);
  assert.equal(frame[5], 9);
  assert.equal(frame[6], 2);
  assert.equal(readU32LE(frame, 7) | 0, -100);
  assert.equal(readU32LE(frame, 11), 218);
  assert.equal(frame.at(-2), 0x5a);
  assert.equal(frame.at(-1), 0xa5);
});

test("builds pressure threshold command payload", () => {
  const frame = buildCalibrationSetThresholdCommand(650, 7);
  assert.equal(frame[0], 0xa5);
  assert.equal(frame[3], 0x12);
  assert.equal(frame[4], 2);
  assert.equal(frame[5], 7);
  assert.equal(readU16LE(frame, 6), 650);
  assert.equal(frame.at(-2), 0x5a);
  assert.equal(frame.at(-1), 0xa5);
  assert.throws(() => buildCalibrationSetThresholdCommand(0), /Pressure threshold/);
  assert.throws(() => buildCalibrationSetThresholdCommand(50001), /Pressure threshold/);
});

test("describes set-table ack success and failure for operator popup", () => {
  assert.deepEqual(
    describeCalibrationAck({ cmd: 0x10, status: 0, runtimeEnabled: 1, pointCount: 8 }),
    {
      kind: "success",
      title: "写入 MCU 标定表成功",
      message: "MCU 已启用 8 点标定表。"
    }
  );

  assert.deepEqual(
    describeCalibrationAck({ cmd: 0x10, status: 4, runtimeEnabled: 0, pointCount: 0 }),
    {
      kind: "error",
      title: "写入 MCU 标定表失败",
      message: "Flash 写入失败。请检查供电和连接后重试。"
    }
  );
});

test("describes pressure threshold ack success and failure for operator popup", () => {
  assert.deepEqual(
    describeCalibrationAck({ cmd: 0x12, status: 0, runtimeEnabled: 1, pointCount: 8 }),
    {
      kind: "success",
      title: "写入压力阈值成功",
      message: "MCU 已保存压力阈值。"
    }
  );

  assert.deepEqual(
    describeCalibrationAck({ cmd: 0x12, status: 6, runtimeEnabled: 1, pointCount: 8 }),
    {
      kind: "error",
      title: "写入压力阈值失败",
      message: "参数值非法。请检查阈值后重试。"
    }
  );
});
