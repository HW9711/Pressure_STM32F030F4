const fs = require('fs');

const files = process.argv.slice(2);
if (files.length === 0) {
  throw new Error('usage: node verify_hex_layout.js <firmware.hex> [...]');
}

for (const file of files) {
  let upperAddress = 0;
  let minAddress = Number.POSITIVE_INFINITY;
  let maxAddress = 0;
  let dataBytes = 0;

  for (const line of fs.readFileSync(file, 'ascii').trim().split(/\r?\n/)) {
    if (!line.startsWith(':')) throw new Error(`${file}: invalid record`);
    const bytes = Buffer.from(line.slice(1), 'hex');
    const count = bytes[0];
    const address = (bytes[1] << 8) | bytes[2];
    const type = bytes[3];
    const checksum = bytes.reduce((sum, value) => (sum + value) & 0xff, 0);
    if (bytes.length !== count + 5 || checksum !== 0) {
      throw new Error(`${file}: bad Intel HEX checksum or length`);
    }

    if (type === 0x04) {
      upperAddress = ((bytes[4] << 8) | bytes[5]) << 16;
    } else if (type === 0x00) {
      const absolute = upperAddress + address;
      minAddress = Math.min(minAddress, absolute);
      maxAddress = Math.max(maxAddress, absolute + count);
      dataBytes += count;
    }
  }

  if (minAddress < 0x08000000 || maxAddress > 0x08003800) {
    throw new Error(`${file}: data overlaps reserved flash pages`);
  }
  console.log(`${file}: ${dataBytes} bytes, 0x${minAddress.toString(16)}..0x${maxAddress.toString(16)}`);
}
