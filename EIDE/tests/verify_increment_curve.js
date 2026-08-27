const fs = require('fs');
const path = require('path');

const csvPath = process.argv[2];
if (!csvPath) {
  throw new Error('usage: node verify_increment_curve.js <comparison.csv>');
}

const lines = fs.readFileSync(csvPath, 'utf8').trim().split(/\r?\n/);
const keys = lines[0].split(',');
const rows = lines.slice(1).map((line) => {
  const values = line.split(',');
  return Object.fromEntries(keys.map((key, index) => [key, values[index]]));
});

const weightRows = rows.filter((row) => row.stage === 'weight');
const trayBySensor = new Map(
  weightRows
    .filter((row) => Number(row.standard_weight_g) === 0)
    .map((row) => [row.sensor_id, Number(row.delta_raw)]),
);

const grouped = new Map();
for (const row of weightRows) {
  const weight = Number(row.standard_weight_g);
  const increment = Number(row.delta_raw) - trayBySensor.get(row.sensor_id);
  if (!grouped.has(weight)) grouped.set(weight, []);
  grouped.get(weight).push(increment);
}

const curve = [...grouped.entries()]
  .sort((a, b) => a[0] - b[0])
  .map(([weight, source]) => {
    const sorted = [...source].sort((a, b) => a - b);
    const trimmed = sorted.slice(1, -1);
    const trimmedMean = Math.round(trimmed.reduce((sum, value) => sum + value, 0) / trimmed.length);
    return { weight, trimmedMean, min: sorted[0], max: sorted.at(-1) };
  });

const headerPath = path.resolve(__dirname, '../../Core/Inc/weight_calibration.h');
const header = fs.readFileSync(headerPath, 'utf8');
console.table(curve);
const interpolateWeight = (raw) => {
  if (raw <= 0) return 0;
  let index = curve.findIndex((point) => raw <= point.trimmedMean);
  if (index < 1) index = curve.length - 1;
  const lo = curve[index - 1];
  const hi = curve[index];
  return lo.weight + ((raw - lo.trimmedMean) * (hi.weight - lo.weight)) /
    (hi.trimmedMean - lo.trimmedMean);
};
const errorSummary = curve.slice(1).map((point) => {
  const predicted = grouped.get(point.weight).map(interpolateWeight);
  const errors = predicted.map((value) => value - point.weight);
  return {
    weight: point.weight,
    predictedMin: Number(Math.min(...predicted).toFixed(2)),
    predictedMax: Number(Math.max(...predicted).toFixed(2)),
    worstAbsError: Number(Math.max(...errors.map(Math.abs)).toFixed(2)),
  };
});
console.table(errorSummary);

const absoluteGroups = new Map([[0, rows.filter((row) => row.stage === 'zero').map(() => 0)]]);
for (const row of weightRows) {
  const load = Number(row.applied_load_g);
  if (!absoluteGroups.has(load)) absoluteGroups.set(load, []);
  absoluteGroups.get(load).push(Number(row.delta_raw));
}
const absoluteCurve = [...absoluteGroups.entries()]
  .sort((a, b) => a[0] - b[0])
  .map(([weight, source]) => {
    const sorted = [...source].sort((a, b) => a - b);
    const trimmed = sorted.length > 2 ? sorted.slice(1, -1) : sorted;
    return {
      weight,
      trimmedMean: Math.round(trimmed.reduce((sum, value) => sum + value, 0) / trimmed.length),
    };
  });
for (const point of absoluteCurve) {
  const suffix = point.weight === 0 ? 'EMPTY' : `${String(point.weight).replace('.', 'P')}G`;
  const macro = `WEIGHT_CALIBRATION_RAW_${suffix}`;
  const match = header.match(new RegExp(`#define\\s+${macro}\\s+(-?\\d+)`));
  if (!match || Number(match[1]) !== point.trimmedMean) {
    throw new Error(`${macro}: CSV=${point.trimmedMean}, firmware=${match?.[1] ?? 'missing'}`);
  }
}
const interpolateAbsolute = (raw) => {
  if (raw <= 0) return 0;
  let index = absoluteCurve.findIndex((point) => raw <= point.trimmedMean);
  if (index < 1) index = absoluteCurve.length - 1;
  const lo = absoluteCurve[index - 1];
  const hi = absoluteCurve[index];
  return lo.weight + ((raw - lo.trimmedMean) * (hi.weight - lo.weight)) /
    (hi.trimmedMean - lo.trimmedMean);
};
const absoluteSubtractErrors = curve.slice(1).map((point) => {
  const predictions = weightRows
    .filter((row) => Number(row.standard_weight_g) === point.weight)
    .map((row) => interpolateAbsolute(Number(row.delta_raw)) -
      interpolateAbsolute(trayBySensor.get(row.sensor_id)));
  return {
    weight: point.weight,
    predictedMin: Number(Math.min(...predictions).toFixed(2)),
    predictedMax: Number(Math.max(...predictions).toFixed(2)),
    worstAbsError: Number(Math.max(...predictions.map((value) => Math.abs(value - point.weight))).toFixed(2)),
  };
});
console.table(absoluteCurve);
console.table(absoluteSubtractErrors);
console.log(`verified ${absoluteCurve.length} CSV-derived firmware points`);
