//*************************************************************************
// File cex.hpp
// Date 22.06.2018
// Copyright (c) 2018-2019 by Patrick Fial
//-------------------------------------------------------------------------
// cex Library include wrapper
//*************************************************************************

#ifndef __CEX_HPP__
#define __CEX_HPP__

//***************************************************************************
// includes
//***************************************************************************

#include "cex/core.hpp"

/*! \mainpage My Personal Index Page
 *
 * \section intro_sec Introduction
 *
 * `libcex` - embeddeded C++11 webserver.
 *
 * \section require_sec Requirements
 * \subsection manda_sec Mandatory libraries
 *
 * \li <a href="http://libevent.org/">libevent</a>
 * \li <a href="https://github.com/criticalstack/libevhtp">libevhtp</a>
 *   - For WebSocket support, use <a href="https://github.com/hispid/libevhtp_ws">libevhtp_ws</a> instead
 *
 * \subsection optional_sec Optional dependencies
 * \li OpenSSL (for HTTPS)
 * \li libz (for deflate/gzip compression)

 * \section installation_sec Installation
 * ``` 
 * git clone https://github.com/hispid/libcex .
 * mkdir build
 * cd build
 * cmake ..
 * make
 * ``` 
 * If cmake cannot find your OpenSSL installation, or you've installed in a non-standard location, you might want to add `-DOPENSSL_ROOT_DIR=/path/to/ssl` to the cmake call.
 * \section usage_sec Usage
 * `libcex` is built around the concept of middleware functions which serve both for routing and also for request/response interaction and processing. A minimal example might look like:
 * ```
 * 
   #include <cex.hpp>

   int main()
   {
      cex::Server app;

      app.get([](cex::Request* req, cex::Response* res, std::function<void()> next)
      {
         res->end(200);
      });
   
      app.listen("127.0.0.1", 5555, true);   // blocks

      return 0;
   }

 * ```
 * The \link cex::Server \endlink class provides a set of functions to attach middlewares for various situations. Middleware functions can be installed 
 * for a given HTTP method (GET, POST) or for a given URL-path.
 * Each middleware function receives 3 parameters:
 * \li The cex::Request object contaning everything about the incoming request
 * \li The cex::Response object which is used to create a response
 * \li A function pointer which shall be used to skip to the next middleware
 *
 * All registered middleware functions are executed in the order they have been registered, for each request. A middleware function is only executed, when its specification (HTTP method, URL-path) matches the incoming request.
 * Execution of middlewares stops as soon as one of the following happens:
 * \li The last registered middleware was executed
 * \li The `next` method is not called
 *
 * The method \link cex::Response::end \endlink is used to send a response to the client. This can be just a statuscode, or also a payload.
 *
 * \section websocket_sec WebSocket Support
 * `libcex` provides WebSocket support for full-duplex communication when built with <a href="https://github.com/hispid/libevhtp_ws">libevhtp_ws</a>. The \link cex::Server::websocket \endlink method allows registering WebSocket handlers:
 * ```
 * app.websocket("/ws", 
 *     [](const cex::WebSocket& ws) {
 *         // Called when a WebSocket connection is established
 *     },
 *     [](const cex::WebSocket& ws, const char* data, size_t len, cex::WebSocket::FrameType type) {
 *         // Called when a WebSocket message is received
 *         ws.send("Echo: ");
 *         ws.send(data, len);
 *     },
 *     [](const cex::WebSocket& ws) {
 *         // Called when a WebSocket connection is closed
 *     }
 * );
 * ```
 * See \link cex::WebSocket \endlink for more information about WebSocket API.
 */


//***************************************************************************
#endif // __CEX_HPP__
