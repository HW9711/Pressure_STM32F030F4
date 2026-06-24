export const DEFAULT_TRAY_WEIGHT_G = 21.8;
export const DEFAULT_STANDARD_WEIGHTS_G = [0, 5, 20, 50, 100, 200, 500, 1000];
export const DEFAULT_SAMPLING_CONFIG = Object.freeze({
  trayWeightG: DEFAULT_TRAY_WEIGHT_G,
  sampleCount: 25,
  settleMs: 1500,
  maxRawSpan: 1500,
  maxRawStddev: 550
});

function numberOrDefault(value, fallback) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : fallback;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

export function normalizeSamplingConfig(input = {}) {
  return {
    trayWeightG: numberOrDefault(input.trayWeightG, DEFAULT_SAMPLING_CONFIG.trayWeightG),
    sampleCount: clamp(Math.round(numberOrDefault(input.sampleCount, DEFAULT_SAMPLING_CONFIG.sampleCount)), 5, 120),
    settleMs: clamp(Math.round(numberOrDefault(input.settleMs, DEFAULT_SAMPLING_CONFIG.settleMs)), 0, 10000),
    maxRawSpan: Math.max(1, Math.round(numberOrDefault(input.maxRawSpan, DEFAULT_SAMPLING_CONFIG.maxRawSpan))),
    maxRawStddev: Math.max(1, Math.round(numberOrDefault(input.maxRawStddev, DEFAULT_SAMPLING_CONFIG.maxRawStddev)))
  };
}

export function roundToX10(grams) {
  return Math.round(Number(grams) * 10);
}

export function formatNumber(value, digits = 1) {
  if (!Number.isFinite(value)) {
    return "--";
  }
  return Number(value).toFixed(digits);
}

export function createCalibrationSteps({ trayWeightG = DEFAULT_TRAY_WEIGHT_G, weightsG = DEFAULT_STANDARD_WEIGHTS_G } = {}) {
  return weightsG.map((standardWeightG, index) => ({
    index,
    standardWeightG,
    targetLoadG: Number(trayWeightG) + Number(standardWeightG),
    samples: [],
    captured: null,
    status: "pending"
  }));
}

export function mean(values) {
  if (values.length === 0) {
    return NaN;
  }
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

export function median(values) {
  if (values.length === 0) {
    return NaN;
  }
  const sorted = [...values].sort((a, b) => a - b);
  const center = Math.floor(sorted.length / 2);
  if (sorted.length % 2 === 1) {
    return sorted[center];
  }
  return (sorted[center - 1] + sorted[center]) / 2;
}

export function stddev(values) {
  if (values.length < 2) {
    return 0;
  }
  const avg = mean(values);
  const variance = values.reduce((sum, value) => sum + ((value - avg) ** 2), 0) / (values.length - 1);
  return Math.sqrt(variance);
}

export function summarizeRawSamples(samples) {
  const rawValues = samples.map((sample) => sample.raw);
  if (rawValues.length === 0) {
    return {
      count: 0,
      min: NaN,
      max: NaN,
      span: NaN,
      mean: NaN,
      median: NaN,
      stddev: NaN
    };
  }

  const min = Math.min(...rawValues);
  const max = Math.max(...rawValues);
  return {
    count: rawValues.length,
    min,
    max,
    span: max - min,
    mean: mean(rawValues),
    median: median(rawValues),
    stddev: stddev(rawValues)
  };
}

function positiveNumberOrDefault(value, fallback) {
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric > 0 ? numeric : fallback;
}

export function stabilityLimitsForTarget({
  targetLoadG = 0,
  maxRawSpan = 1500,
  maxRawStddev = 550,
  dynamicStartG = 200,
  dynamicFullScaleG = 1000,
  dynamicStartScale = 1.5
} = {}) {
  const baseSpan = positiveNumberOrDefault(maxRawSpan, 1500);
  const baseStddev = positiveNumberOrDefault(maxRawStddev, 550);
  const loadG = Math.max(0, Number(targetLoadG) || 0);
  const startG = Math.max(0, Number(dynamicStartG) || 200);
  const fullScaleG = Math.max(startG + 1, Number(dynamicFullScaleG) || 1000);
  const heavyStartScale = Math.max(1, Number(dynamicStartScale) || 1.5);
  const ratio = Math.min(1, Math.max(0, (loadG - startG) / (fullScaleG - startG)));
  const scale = loadG < startG ? 1 : heavyStartScale + (ratio * (3 - heavyStartScale));

  return {
    maxRawSpan: Math.round(baseSpan * scale),
    maxRawStddev: Math.round(baseStddev * scale)
  };
}

export function isWindowStable(samples, {
  sampleCount = 25,
  maxRawSpan = 1500,
  maxRawStddev = 550,
  targetLoadG = 0,
  dynamicStartG = 200,
  dynamicFullScaleG = 1000,
  dynamicStartScale = 1.5
} = {}) {
  if (samples.length < sampleCount) {
    return false;
  }
  const summary = summarizeRawSamples(samples.slice(-sampleCount));
  const limits = stabilityLimitsForTarget({
    targetLoadG,
    maxRawSpan,
    maxRawStddev,
    dynamicStartG,
    dynamicFullScaleG,
    dynamicStartScale
  });
  return summary.span <= limits.maxRawSpan && summary.stddev <= limits.maxRawStddev;
}

export function holdForManualConfirmation({ activeIndex, totalSteps }) {
  const lastIndex = Math.max(0, Number(totalSteps) - 1);
  if (Number(activeIndex) >= lastIndex) {
    return {
      activeIndex,
      running: false,
      waitingConfirm: false,
      complete: true,
      nextIndex: null
    };
  }
  return {
    activeIndex,
    running: false,
    waitingConfirm: true,
    complete: false,
    nextIndex: Number(activeIndex) + 1
  };
}

export function confirmNextCalibrationPoint({ activeIndex, totalSteps }) {
  const lastIndex = Math.max(0, Number(totalSteps) - 1);
  if (Number(activeIndex) >= lastIndex) {
    return {
      activeIndex,
      running: false,
      waitingConfirm: false
    };
  }
  return {
    activeIndex: Number(activeIndex) + 1,
    running: true,
    waitingConfirm: false
  };
}

export function capturedPointsFromSteps(steps) {
  return steps
    .filter((step) => step.captured)
    .map((step) => ({
      index: step.index,
      standardWeightG: step.standardWeightG,
      targetLoadG: step.targetLoadG,
      targetX10: roundToX10(step.targetLoadG),
      raw: Math.round(step.captured.rawMedian),
      rawMean: step.captured.rawMean,
      rawMedian: step.captured.rawMedian,
      rawStddev: step.captured.rawStddev,
      rawSpan: step.captured.rawSpan,
      sampleCount: step.captured.sampleCount,
      timestamp: step.captured.timestamp
    }));
}

export function monotonicStatus(points) {
  if (points.length < 2) {
    return { ok: true, direction: "unknown", message: "点数不足，暂不判断单调性。" };
  }
  let up = true;
  let down = true;
  for (let index = 1; index < points.length; index += 1) {
    up &&= points[index].raw > points[index - 1].raw;
    down &&= points[index].raw < points[index - 1].raw;
  }
  if (up) {
    return { ok: true, direction: "up", message: "raw 随砝码递增。" };
  }
  if (down) {
    return { ok: true, direction: "down", message: "raw 随砝码递减。" };
  }
  return { ok: false, direction: "mixed", message: "raw 非单调，建议重采异常点或检查接线/放码顺序。" };
}

function pointMacroLines(points) {
  return points.flatMap((point, index) => [
    `#define CS1237_CAL${index}_REAL_X10         ${point.targetX10}U`,
    `#define CS1237_CAL${index}_RAW              ${point.raw}`
  ]);
}

function generatedAtString() {
  const date = new Date();
  const pad = (value) => String(value).padStart(2, "0");
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

export function buildCalibrationHeader(points, { trayWeightG = DEFAULT_TRAY_WEIGHT_G } = {}) {
  const lines = [
    "#ifndef __WEIGHT_CALIBRATION_H__",
    "#define __WEIGHT_CALIBRATION_H__",
    "",
    "#ifdef __cplusplus",
    'extern "C" {',
    "#endif",
    "",
    '#include "main.h"',
    "",
    "/* CS1237 原始重量换算基础系数；分段标定打开后仅作为异常回退路径。 */",
    "#define CS1237_SCALE_BASE            14751U",
    "#define CS1237_SCALE_FACTOR          CS1237_SCALE_BASE",
    "#define CS1237_SEGMENT_CAL_ENABLE    1U",
    `#define CS1237_CAL_POINT_COUNT       ${points.length}U`,
    `#define CS1237_CAL_TRAY_WEIGHT_X10   ${roundToX10(trayWeightG)}U`,
    "",
    "/* 上位机生成的标定点：目标重量包含 21.8g 托盘和对应砝码重量。 */",
    ...pointMacroLines(points),
    "",
    "/* 按当前工程的分段标定点，把 CS1237 原始值换算成 0.1g。 */",
    "uint32_t WeightCalibration_ApplySegmentCalibrationX10(int32_t raw_value);",
    "",
    "#ifdef __cplusplus",
    "}",
    "#endif",
    "",
    "#endif /* __WEIGHT_CALIBRATION_H__ */",
    ""
  ];
  return lines.join("\n");
}

function cArrayMacroLines(points, fieldSuffix, indent = "      ") {
  return points.map((point, index) => {
    const comma = index === points.length - 1 ? "" : ",";
    return `${indent}CS1237_CAL${index}_${fieldSuffix}${comma}`;
  });
}

export function buildCalibrationSource(points) {
  const lines = [
    '#include "weight_calibration.h"',
    '#include "cs1237.h"',
    "",
    "/* 两点之间做线性插值，返回 0.1g 单位的换算结果。 */",
    "static uint32_t WeightCalibration_InterpolateSegmentX10(int32_t raw_value,",
    "                                                        int32_t raw_lo,",
    "                                                        int32_t raw_hi,",
    "                                                        uint32_t real_lo_x10,",
    "                                                        uint32_t real_hi_x10)",
    "{",
    "  int64_t delta_raw = (int64_t)raw_hi - (int64_t)raw_lo;",
    "  int64_t delta_real = (int64_t)real_hi_x10 - (int64_t)real_lo_x10;",
    "  int64_t numerator;",
    "  int64_t corrected_x10;",
    "",
    "  if (delta_raw == 0LL) {",
    "    return real_lo_x10;",
    "  }",
    "",
    "  numerator = ((int64_t)raw_value - (int64_t)raw_lo) * delta_real;",
    "",
    "  if (numerator >= 0) {",
    "    corrected_x10 = (int64_t)real_lo_x10 + ((numerator + (delta_raw / 2LL)) / delta_raw);",
    "  } else {",
    "    corrected_x10 = (int64_t)real_lo_x10 + ((numerator - (delta_raw / 2LL)) / delta_raw);",
    "  }",
    "",
    "  if (corrected_x10 < 0LL) {",
    "    return 0U;",
    "  }",
    "",
    "  return (uint32_t)corrected_x10;",
    "}",
    "",
    "/* 将原始码值映射到最近的分段区间，并输出换算后的重量。 */",
    "uint32_t WeightCalibration_ApplySegmentCalibrationX10(int32_t raw_value)",
    "{",
    "#if (CS1237_SEGMENT_CAL_ENABLE != 0U)",
    "  static const int32_t raw_points[CS1237_CAL_POINT_COUNT] = {",
    ...cArrayMacroLines(points, "RAW"),
    "  };",
    "  static const uint32_t real_points[CS1237_CAL_POINT_COUNT] = {",
    ...cArrayMacroLines(points, "REAL_X10"),
    "  };",
    "  uint8_t i;",
    "  uint8_t best_idx = 0U;",
    "  uint32_t best_distance = 0xFFFFFFFFU;",
    "",
    "  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {",
    "    if (raw_points[i] == raw_points[i + 1U]) {",
    "      return CS1237_GetMeasurementX10();",
    "    }",
    "  }",
    "",
    "  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {",
    "    int32_t raw_a = raw_points[i];",
    "    int32_t raw_b = raw_points[i + 1U];",
    "    int32_t range_min = (raw_a < raw_b) ? raw_a : raw_b;",
    "    int32_t range_max = (raw_a > raw_b) ? raw_a : raw_b;",
    "",
    "    if ((raw_value >= range_min) && (raw_value <= range_max)) {",
    "      return WeightCalibration_InterpolateSegmentX10(raw_value,",
    "                                                     raw_a,",
    "                                                     raw_b,",
    "                                                     real_points[i],",
    "                                                     real_points[i + 1U]);",
    "    }",
    "  }",
    "",
    "  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {",
    "    int32_t raw_a = raw_points[i];",
    "    int32_t raw_b = raw_points[i + 1U];",
    "    uint32_t distance_a = (raw_value > raw_a) ? (uint32_t)(raw_value - raw_a) : (uint32_t)(raw_a - raw_value);",
    "    uint32_t distance_b = (raw_value > raw_b) ? (uint32_t)(raw_value - raw_b) : (uint32_t)(raw_b - raw_value);",
    "    uint32_t segment_distance = (distance_a < distance_b) ? distance_a : distance_b;",
    "",
    "    if (segment_distance < best_distance) {",
    "      best_distance = segment_distance;",
    "      best_idx = i;",
    "    }",
    "  }",
    "",
    "  return WeightCalibration_InterpolateSegmentX10(raw_value,",
    "                                                 raw_points[best_idx],",
    "                                                 raw_points[best_idx + 1U],",
    "                                                 real_points[best_idx],",
    "                                                 real_points[best_idx + 1U]);",
    "#else",
    "  return CS1237_GetMeasurementX10();",
    "#endif",
    "}",
    ""
  ];
  return lines.join("\n");
}

export function buildCalibrationCsv(points) {
  const rows = [
    ["index", "standard_weight_g", "target_load_g", "target_x10", "raw", "raw_mean", "raw_median", "raw_stddev", "raw_span", "sample_count"],
    ...points.map((point, index) => [
      index,
      point.standardWeightG,
      point.targetLoadG.toFixed(1),
      point.targetX10,
      point.raw,
      point.rawMean.toFixed(3),
      point.rawMedian.toFixed(3),
      point.rawStddev.toFixed(3),
      point.rawSpan,
      point.sampleCount
    ])
  ];
  return rows.map((row) => row.join(",")).join("\n") + "\n";
}

export function buildCalibrationMarkdown(points, options = {}) {
  const monotonic = monotonicStatus(points);
  const lines = [
    "# 压力传感器标定记录",
    "",
    `- 生成时间：${generatedAtString()}`,
    `- 托盘重量：${formatNumber(options.trayWeightG ?? DEFAULT_TRAY_WEIGHT_G, 1)} g`,
    `- 标定点数：${points.length}`,
    `- raw 单调性：${monotonic.message}`,
    "",
    "| 序号 | 砝码(g) | 目标总重(g) | 目标0.1g | raw | raw均值 | raw标准差 | raw极差 | 样本数 |",
    "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ...points.map((point, index) => (
      `| ${index} | ${point.standardWeightG} | ${point.targetLoadG.toFixed(1)} | ${point.targetX10} | ${point.raw} | ${point.rawMean.toFixed(2)} | ${point.rawStddev.toFixed(2)} | ${point.rawSpan} | ${point.sampleCount} |`
    )),
    ""
  ];
  return lines.join("\n");
}
