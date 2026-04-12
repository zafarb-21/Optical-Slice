/*
 * WaveForms Protocol -> I2C -> Sensor mode passive spy/logger
 *
 * This script is intended for passive bus observation with a Digilent
 * Digital Discovery. It does not call I2C Write() or Read(), so it does
 * not intentionally drive the bus.
 *
 * Project-specific bus notes:
 * - STM32 PB6/PB7 are the upstream I2C1 bus to the master board.
 * - BH1750, VL53L1X, and WonderCam are not on PB6/PB7 in this project.
 * - Those sensors live on the downstream I2C bus behind the SC18IS604
 *   SPI-to-I2C bridge, so you must probe the downstream SCL/SDA nets if
 *   you want to see addresses 0x23, 0x29, or 0x32.
 *
 * Known downstream addresses:
 * - 0x23 : BH1750
 * - 0x5C : BH1750 alternate
 * - 0x29 : VL53L1X
 * - 0x32 : WonderCam
 *
 * Recommended WaveForms setup:
 * - Use Protocol -> I2C -> Sensor.
 * - Select your analyzer input pins for SCL and SDA in the UI.
 * - Disable "Debug with Logic Analyzer" for passive receive.
 * - Set the logic threshold to your actual bus voltage.
 * - Set Frequency near the real bus speed if you also use glitch filtering.
 */

var FILTER_ADDRESS_7BIT = -1; /* -1 = all addresses, or set like 0x29 */
var FILTER_DIRECTION = "";    /* "" = both, "R" = reads only, "W" = writes only */
var LOG_TO_FILE = false;      /* set true and provide LOG_FILE_PATH to save CSV */
var LOG_FILE_PATH = "";       /* example: "C:/temp/i2c_capture.csv" */
var PRINT_RAW_TOKENS = false; /* true to also print the raw Receive() token array */

var PROJECT_NAMES = {
  "35": "BH1750",
  "92": "BH1750_ALT",
  "41": "VL53L1X",
  "50": "WonderCam"
};

var TOKEN_START = -1;
var TOKEN_RESTART = -2;
var TOKEN_STOP = -3;

var g_log_file = null;
var g_frame_count = 0;
var g_summary = {};

function getI2cApi() {
  if (typeof Protocol !== "undefined" && Protocol && Protocol.I2C) {
    return Protocol.I2C;
  }

  if (typeof I2C !== "undefined" && I2C) {
    return I2C;
  }

  throw new Error("WaveForms I2C API not found. Use Protocol -> I2C and run this from the Protocol script context.");
}

function hex2(value) {
  var text = value.toString(16).toUpperCase();
  return (text.length < 2 ? "0" : "") + text;
}

function hex7(value) {
  return "0x" + hex2(value & 0x7F);
}

function nowMs() {
  return (new Date()).getTime();
}

function knownName(address_7bit) {
  var key = String(address_7bit & 0x7F);
  return PROJECT_NAMES[key] ? PROJECT_NAMES[key] : "";
}

function dataToText(bytes, acks) {
  var parts = [];
  var i;

  for (i = 0; i < bytes.length; i += 1) {
    parts.push("0x" + hex2(bytes[i]) + "(" + (acks[i] ? "ACK" : "NACK") + ")");
  }

  return parts.join(" ");
}

function passesFilter(frame) {
  if (FILTER_ADDRESS_7BIT >= 0 && frame.address_7bit !== FILTER_ADDRESS_7BIT) {
    return false;
  }

  if (FILTER_DIRECTION !== "" && frame.direction !== FILTER_DIRECTION) {
    return false;
  }

  return true;
}

function bumpSummary(frame) {
  var key;

  key = hex7(frame.address_7bit) + " " + frame.direction + " " + frame.address_ack;

  if (!g_summary[key]) {
    g_summary[key] = 0;
  }

  g_summary[key] += 1;
}

function emitLine(line) {
  print(line);

  if (g_log_file) {
    g_log_file.appendLine(line);
  }
}

function emitFrame(frame) {
  var name;
  var note;
  var line;

  if (frame.address_7bit < 0 || !passesFilter(frame)) {
    return;
  }

  g_frame_count += 1;
  bumpSummary(frame);

  name = knownName(frame.address_7bit);
  note = name !== "" ? name : "-";

  if (frame.note !== "") {
    note += "|" + frame.note;
  }

  line =
    g_frame_count +
    ",t_ms=" + nowMs() +
    ",start=" + frame.start_type +
    ",addr=" + hex7(frame.address_7bit) +
    ",dir=" + frame.direction +
    ",addr_ack=" + frame.address_ack +
    ",data=" + (frame.data.length ? dataToText(frame.data, frame.data_acks) : "-") +
    ",end=" + frame.end_type +
    ",note=" + note;

  emitLine(line);
}

function newFrame(start_type) {
  return {
    start_type: start_type,
    end_type: "",
    address_7bit: -1,
    direction: "",
    address_ack: "",
    data: [],
    data_acks: [],
    note: ""
  };
}

function decodeTransfers(tokens) {
  var frame = null;
  var i;
  var token;
  var value;
  var ack;

  if (!tokens || tokens.length === 0) {
    return;
  }

  if (PRINT_RAW_TOKENS) {
    emitLine("RAW," + tokens.join(","));
  }

  for (i = 0; i < tokens.length; i += 1) {
    token = tokens[i];

    if (token === TOKEN_START) {
      if (frame !== null) {
        frame.end_type = "IMPLICIT_START";
        frame.note = frame.note !== "" ? frame.note : "missing STOP before START";
        emitFrame(frame);
      }

      frame = newFrame("START");
      continue;
    }

    if (token === TOKEN_RESTART) {
      if (frame !== null) {
        frame.end_type = "RESTART";
        emitFrame(frame);
      }

      frame = newFrame("RESTART");
      continue;
    }

    if (token === TOKEN_STOP) {
      if (frame !== null) {
        frame.end_type = "STOP";
        emitFrame(frame);
        frame = null;
      }
      continue;
    }

    if (token < 0) {
      if (frame === null) {
        frame = newFrame("IMPLICIT");
      }

      frame.note = "error_token=" + token;
      continue;
    }

    if (frame === null) {
      frame = newFrame("IMPLICIT");
    }

    value = token >> 1;
    ack = (token & 0x1) === 0;

    if (frame.address_7bit < 0) {
      frame.address_7bit = (value >> 1) & 0x7F;
      frame.direction = (value & 0x1) ? "R" : "W";
      frame.address_ack = ack ? "ACK" : "NACK";
    } else {
      frame.data.push(value & 0xFF);
      frame.data_acks.push(ack);
    }
  }

  if (frame !== null) {
    frame.end_type = "END_OF_BUFFER";
    frame.note = frame.note !== "" ? frame.note : "buffer ended without STOP";
    emitFrame(frame);
  }
}

function initialize() {
  var i2c = getI2cApi();

  g_frame_count = 0;
  g_summary = {};
  g_log_file = null;

  if (LOG_TO_FILE && LOG_FILE_PATH !== "") {
    g_log_file = new File(LOG_FILE_PATH);
    g_log_file.writeLine("frame,host_time,start,address,direction,address_ack,data,end,note");
  }

  if (typeof Protocol !== "undefined" && Protocol && Protocol.Mode) {
    Protocol.Mode.text = "I2C";
  }

  i2c.Receiver();

  emitLine("I2C spy started");
  emitLine("Filter: address=" + (FILTER_ADDRESS_7BIT >= 0 ? hex7(FILTER_ADDRESS_7BIT) : "ALL") +
           " direction=" + (FILTER_DIRECTION !== "" ? FILTER_DIRECTION : "RW"));
  emitLine("Reminder: PB6/PB7 only show the upstream I2C1 bus on this board.");

  return true;
}

function loop() {
  var tokens = getI2cApi().Receive();

  decodeTransfers(tokens);

  return true;
}

function finish() {
  var key;

  emitLine("I2C spy finished");

  for (key in g_summary) {
    if (g_summary.hasOwnProperty(key)) {
      emitLine("SUMMARY," + key + "," + g_summary[key]);
    }
  }

  return "done";
}
