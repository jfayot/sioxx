Resource limits
===============

The built-in transports and decoders bound individual incoming messages.
These limits apply to both library-owned I/O and a caller-owned Asio context.

.. list-table:: Incoming limits
   :header-rows: 1
   :widths: 35 25 40

   * - Input
     - Limit
     - On exceeding the limit
   * - WS or WSS message, including all fragments
     - 16 MiB
     - The WebSocket connection closes.
   * - HTTP or HTTPS polling response body
     - 8 MiB, including all chunks
     - The transport reports an error and closes.
   * - Polling HTTP response headers and trailers
     - Beast's default 8 KiB header budget
     - The transport reports an error and closes.
   * - Polling network read buffer, including incomplete chunk metadata
     - 64 KiB
     - The transport reports an error and closes.
   * - JSON binary event or acknowledgement
     - 64 attachments; 8 MiB for the Socket.IO header and attachments combined
     - The decoder discards the pending packet and resumes at the next text
       packet.
   * - JSON or MessagePack arrays and objects
     - 128 nested containers, counting the outermost container as level one
     - The Socket.IO decoder discards the packet; an over-limit Engine.IO
       handshake is rejected.

Decoder and framing review
--------------------------

The JSON and MessagePack decoders and Engine.IO handshake parser build their
JSON trees through the same depth-limited SAX handler. The handler does not
reserve arrays or objects from advertised MessagePack element counts. The
MessagePack decoder stops at the first parse error, including with older
nlohmann-json versions, so truncated strings and binary values are bounded
by the available input.

JSON binary assembly retains at most one pending packet. Attachment indices
must be unique and cover exactly the advertised count, so reconstruction
cannot expand one attachment into many copies. A new text packet replaces an
incomplete assembly, and an oversized attachment releases it.

Polling delivers one packet at a time from its bounded response body. Packet
splitting and base64 decoding scan the available input; base64 output is no
larger than its encoded input. The HTTP body budget applies to declared
lengths, cumulative chunked bodies, and bodies delimited by connection close.
The header and read-buffer budgets also reject oversized framing metadata.
Proxy CONNECT responses skip their bodies and use Beast's header limit.

URL parsing and query construction operate on application-provided strings.
Applications accepting URLs or options from another trust boundary must bound
those values before connecting. Custom parsers must enforce their own decoding
and assembly limits; the transport limits still apply.

Operational boundaries
----------------------

Wire-size limits are not a process-memory quota. JSON trees, packet copies,
binary reconstruction, and application-retained values need additional memory.
There is no separate limit on the number of JSON values within a packet.

There is no receive-rate limit or global queue budget. Sustained traffic can
consume CPU and queue automatic heartbeat replies. Application emissions,
disconnected-event buffers, pending acknowledgements, and work dispatched by
callbacks also need application-level admission control. Use deployment-level
traffic and process limits when connecting to potentially hostile endpoints.

Polling requests have no request deadline. In library-owned I/O mode, a stalled
server or proxy can keep a blocking request and ``sync_close()`` waiting.
The receive-size checks do not provide a wall-clock shutdown guarantee.

Validation
----------

Unit tests exercise the exact polling body limit and one byte over it with
Content-Length, multiple chunks, and connection-close framing in both I/O
modes. They also reject oversized headers and chunk metadata. MessagePack
tests reject truncated strings, binary values, extensions, arrays, and maps
and verify decoding of the next valid packet.

The existing unit and E2E tests cover nesting, binary assembly limits and
recovery, and fragmented oversized WS/WSS messages. The sanitizer CI jobs fuzz
JSON, MessagePack, Engine.IO, URLs, and polling payloads. These checks cover
individual inputs; they do not establish a global memory or CPU bound under
sustained traffic.
