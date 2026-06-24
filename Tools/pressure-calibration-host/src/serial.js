export class SerialConnection extends EventTarget {
  constructor() {
    super();
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.keepReading = false;
  }

  get supported() {
    return typeof navigator !== "undefined" && "serial" in navigator;
  }

  get connected() {
    return Boolean(this.port && this.reader);
  }

  async connect(options) {
    if (!this.supported) {
      throw new Error("Web Serial is not supported. Use Chrome or Edge through localhost.");
    }

    this.port = await navigator.serial.requestPort();
    await this.port.open({
      baudRate: options.baudRate,
      dataBits: options.dataBits,
      stopBits: options.stopBits,
      parity: options.parity,
      flowControl: options.flowControl
    });

    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.keepReading = true;
    this.dispatchEvent(new CustomEvent("status", { detail: { connected: true } }));
    void this.readLoop();
  }

  async disconnect() {
    this.keepReading = false;
    if (this.reader) {
      await this.reader.cancel().catch(() => {});
      this.reader.releaseLock();
      this.reader = null;
    }
    if (this.writer) {
      this.writer.releaseLock();
      this.writer = null;
    }
    if (this.port) {
      await this.port.close().catch(() => {});
      this.port = null;
    }
    this.dispatchEvent(new CustomEvent("status", { detail: { connected: false } }));
  }

  async readLoop() {
    try {
      while (this.keepReading && this.reader) {
        const { value, done } = await this.reader.read();
        if (done || !this.keepReading) {
          break;
        }
        if (value && value.length > 0) {
          this.dispatchEvent(new CustomEvent("rx", { detail: { bytes: value, timestamp: Date.now() } }));
        }
      }
    } catch (error) {
      this.dispatchEvent(new CustomEvent("error", { detail: { error } }));
    } finally {
      if (this.keepReading) {
        await this.disconnect();
      }
    }
  }

  async send(bytes) {
    if (!this.writer) {
      throw new Error("Serial port is not connected.");
    }
    const payload = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
    await this.writer.write(payload);
    this.dispatchEvent(new CustomEvent("tx", { detail: { bytes: payload, timestamp: Date.now() } }));
  }
}
