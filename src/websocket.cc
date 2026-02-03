//*************************************************************************
// File websocket.cc
// Date 2024
//-------------------------------------------------------------------------
// cex Library WebSocket class implementation
//*************************************************************************

//***************************************************************************
// includes
//***************************************************************************

#include <cex/core.hpp>
#include <cex/util.hpp>
#include <cstring>

#ifdef EVHTP_WS_SUPPORT
#include <evhtp/ws/evhtp_ws.h>

namespace cex
{

//***************************************************************************
// class WebSocket
//***************************************************************************
// ctor/dtor
//***************************************************************************

WebSocket::WebSocket(evhtp_request* req)
   : req(req)
{
}

//***************************************************************************
// send (text message)
//***************************************************************************

int WebSocket::send(const char* message)
{
   if (!message || !req)
      return fail;

   return sendFrame(message, strlen(message), wsText);
}

//***************************************************************************
// send (binary message)
//***************************************************************************

int WebSocket::send(const char* data, size_t len)
{
   return sendFrame(data, len, wsBinary);
}

//***************************************************************************
// sendFrame
//***************************************************************************

int WebSocket::sendFrame(const char* data, size_t len, FrameType frameType)
{
   if (!req || !data || len == 0)
      return fail;

   // Convert our FrameType to libevhtp_ws opcode
   uint8_t opcode;
   switch (frameType)
   {
      case wsText:   opcode = OP_TEXT; break;
      case wsBinary: opcode = OP_BIN; break;
      case wsPing:   opcode = OP_PING; break;
      case wsPong:   opcode = OP_PONG; break;
      case wsClose:  opcode = OP_CLOSE; break;
      default:        opcode = OP_TEXT; break;
   }

   // Create a buffer with the data
   struct evbuffer* buf = evbuffer_new();
   if (!buf)
      return fail;

   evbuffer_add(buf, data, len);
   
   // Add WebSocket header
   evhtp_ws_add_header(buf, opcode);
   
   // Send the frame
   evhtp_send_reply_body(req, buf);
   
   evbuffer_free(buf);
   
   return success;
}

//***************************************************************************
// close
//***************************************************************************

void WebSocket::close()
{
   if (req)
   {
      evhtp_ws_disconnect(req);
   }
}

//***************************************************************************
// isOpen
//***************************************************************************

bool WebSocket::isOpen() const
{
   return req && req->websock && req->ws_parser != nullptr;
}

//***************************************************************************
} // namespace cex

#endif // EVHTP_WS_SUPPORT
