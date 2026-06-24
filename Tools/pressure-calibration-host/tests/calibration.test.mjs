import test from "node:test";
import assert from "node:assert/strict";
import {
  buildCalibrationHeader,
  buildCalibrationSource,
  capturedPointsFromSteps,
  confirmNextCalibrationPoint,
  createCalibrationSteps,
  holdForManualConfirmation,
  isWindowStable,
  monotonicStatus,
  normalizeSamplingConfig,
  stabilityLimitsForTarget
} from "../src/calibration.js";

test("creates tray-plus-weight targets", () => {
  const steps = createCalibrationSteps({ trayWeightG: 21.8, weightsG: [0, 5, 20] });
  assert.deepEqual(steps.map((step) => step.targetLoadG), [21.8, 26.8, 41.8]);
});

test("normalizes sampling config before applying edited inputs", () => {
  assert.deepEqual(
    normalizeSamplingConfig({
      trayWeightG: "22.5",
      sampleCount: "31",
      settleMs: "1800",
      maxRawSpan: "1600",
      maxRawStddev: "600"
    }),
    {
      trayWeightG: 22.5,
      sampleCount: 31,
      settleMs: 1800,
      maxRawSpan: 1600,
      maxRawStddev: 600
    }
  );

  assert.deepEqual(normalizeSamplingConfig({}), {
    trayWeightG: 21.8,
    sampleCount: 25,
    settleMs: 1500,
    maxRawSpan: 1500,
    maxRawStddev: 550
  });
});

test("stable window checks sample count, span, and deviation", () => {
  const samples = Array.from({ length: 25 }, (_, index) => ({ raw: 10000 + (index % 5) }));
  assert.equal(isWindowStable(samples, { sampleCount: 25, maxRawSpan: 10, maxRawStddev: 10 }), true);
  samples[24].raw = 12000;
  assert.equal(isWindowStable(samples, { sampleCount: 25, maxRawSpan: 10, maxRawStddev: 10 }), false);
});

test("dynamic stability limits keep low-load strict and relax high-load points", () => {
  assert.deepEqual(
    stabilityLimitsForTarget({ targetLoadG: 21.8 }),
    { maxRawSpan: 1500, maxRawStddev: 550 }
  );
  assert.deepEqual(
    stabilityLimitsForTarget({ targetLoadG: 21.8, maxRawSpan: 1200, maxRawStddev: 350 }),
    { maxRawSpan: 1200, maxRawStddev: 350 }
  );
  assert.deepEqual(
    stabilityLimitsForTarget({ targetLoadG: 121.8, maxRawSpan: 1200, maxRawStddev: 350 }),
    { maxRawSpan: 1200, maxRawStddev: 350 }
  );

  const highLoad = stabilityLimitsForTarget({ targetLoadG: 1021.8, maxRawSpan: 1200, maxRawStddev: 350 });
  assert.equal(highLoad.maxRawSpan, 3600);
  assert.equal(highLoad.maxRawStddev, 1050);

  const firstHeavyLoad = stabilityLimitsForTarget({ targetLoadG: 221.8, maxRawSpan: 1200, maxRawStddev: 350 });
  assert.equal(firstHeavyLoad.maxRawSpan >= 1800, true);
  assert.equal(firstHeavyLoad.maxRawStddev >= 525, true);
});

test("dynamic stable window allows heavier points to converge with larger raw jitter", () => {
  const samples = Array.from({ length: 25 }, (_, index) => ({ raw: 100000 + ((index % 5) * 625) }));

  assert.equal(
    isWindowStable(samples, { sampleCount: 25, maxRawSpan: 1200, maxRawStddev: 350, targetLoadG: 21.8 }),
    false
  );
  assert.equal(
    isWindowStable(samples, { sampleCount: 25, maxRawSpan: 1200, maxRawStddev: 350, targetLoadG: 1021.8 }),
    true
  );
});

test("exports firmware artifacts for captured points", () => {
  const steps = createCalibrationSteps({ trayWeightG: 21.8, weightsG: [0, 5] });
  steps[0].captured = { rawMedian: 1000, rawMean: 1001, rawStddev: 2, rawSpan: 5, sampleCount: 25, timestamp: 1 };
  steps[1].captured = { rawMedian: 2200, rawMean: 2199, rawStddev: 3, rawSpan: 7, sampleCount: 25, timestamp: 2 };
  const points = capturedPointsFromSteps(steps);
  const header = buildCalibrationHeader(points, { trayWeightG: 21.8 });
  const source = buildCalibrationSource(points);
  assert.match(header, /CS1237_CAL_POINT_COUNT       2U/);
  assert.match(header, /CS1237_CAL0_REAL_X10         218U/);
  assert.match(header, /CS1237_CAL1_REAL_X10         268U/);
  assert.match(source, /CS1237_CAL0_RAW/);
  assert.equal(monotonicStatus(points).ok, true);
});

test("holds after each captured point until operator confirms the next weight is placed", () => {
  assert.deepEqual(
    holdForManualConfirmation({ activeIndex: 0, totalSteps: 8 }),
    { activeIndex: 0, running: false, waitingConfirm: true, complete: false, nextIndex: 1 }
  );
  assert.deepEqual(
    confirmNextCalibrationPoint({ activeIndex: 0, totalSteps: 8 }),
    { activeIndex: 1, running: true, waitingConfirm: false }
  );
});
