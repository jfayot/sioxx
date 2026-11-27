sioxx API Reference
===================

.. This page is populated from Doxygen XML through Breathe.
   The Breathe project name used below must match breathe_projects in conf.py.

.. default-domain:: cpp

The API is centered on :cpp:class:`sioxx::client`, which owns the Engine.IO
connection, and :cpp:class:`sioxx::socket`, which represents one Socket.IO
namespace.

The v1.x source compatibility promise covers the documented client, socket,
options, callbacks, message types and helpers, and the parser extension API
below. Rebuild and relink when upgrading the shared library.

.. contents:: API contents
   :local:
   :depth: 2

Client
------

Configure a client with :cpp:struct:`sioxx::client_options`, obtain namespace
sockets with :cpp:func:`sioxx::client::socket`, and then connect to the
server.

v1.0 supports both library-owned I/O and a caller-owned Boost.Asio
``io_context``. The four reconnect settings stay on ``client_options``.
Coroutine interfaces, outgoing catch-all listeners, and richer connection and
transport diagnostics are deferred to later releases.

Invalid options can throw synchronously. Client construction throws
``std::invalid_argument`` if a custom parser factory returns null.
``client::connect()`` throws ``std::invalid_argument`` for a reserved
Engine.IO query key or an invalid proxy URI.

* ``set_open_listener()`` observes the Engine.IO handshake. Use
  ``socket::on_connect()`` to observe a namespace connection.
* ``set_error_listener()`` receives diagnostics, including transport errors
  and WebSocket-to-polling fallback. A connection can still succeed after
  a diagnostic.
* ``set_fail_listener()`` reports exhaustion of configured reconnect attempts.
* Malformed Engine.IO open handshakes fail the connection attempt through
  the transport error listener.
* A namespace rejection delivers ``connect_error`` to that socket's event
  listeners.

.. doxygenenum:: sioxx::parser_kind
   :project: sioxx

.. doxygenstruct:: sioxx::proxy_options
   :project: sioxx
   :members:

.. doxygenstruct:: sioxx::client_options
   :project: sioxx
   :members:

.. doxygenclass:: sioxx::client
   :project: sioxx
   :members:
   :protected-members:

Socket
------

A :cpp:class:`sioxx::socket` represents a single Socket.IO namespace. It
provides event listeners, event emission, acknowledgements and namespace
connection lifecycle callbacks. Obtain it with :cpp:func:`sioxx::client::socket`.

A retained shared pointer keeps the socket alive. Network operations require a
live client. Callbacks may execute on a library worker or the supplied context.
Keep referenced state alive until callbacks finish, including those already
dispatched when a listener is replaced or removed. With a caller-owned
context, run it until the client's pending transport work has drained.

Emitting after explicit shutdown throws ``std::logic_error`` until
``client::connect()`` is called again. The timed ACK overloads throw
``std::invalid_argument`` for a non-positive timeout or an empty callback.

.. doxygenclass:: sioxx::socket
   :project: sioxx
   :members:
   :protected-members:

Messages
--------

Socket.IO values are represented directly with ``nlohmann::json``.
``sioxx::json`` and ``sioxx::message`` are aliases for ``nlohmann::json``,
while ``sioxx::message_list`` represents an array of event arguments.

.. doxygenfile:: message.hpp
   :project: sioxx

Parser extension API
--------------------

Applications can provide a custom wire-format parser by deriving from
:cpp:class:`sioxx::parser_base` and assigning a factory to
:cpp:member:`sioxx::client_options::parser_factory`.

.. doxygenenum:: sioxx::packet_type
   :project: sioxx

.. doxygenstruct:: sioxx::packet
   :project: sioxx
   :members:

.. doxygentypedef:: sioxx::frame_writer
   :project: sioxx

.. doxygenclass:: sioxx::parser_base
   :project: sioxx
   :members:
   :protected-members:

Related links
-------------

* `Source repository <https://github.com/jfayot/sioxx>`_
* `Issue tracker <https://github.com/jfayot/sioxx/issues>`_
