'use strict';

const { EventEmitter } = require('events');
const { Encoder: CborEncoder } = require('cbor-x');

const protocol = 5;
const CONNECT = 0;
const DISCONNECT = 1;
const EVENT = 2;
const ACK = 3;
const CONNECT_ERROR = 4;

// Disabling records keeps the output as ordinary RFC 8949 CBOR maps, matching
// nlohmann::json::to_cbor/from_cbor in examples/cbor_parser.hpp.
const codec = new CborEncoder({ useRecords: false, mapsAsObjects: true });

class Encoder {
  encode(packet) {
    return [Buffer.from(codec.encode(packet))];
  }
}

class Decoder extends EventEmitter {
  add(data) {
    const packet = codec.decode(data);
    checkPacket(packet);
    this.emit('decoded', packet);
  }

  destroy() {}
}

function checkPacket(packet) {
  const validType = Number.isInteger(packet.type)
    && packet.type >= CONNECT
    && packet.type <= CONNECT_ERROR;
  if (!validType) throw new Error('invalid packet type');
  if (typeof packet.nsp !== 'string') throw new Error('invalid namespace');

  const isObject = packet.data !== null
    && typeof packet.data === 'object'
    && !Array.isArray(packet.data);
  let validData;
  switch (packet.type) {
    case CONNECT:
      validData = packet.data === undefined || isObject;
      break;
    case DISCONNECT:
      validData = packet.data === undefined;
      break;
    case CONNECT_ERROR:
      validData = typeof packet.data === 'string' || isObject;
      break;
    case EVENT:
    case ACK:
      validData = Array.isArray(packet.data);
      break;
    default:
      validData = false;
  }
  if (!validData) throw new Error('invalid payload');
  if (packet.id !== undefined && !Number.isInteger(packet.id)) {
    throw new Error('invalid packet id');
  }
}

module.exports = { protocol, Encoder, Decoder };
