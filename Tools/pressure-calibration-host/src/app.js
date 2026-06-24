import { SerialConnection } from "./serial.js";
import {
  PressureHostFrameParser,
  buildCalibrationClearCommand,
  buildCalibrationPingCommand,
  buildCalibrationSetTableCommand,
  buildCalibrationSetThresholdCommand,
  CAL_CMD_SET_THRESHOLD,
  calibrationStatusText,
  describeCalibrationAck,
  formatHex
} from "./protocol.js";
import {
  DEFAULT_STANDARD_WEIGHTS_G,
  DEFAULT_TRAY_WEIGHT_G,
  buildCalibrationCsv,
  buildCalibrationHeader,
  buildCalibrationMarkdown,
  capturedPointsFromSteps,
  confirmNextCalibrationPoint,
  createCalibrationSteps,
  formatNumber,
  holdForManualConfirmation,
  isWindowStable,
  monotonicStatus,
  normalizeSamplingConfig,
  stabilityLimitsForTarget,
  summarizeRawSamples
} from "./calibration.js";

const $ = (id) => document.getElementById(id);

const state = {
  serial: new SerialConnection(),
  parser: new PressureHostFrameParser(),
  connected: false,
  seq: 0,
  steps: createCalibrationSteps(),
  activeIndex: 0,
  running: false,
  waitingConfirm: false,
  settleUntil: 0,
  latestFrame: null,
  frames: [],
  rawHistory: [],
  logs: [],
  lastFrameAt: 0,
  frameIntervals: [],
  samplingConfig: normalizeSamplingConfig(),
  samplingConfigDirty: false,
  thresholdDirty: false
};

function config() {
  return {
    ...state.samplingConfig,
    baudRate: Number($("baudRateInput").value || 9600)
  };
}

function readSamplingInputs() {
  return normalizeSamplingConfig({
    trayWeightG: $("trayWeightInput").value,
    sampleCount: $("sampleCountInput").value,
    settleMs: $("settleMsInput").value,
    maxRawSpan: $("maxSpanInput").value,
    maxRawStddev: $("maxStdInput").value
  });
}

function setSamplingConfigDirty(dirty) {
  state.samplingConfigDirty = dirty;
  const button = $("applySamplingConfigButton");
  button.dataset.dirty = String(dirty);
  button.textContent = dirty ? "确认写入参数 *" : "参数已写入";
}

function applySamplingConfig() {
  state.samplingConfig = readSamplingInputs();
  rebuildSteps();
  setSamplingConfigDirty(false);
  log("info", `采样参数已写入：托盘 ${state.samplingConfig.trayWeightG}g，样本 ${state.samplingConfig.sampleCount}，等待 ${state.samplingConfig.settleMs}ms，极差 ${state.samplingConfig.maxRawSpan}，标准差 ${state.samplingConfig.maxRawStddev}`);
}

function setThresholdDirty(dirty) {
  state.thresholdDirty = dirty;
  const button = $("sendThresholdButton");
  button.dataset.dirty = String(dirty);
  button.textContent = dirty ? "写入 MCU 阈值 *" : "写入 MCU 阈值";
}

async function sendThreshold() {
  const thresholdG = Number($("thresholdInput").value);
  const frame = buildCalibrationSetThresholdCommand(thresholdG, nextSeq());
  await sendCommand(frame, "SET_THRESHOLD");
}

function rebuildSteps() {
  state.steps = createCalibrationSteps({
    trayWeightG: config().trayWeightG,
    weightsG: DEFAULT_STANDARD_WEIGHTS_G
  });
  state.activeIndex = 0;
  state.running = false;
  state.waitingConfirm = false;
  renderAll();
}

function log(kind, message) {
  const item = {
    kind,
    message,
    timestamp: new Date().toLocaleTimeString()
  };
  state.logs.unshift(item);
  state.logs = state.logs.slice(0, 220);
  renderLogs();
}

function downloadText(filename, text, type = "text/plain;charset=utf-8") {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

function nextSeq() {
  const seq = state.seq;
  state.seq = (state.seq + 1) & 0xff;
  return seq;
}

async function sendCommand(bytes, label) {
  if (!state.serial.connected) {
    log("error", "串口未连接");
    return;
  }
  await state.serial.send(bytes);
  log("tx", `${label}: ${formatHex(bytes)}`);
}

function showAckDialog(details) {
  if (!details) {
    return;
  }
  const dialog = $("ackDialog");
  dialog.dataset.kind = details.kind;
  $("ackDialogTitle").textContent = details.title;
  $("ackDialogMessage").textContent = details.message;
  dialog.hidden = false;
  $("ackDialogClose").focus();
}

function hideAckDialog() {
  $("ackDialog").hidden = true;
}

function activeStep() {
  return state.steps[state.activeIndex] || null;
}

function stabilityOptionsForStep(step) {
  return {
    ...config(),
    targetLoadG: step?.targetLoadG ?? 0
  };
}

function setStepActive(index) {
  state.activeIndex = index;
  state.waitingConfirm = false;
  state.settleUntil = Date.now() + config().settleMs;
  const step = activeStep();
  if (step && !step.captured) {
    step.samples = [];
    step.status = "等待稳定";
  }
  renderAll();
}

function startCalibration() {
  state.running = true;
  state.waitingConfirm = false;
  const firstOpen = state.steps.findIndex((step) => !step.captured);
  setStepActive(firstOpen >= 0 ? firstOpen : 0);
  log("info", "开始自动标定");
}

function resetCalibration() {
  rebuildSteps();
  log("info", "已重置采样表");
}

function confirmNextStep() {
  if (!state.waitingConfirm) {
    log("error", "当前没有等待确认的下一点");
    return;
  }
  const transition = confirmNextCalibrationPoint({
    activeIndex: state.activeIndex,
    totalSteps: state.steps.length
  });
  state.activeIndex = transition.activeIndex;
  state.running = transition.running;
  state.waitingConfirm = transition.waitingConfirm;
  state.settleUntil = Date.now() + config().settleMs;
  const step = activeStep();
  if (step && !step.captured) {
    step.samples = [];
    step.status = "等待稳定";
    log("info", `确认进入点 ${step.index}：砝码 ${step.standardWeightG}g`);
  }
  renderAll();
}

function captureCurrentStep(force = false) {
  const step = activeStep();
  if (!step) {
    return false;
  }
  const stableConfig = stabilityOptionsForStep(step);
  const sampleCount = stableConfig.sampleCount;
  const windowSamples = step.samples.slice(-sampleCount);
  if (!force && windowSamples.length < sampleCount) {
    return false;
  }
  const samples = force && windowSamples.length === 0 && state.latestFrame ? [state.latestFrame] : windowSamples;
  const summary = summarizeRawSamples(samples);
  if (!force && !isWindowStable(samples, stableConfig)) {
    return false;
  }

  step.captured = {
    rawMean: summary.mean,
    rawMedian: summary.median,
    rawStddev: summary.stddev,
    rawSpan: summary.span,
    sampleCount: summary.count,
    timestamp: Date.now()
  };
  step.status = "已采集";
  step.samples = samples;
  log("info", `点 ${step.index} 已采集：砝码 ${step.standardWeightG}g raw=${Math.round(summary.median)}`);

  const transition = holdForManualConfirmation({
    activeIndex: state.activeIndex,
    totalSteps: state.steps.length
  });
  state.running = transition.running;
  state.waitingConfirm = transition.waitingConfirm;
  if (transition.waitingConfirm) {
    const next = state.steps[transition.nextIndex];
    if (next && !next.captured) {
      next.status = "等待确认";
    }
    log("info", `请放置 ${next?.standardWeightG ?? "--"}g 砝码后点击确认下一点`);
  } else {
    state.running = false;
    state.waitingConfirm = false;
    log("info", "8 点采集完成，可写入 MCU");
  }
  renderAll();
  return true;
}

function handleAutoCapture(frame) {
  const step = activeStep();
  if (!state.running || state.waitingConfirm || !step || step.captured) {
    return;
  }

  if (Date.now() < state.settleUntil) {
    step.status = "等待稳定";
    return;
  }

  step.samples.push(frame);
  const keep = Math.max(config().sampleCount * 4, 40);
  if (step.samples.length > keep) {
    step.samples.splice(0, step.samples.length - keep);
  }
  step.status = "采样中";
  captureCurrentStep(false);
}

function handleReportFrame(frame) {
  const now = Date.now();
  if (state.lastFrameAt > 0) {
    state.frameIntervals.push(now - state.lastFrameAt);
    state.frameIntervals = state.frameIntervals.slice(-10);
  }
  state.lastFrameAt = now;
  state.latestFrame = frame;
  state.frames.push(frame);
  state.frames = state.frames.slice(-1200);
  state.rawHistory.push(frame.raw);
  state.rawHistory = state.rawHistory.slice(-240);
  handleAutoCapture(frame);
  renderLive();
  renderSteps();
  renderChart();
}

function handleAck(ack) {
  const text = calibrationStatusText(ack.status);
  $("ackBox").textContent = `CMD 0x${ack.cmd.toString(16).padStart(2, "0")} / SEQ ${ack.seq} / ${text} / 运行时表 ${ack.runtimeEnabled ? "已启用" : "未启用"} / 点数 ${ack.pointCount}`;
  $("calState").textContent = ack.runtimeEnabled ? `MCU 标定表 ${ack.pointCount} 点` : "MCU 使用默认表";
  log(ack.status === 0 ? "ack" : "error", `ACK ${text}: ${ack.hex}`);
  if (ack.cmd === CAL_CMD_SET_THRESHOLD && ack.status === 0) {
    setThresholdDirty(false);
  }
  showAckDialog(describeCalibrationAck(ack));
}

function onSerialBytes(bytes, timestamp) {
  const events = state.parser.push(bytes, timestamp);
  for (const event of events) {
    if (event.type === "frame") {
      handleReportFrame(event.frame);
    } else if (event.type === "ack") {
      handleAck(event.ack);
    } else if (event.type.endsWith("error")) {
      log("error", event.error.message);
    }
  }
}

function renderLive() {
  const frame = state.latestFrame;
  $("connectionState").textContent = state.connected ? "已连接" : "未连接";
  $("connectionState").dataset.state = state.connected ? "on" : "off";

  if (!frame) {
    return;
  }

  $("frameState").textContent = `RX ${frame.hex.slice(0, 17)} ...`;
  $("liveRaw").textContent = String(frame.raw);
  $("liveWeight").textContent = formatNumber(frame.weightG, 1);
  $("liveThreshold").textContent = String(frame.thresholdG);
  if (!state.thresholdDirty && document.activeElement !== $("thresholdInput")) {
    $("thresholdInput").value = String(frame.thresholdG);
  }
  $("liveDevice").textContent = `${frame.deviceBits}${frame.knownDevice ? "" : " ?"}`;
  $("liveSeq").textContent = String(frame.seq);
  if (state.frameIntervals.length > 0) {
    const avgMs = state.frameIntervals.reduce((sum, value) => sum + value, 0) / state.frameIntervals.length;
    $("liveRate").textContent = `${formatNumber(1000 / avgMs, 1)} Hz`;
  }

  const step = activeStep();
  if (state.waitingConfirm) {
    const next = state.steps[state.activeIndex + 1];
    $("stableState").textContent = `当前点已完成 / 请放置 ${next?.standardWeightG ?? "--"}g 砝码后点击确认下一点`;
  } else if (step && state.running) {
    const stableConfig = stabilityOptionsForStep(step);
    const summary = summarizeRawSamples(step.samples.slice(-stableConfig.sampleCount));
    const limits = stabilityLimitsForTarget(stableConfig);
    const stable = isWindowStable(step.samples, stableConfig);
    $("stableState").textContent = `${step.status} / 样本 ${summary.count}/${stableConfig.sampleCount} / 极差 ${formatNumber(summary.span, 0)}/${limits.maxRawSpan} / 标准差 ${formatNumber(summary.stddev, 1)}/${limits.maxRawStddev} / ${stable ? "稳定" : "未稳定"}`;
  }
}

function renderSteps() {
  const body = $("stepsBody");
  body.innerHTML = "";
  for (const step of state.steps) {
    const captured = step.captured;
    const row = document.createElement("tr");
    row.dataset.active = String(step.index === state.activeIndex && state.running);
    row.dataset.done = String(Boolean(captured));
    row.innerHTML = `
      <td>${step.index}</td>
      <td>${step.standardWeightG} g</td>
      <td>${step.targetLoadG.toFixed(1)} g</td>
      <td>${captured ? Math.round(captured.rawMedian) : "--"}</td>
      <td>${captured ? captured.rawMean.toFixed(1) : "--"}</td>
      <td>${captured ? captured.rawStddev.toFixed(1) : "--"}</td>
      <td>${captured ? captured.rawSpan.toFixed(0) : "--"}</td>
      <td>${captured ? captured.sampleCount : step.samples.length}</td>
      <td>${captured ? "完成" : step.status}</td>
    `;
    body.append(row);
  }

  const done = state.steps.filter((step) => step.captured).length;
  $("progressBar").style.width = `${(done / state.steps.length) * 100}%`;
  const step = activeStep();
  if (state.waitingConfirm) {
    const next = state.steps[state.activeIndex + 1];
    $("autoPrompt").textContent = `点 ${state.activeIndex} 已完成：放 ${next?.standardWeightG ?? "--"}g 砝码后点击确认`;
  } else if (!state.running) {
    $("autoPrompt").textContent = done === state.steps.length ? "采集完成" : "等待开始";
  } else if (step) {
    const label = step.standardWeightG === 0 ? "仅放托盘" : `放 ${step.standardWeightG}g 砝码`;
    $("autoPrompt").textContent = `当前点 ${step.index}: ${label}`;
  }
}

function renderLogs() {
  const list = $("logList");
  list.innerHTML = "";
  for (const item of state.logs) {
    const line = document.createElement("div");
    line.className = "log-line";
    line.dataset.kind = item.kind;
    line.textContent = `${item.timestamp} ${item.message}`;
    list.append(line);
  }
}

function renderChart() {
  const canvas = $("rawChart");
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#101513";
  ctx.fillRect(0, 0, width, height);

  const values = state.rawHistory;
  if (values.length < 2) {
    $("chartRange").textContent = "--";
    return;
  }

  const min = Math.min(...values);
  const max = Math.max(...values);
  const pad = Math.max(1, (max - min) * 0.08);
  const lo = min - pad;
  const hi = max + pad;
  $("chartRange").textContent = `${Math.round(lo)} .. ${Math.round(hi)}`;

  ctx.strokeStyle = "rgba(255,255,255,0.12)";
  ctx.lineWidth = 1;
  for (let i = 1; i < 6; i += 1) {
    const y = (height / 6) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }

  ctx.strokeStyle = "#4ee18a";
  ctx.lineWidth = 2;
  ctx.beginPath();
  values.forEach((value, index) => {
    const x = (index / (values.length - 1)) * width;
    const y = height - ((value - lo) / (hi - lo)) * height;
    if (index === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  });
  ctx.stroke();
}

function renderAll() {
  renderLive();
  renderSteps();
  renderLogs();
  renderChart();
}

function pointsOrWarn(requireAll = true) {
  const points = capturedPointsFromSteps(state.steps);
  if (requireAll && points.length !== state.steps.length) {
    log("error", `标定点不完整：${points.length}/${state.steps.length}`);
    return null;
  }
  const monotonic = monotonicStatus(points);
  if (!monotonic.ok) {
    log("error", monotonic.message);
    return null;
  }
  return points;
}

async function sendTable() {
  const points = pointsOrWarn(true);
  if (!points) {
    return;
  }
  const frame = buildCalibrationSetTableCommand(points, nextSeq());
  await sendCommand(frame, "SET_TABLE");
}

function exportCsv() {
  const points = pointsOrWarn(false);
  if (points) {
    downloadText("pressure-calibration.csv", buildCalibrationCsv(points), "text/csv;charset=utf-8");
  }
}

function exportJson() {
  const points = capturedPointsFromSteps(state.steps);
  downloadText("pressure-calibration.json", JSON.stringify({
    trayWeightG: config().trayWeightG,
    points,
    frames: state.frames.slice(-200)
  }, null, 2), "application/json;charset=utf-8");
}

function exportHeader() {
  const points = pointsOrWarn(false);
  if (points) {
    downloadText("weight_calibration.h", buildCalibrationHeader(points, { trayWeightG: config().trayWeightG }), "text/plain;charset=utf-8");
  }
}

function exportMarkdown() {
  const points = pointsOrWarn(false);
  if (points) {
    downloadText("pressure-calibration-record.md", buildCalibrationMarkdown(points, { trayWeightG: config().trayWeightG }), "text/markdown;charset=utf-8");
  }
}

async function connectSerial() {
  if (state.serial.connected) {
    await state.serial.disconnect();
    return;
  }
  await state.serial.connect({
    baudRate: config().baudRate,
    dataBits: 8,
    stopBits: 1,
    parity: "none",
    flowControl: "none"
  });
}

function bindEvents() {
  state.serial.addEventListener("status", (event) => {
    state.connected = event.detail.connected;
    $("connectButton").textContent = state.connected ? "断开串口" : "连接串口";
    renderLive();
    log("info", state.connected ? "串口已连接" : "串口已断开");
  });
  state.serial.addEventListener("rx", (event) => {
    onSerialBytes(event.detail.bytes, event.detail.timestamp);
  });
  state.serial.addEventListener("tx", (event) => {
    log("tx", `TX ${formatHex(event.detail.bytes)}`);
  });
  state.serial.addEventListener("error", (event) => {
    log("error", event.detail.error.message);
  });

  document.addEventListener("click", (event) => {
    const button = event.target.closest("button");
    if (!button || button.disabled) {
      return;
    }
    button.classList.add("is-pressed");
    window.setTimeout(() => button.classList.remove("is-pressed"), 180);
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !$("ackDialog").hidden) {
      hideAckDialog();
    }
  });

  $("ackDialogClose").addEventListener("click", hideAckDialog);
  $("connectButton").addEventListener("click", () => connectSerial().catch((error) => log("error", error.message)));
  $("pingButton").addEventListener("click", () => sendCommand(buildCalibrationPingCommand(nextSeq()), "PING").catch((error) => log("error", error.message)));
  $("startButton").addEventListener("click", startCalibration);
  $("captureButton").addEventListener("click", () => captureCurrentStep(true));
  $("skipButton").addEventListener("click", confirmNextStep);
  $("resetButton").addEventListener("click", resetCalibration);
  $("sendTableButton").addEventListener("click", () => sendTable().catch((error) => log("error", error.message)));
  $("clearDeviceButton").addEventListener("click", () => sendCommand(buildCalibrationClearCommand(nextSeq()), "CLEAR_TABLE").catch((error) => log("error", error.message)));
  $("sendThresholdButton").addEventListener("click", () => sendThreshold().catch((error) => log("error", error.message)));
  $("thresholdInput").addEventListener("input", () => setThresholdDirty(true));
  $("exportCsvButton").addEventListener("click", exportCsv);
  $("exportJsonButton").addEventListener("click", exportJson);
  $("exportHeaderButton").addEventListener("click", exportHeader);
  $("exportMarkdownButton").addEventListener("click", exportMarkdown);
  $("clearLogButton").addEventListener("click", () => {
    state.logs = [];
    renderLogs();
  });
  $("applySamplingConfigButton").addEventListener("click", applySamplingConfig);
  [
    "trayWeightInput",
    "sampleCountInput",
    "settleMsInput",
    "maxSpanInput",
    "maxStdInput"
  ].forEach((id) => {
    $(id).addEventListener("input", () => setSamplingConfigDirty(true));
  });
  setSamplingConfigDirty(false);
  setThresholdDirty(false);
}

bindEvents();
renderAll();
