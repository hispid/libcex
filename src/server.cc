//*************************************************************************
// File server.cc
// Date 24.04.2018 - #1
// Copyright (c) 2018-2018 by Patrick Fial
//-------------------------------------------------------------------------
// cex Library core definitions
//*************************************************************************

//***************************************************************************
// includes
//***************************************************************************

#include <cex/core.hpp>
#include <cex/ssl.hpp>
#include <cex/util.hpp>
#include <utility>
#include <cstring>
#ifdef EVHTP_WS_SUPPORT
extern "C" {
#include <evhtp/ws/evhtp_ws.h>
}
#endif
#ifdef CEX_WITH_SSL
#include <evhtp/sslutils.h>
#endif

namespace cex
{

//***************************************************************************
// class Server
//***************************************************************************
// statics
//***************************************************************************

bool Server::initialized= false;
std::mutex Server::initMutex;
std::unique_ptr<MimeTypes> Server::mimeTypes(new MimeTypes);

const char* getLibraryVersion()
{
   return CEX_VERSION;
}

//***************************************************************************
// ctor/dtor
//***************************************************************************

Server::Server(Config& config)
   : serverConfig(config)
{
   libraryInit();

   startSignaled= started= false;
}

Server::Server() 
{ 
   libraryInit();
   startSignaled= started= false;
}

Server::~Server()
{
}

//***************************************************************************
// init
//***************************************************************************

void Server::libraryInit()
{
   initMutex.lock();

   if (!initialized)
   {
      // must be called ONCE before all other libevent calls (!)
      // otherwise threading/locking will fail/cause issues.

      evthread_use_pthreads();

      // mime type map, init once.

      initMimeTypes();
      initialized= true;
   }

   initMutex.unlock();
}

//***************************************************************************
// listen
//***************************************************************************

int Server::listen(bool block)
{
   if (!serverConfig.address.length() || serverConfig.port == na)
      return fail;

   return start(block);
}

int Server::listen(std::string aAddress, int aPort, bool block) 
{ 
   serverConfig.address= std::move(aAddress);
   serverConfig.port= aPort;

   return listen(block);
}

//***************************************************************************
// start
//***************************************************************************

int Server::start(bool block)
{
   // create eventloop & http server
   // in non-blocking mode, start in separate background thread
   // this is NOT the thread for the request handlers. request handlers will
   // have separate worker threads ANYWAY as supplied by the 'threadCount' parameter
   // && set by evhtp_use_threads() function

   if (started)
      throw std::runtime_error("Server already started");

   std::runtime_error err("");

   auto startFunc= [this, &block, &err]()
   {
      if (!block)
         startMutex.lock();

      eventBase= EventBasePtr(event_base_new(), &event_base_free);
      std::unique_ptr<evhtp_t, decltype(&evhtp_free)> httpServer(evhtp_new(eventBase ? eventBase.get() : nullptr, nullptr), &evhtp_free);

      if (!eventBase)
      {
         err= std::runtime_error("Failed to create new base_event.");
         return;
      }

      if (!httpServer)
         throw std::runtime_error("Failed to create new evhtp.");

#ifdef CEX_WITH_SSL
      if (serverConfig.sslEnabled)
      {
         if (serverConfig.sslVerifyMode) 
         {
            serverConfig.sslConfig->verify_peer= serverConfig.sslVerifyMode;
            serverConfig.sslConfig->x509_verify_cb = Server::verifyCert;
         }

         evhtp_ssl_init(httpServer.get(), serverConfig.sslConfig);
      }
#endif

      // attach static request callback function, init number of threads, bind socket
      // DON'T use evhtp_set_gencb, because we need the return-cb to attach the
      // evhtp_hook_on_headers callback function HERE.
      //
      // IMPORTANT: WebSocket handlers MUST be registered BEFORE the general ""
      // handler. In libevhtp_ws, evhtp_set_cb("", ...) creates a callback with
      // len=0, and strncmp(path, "", 0)==0 matches ANY path, so "" would
      // intercept /ws requests if registered first.

#ifdef EVHTP_WS_SUPPORT
      for (auto& handler : websocketHandlers)
      {
         std::string wsPath = "ws:";
         if (handler->path.empty())
            wsPath += "/";
         else
            wsPath += handler->path;
         evhtp_set_cb(httpServer.get(), wsPath.c_str(), Server::handleWebSocketRequest, handler.get());
      }
#endif

      auto cb= evhtp_set_cb(httpServer.get(), "", Server::handleRequest, this);
      evhtp_callback_set_hook(cb, evhtp_hook_on_headers, (evhtp_hook)Server::handleHeaders, this);

      evhtp_bind_socket(httpServer.get(), serverConfig.address.c_str(), serverConfig.port, 128);

      // function 'evhtp_use_threads' is marked deprecated, but according to libevhtp source
      // the function which should be used now (evhtp_use_threads_wexit) will be renamed to evhtp_use_threads at some point o_O
      
      if (serverConfig.threadCount > 1 && initialized)
         evhtp_use_threads_wexit(httpServer.get(), nullptr, nullptr, serverConfig.threadCount, nullptr);
//         evhtp_use_threads(httpServer.get(), NULL, serverConfig.threadCount, NULL);

      started= true;

      if (!block)
      {
         startSignaled= true;
         startCond.notify_one();
         startMutex.unlock();
      }

      // this BLOCKS the current thread

      event_base_loop(eventBase.get(), 0);

      // when done, properly unbind httpSever. will be freed by unique_ptr
      // event_base will be free'd by stop()

      evhtp_unbind_sockets(httpServer.get());
   };

   // execute the main start function from main/calling thread ...
   // (forced if library was not setup for threading)

   if (block || !Server::initialized)
   {
      startFunc();
      return success;
   }

   // ... OR from background thread

   std::unique_lock<std::mutex> lock(startMutex);

   backgroundThread= ThreadPtr(new std::thread(startFunc), [this](std::thread *t) 
   { 
      // safe to call from different thread context as long as we have used
      // evthread_use_pthreads() (which is the case in Server::init())

      event_base_loopexit(eventBase.get(), nullptr);
      t->join(); 
      delete t; 
   });

   // wait until thread actually up & running (wait cond unlocks the mutex)

   while (!startSignaled)
      startCond.wait(lock);

   return success;
}

//***************************************************************************
// stop
//***************************************************************************

int Server::stop()
{
   if (!eventBase || !started)
      return done;

   // delete (stop) thread or just break out of the event loop

   if (backgroundThread)
      backgroundThread.reset();
   else
      event_base_loopexit(eventBase.get(), nullptr);

   started= startSignaled= false;

   return done;
}

//***************************************************************************
// use (general middleware)
//***************************************************************************

void Server::use(const MiddlewareFunction& func)
{
   use(nullptr, func);
}

//***************************************************************************
// use (routing middleware)
//***************************************************************************

void Server::use(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, na, flags));
}

//***************************************************************************
// use (method variants)
//***************************************************************************

void Server::get(const MiddlewareFunction& func)
{
   get(nullptr, func);
}

void Server::get(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_GET, flags));
}

void Server::put(const MiddlewareFunction& func)
{
   put(nullptr, func);
}

void Server::put(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_PUT, flags));
}

void Server::post(const MiddlewareFunction& func)
{
   post(nullptr, func);
}

void Server::post(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_POST, flags));
}

void Server::head(const MiddlewareFunction& func)
{
   head(nullptr, func);
}

void Server::head(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_HEAD, flags));
}

void Server::del(const MiddlewareFunction& func)
{
   del(nullptr, func);
}

void Server::del(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_DELETE, flags));
}

void Server::connect(const MiddlewareFunction& func)
{
   connect(nullptr, func);
}

void Server::connect(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_CONNECT, flags));
}

void Server::options(const MiddlewareFunction& func)
{
   options(nullptr, func);
}

void Server::options(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_OPTIONS, flags));
}

void Server::trace(const MiddlewareFunction& func)
{
   trace(nullptr, func);
}

void Server::trace(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_TRACE, flags));
}

void Server::patch(const MiddlewareFunction& func)
{
   patch(nullptr, func);
}

void Server::patch(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_PATCH, flags));
}

void Server::mkcol(const MiddlewareFunction& func)
{
   mkcol(nullptr, func);
}

void Server::mkcol(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_MKCOL, flags));
}

void Server::copy(const MiddlewareFunction& func)
{
   copy(nullptr, func);
}

void Server::copy(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_COPY, flags));
}

void Server::move(const MiddlewareFunction& func)
{
   move(nullptr, func);
}

void Server::move(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_MOVE, flags));
}

void Server::propfind(const MiddlewareFunction& func)
{
   propfind(nullptr, func);
}

void Server::propfind(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_PROPFIND, flags));
}

void Server::proppatch(const MiddlewareFunction& func)
{
   proppatch(nullptr, func);
}

void Server::proppatch(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_PROPPATCH, flags));
}

void Server::lock(const MiddlewareFunction& func)
{
   lock(nullptr, func);
}

void Server::lock(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_LOCK, flags));
}

void Server::unlock(const MiddlewareFunction& func)
{
   unlock(nullptr, func);
}

void Server::unlock(const char* path, const MiddlewareFunction& func, int flags)
{
   middleWares.emplace_back(new Middleware(path, func, htp_method_UNLOCK, flags));
}

// upload hooks to catch file uploads w/ streaming

void Server::uploads(const UploadFunction& func)
{
   uploads(nullptr, func);
}

void Server::uploads(const char* path, const UploadFunction& func, Method method, int flags)
{
   int m= htp_method_POST;

   switch (method)
   {
      case methodGET:       m= htp_method_GET; break;
      case methodHEAD:      m= htp_method_HEAD; break;
      case methodPOST:      m= htp_method_POST; break;
      case methodPUT:       m= htp_method_PUT; break;
      case methodDELETE:    m= htp_method_DELETE; break;
      case methodOPTIONS:   m= htp_method_OPTIONS; break;
      case methodTRACE:     m= htp_method_TRACE; break;
      case methodCONNECT:   m= htp_method_CONNECT; break;
      case methodPATCH:     m= htp_method_PATCH; break;
      case methodMKCOL:     m= htp_method_MKCOL; break;
      case methodCOPY:      m= htp_method_COPY; break;
      case methodMOVE:      m= htp_method_MOVE; break;
      case methodPROPFIND:  m= htp_method_PROPFIND; break;
      case methodPROPPATCH: m= htp_method_PROPPATCH; break;
      case methodLOCK:      m= htp_method_LOCK; break;
      case methodUNLOCK:    m= htp_method_UNLOCK; break;
      default:
         break;
   }

   uploadWares.emplace_back(new Middleware(path, func, m, flags));
}

#ifdef EVHTP_WS_SUPPORT
//***************************************************************************
// websocket
//***************************************************************************

void Server::websocket(const char* path, 
                       const WebSocketOpenFunction& onOpen,
                       const WebSocketMessageFunction& onMessage,
                       const WebSocketCloseFunction& onClose,
                       const WebSocketErrorFunction& onError,
                       int flags)
{
   websocketHandlers.emplace_back(new WebSocketHandler(path, onOpen, onMessage, onClose, onError, flags));
}

//***************************************************************************
// handleWebSocketRequest
//***************************************************************************

void Server::handleWebSocketRequest(evhtp_request* req, void* arg)
{
   auto handler= reinterpret_cast<WebSocketHandler*>(arg);
   
   if (!handler || !req)
      return;

   // Check if this is the initial connection (handshake already done by libevhtp_ws)
   if (!req->websock)
   {
      // This shouldn't happen if registered with "ws:" prefix
      evhtp_send_reply(req, 400);
      return;
   }

   // Check if we have data in the buffer (message received)
   size_t bufLen = evbuffer_get_length(req->buffer_in);
   
   if (bufLen == 0)
   {
      // Initial connection - call onOpen
      if (handler->onOpen)
      {
         WebSocket ws(req);
         handler->onOpen(ws);
      }
      return;
   }

   // Message received - call onMessage
   if (handler->onMessage && bufLen > 0)
   {
      // Get the data from buffer
      const char* data = (const char*)evbuffer_pullup(req->buffer_in, -1);
      
      // Convert opcode to FrameType
      WebSocket::FrameType frameType = WebSocket::wsText;
      switch (req->ws_opcode)
      {
         case OP_TEXT:  frameType = WebSocket::wsText; break;
         case OP_BIN:   frameType = WebSocket::wsBinary; break;
         case OP_PING:  frameType = WebSocket::wsPing; break;
         case OP_PONG:  frameType = WebSocket::wsPong; break;
         case OP_CLOSE: frameType = WebSocket::wsClose; break;
         default:       frameType = WebSocket::wsText; break;
      }

      WebSocket ws(req);
      handler->onMessage(ws, data, bufLen, frameType);
   }

   // Check if connection should be closed
   if (req->disconnect)
   {
      if (handler->onClose)
      {
         WebSocket ws(req);
         handler->onClose(ws);
      }
   }
}
#endif
 
//***************************************************************************
// handle headers (step 1)
//***************************************************************************

evhtp_res Server::handleHeaders(evhtp_request_t* request, evhtp_headers_t* hdr, void* arg)
{
   // initially create a context object which will be used throughout the workflow.
   // holds the request, response and server pointers.

   auto serv= reinterpret_cast<Server*>(arg);
   auto ctx= new Server::Context(request, serv);

   // add hooks for body upload & finish of request. 'handleRequest' was already registered
   // in Server::listen

   evhtp_request_set_hook(request, evhtp_hook_on_read, (evhtp_hook)Server::handleBody, ctx); 
   evhtp_request_set_hook(request, evhtp_hook_on_request_fini, (evhtp_hook)Server::handleFinished, ctx); 

   return EVHTP_RES_OK;
}

//***************************************************************************
// handle upload (step 2)
//***************************************************************************

evhtp_res Server::handleBody(evhtp_request_t* req, struct evbuffer* buf, void* arg)
{
   auto ctx= reinterpret_cast<Server::Context*>(arg);
   std::vector<char>* body= &(ctx->req->body);

   if (!body)                 // should never happen
      return EVHTP_RES_OK;

   size_t bytesReady= evbuffer_get_length(buf);
   size_t oldSize= body->size();

   // (1) check if we have attached upload middleware(s)

   if (!ctx->serv->uploadWares.empty())
   {
      auto it= ctx->serv->uploadWares.begin();

      while (it != ctx->serv->uploadWares.end())
      {
         if (!((*it)->match(ctx->req.get())))
         {
            ++it;
            continue;
         }

         // we found a matching upload ware. copy bytes into body buffer (CURRENT CHUNK ONLY)
         // and call the upload middleware function.

         body->resize(bytesReady);

         if (body->size() < bytesReady)
         {
            // allocation error

            return EVHTP_RES_500;
         }

         ev_ssize_t bytesCopied= evbuffer_copyout(buf, (void*)(body->data()), bytesReady);


         ctx->req->middlewarePath= (*it)->getPath();
         (*it)->uploadFunc(ctx->req.get(), body->data(), bytesCopied);

         return EVHTP_RES_OK;
      }
   }

   // (2) no upload middleware attached, or none matched. just copy bytes into (full) body buffer

   body->resize(bytesReady + oldSize);

   if (body->size() < (bytesReady + oldSize))
   {
      // allocation error

      return EVHTP_RES_500;
   }

   evbuffer_copyout(buf, (void*)(body->data() + oldSize), bytesReady);

   // drain, so libevhtp won't copy it into the native request's internal buffer (req->buffer_in)

   evbuffer_drain(buf, evbuffer_get_length(buf));

   return EVHTP_RES_OK;
}
 
//***************************************************************************
// handle upload (step 3)
//***************************************************************************

void Server::handleRequest(evhtp_request* req, void* arg)
{
   // actual request handling & middlewares as soon as we have headers + body ready.
   // the context is stored within the req as the cb-argument for the evhtp_hook_on_request_fini-hook.

   auto serv= reinterpret_cast<Server*>(arg);
   Server::Context* ctx= req && req->hooks ? (Server::Context*)req->hooks->on_request_fini_arg : nullptr;

   if (!ctx)
   {
      // should NEVER happen

      evhtp_send_reply(req, 500);
      return;
   }

   // retrieve SSL client info (certificate), if available & configured

#ifdef CEX_WITH_SSL
   if (ctx->serv->serverConfig.parseSslInfo)
      ctx->serv->getSslClientInfo(ctx->req.get());
#endif

   // enable compression, if available & configured

#ifdef CEX_WITH_ZLIB
   if (ctx->serv->serverConfig.compress)
   {
      const char* acceptEncoding= ctx->req.get()->get("Accept-Encoding");

      if (acceptEncoding && strstr(acceptEncoding, "gzip"))
         ctx->res.get()->setFlags(ctx->res.get()->getFlags() | Response::fCompressGZip);
      else if (acceptEncoding && strstr(acceptEncoding, "deflate"))
         ctx->res.get()->setFlags(ctx->res.get()->getFlags() | Response::fCompressDeflate);
   }
#endif

   // call all registered handlers (route-based and general middlewares)

   auto it= ctx->serv->middleWares.begin();

   if (it == ctx->serv->middleWares.end())
   {
      ctx->res.get()->end(404);
      return;
   }

   std::function<void()> next;

   // create next-function to be handed to each middleware

   next = [&next, &ctx, &it]()
   {
      ++it;

      if (it != ctx->serv->middleWares.end())
      {
         if ((*it)->match(ctx->req.get()))
         {
            ctx->req.get()->middlewarePath= (*it)->getPath();
            (*it)->func(ctx->req.get(), ctx->res.get(), next);
         }
         else
            next();
      }
   };

   // call next handler OR next-function, if next handler doesn't match.
   // if no middleware matched, the request will hang (thats intended).

   if ((*it)->match(ctx->req.get()))
   {
      ctx->req.get()->middlewarePath= (*it)->getPath();
      (*it)->func(ctx->req.get(), ctx->res.get(), next);
   }
   else
      next();
}

//***************************************************************************
// handle finished (step 4)
//***************************************************************************

evhtp_res Server::handleFinished(evhtp_request_t* req, void* arg)
{
   // forget the request context we created

   auto ctx= reinterpret_cast<Server::Context*>(arg);

   delete ctx;
   
   return EVHTP_RES_OK;
}

//***************************************************************************
// class Server::Config
//***************************************************************************
// ctor
//***************************************************************************

Server::Config::Config() 
{ 
   port= na;
   compress= true; 
   parseSslInfo= true; 
   sslEnabled= false;
   threadCount= 4; 

#ifdef CEX_WITH_SSL
   sslVerifyMode= 0;
   sslConfig= (evhtp_ssl_cfg_t*)calloc(1, sizeof(evhtp_ssl_cfg_t));
   sslConfig->ssl_opts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1;
   sslConfig->scache_type= evhtp_ssl_scache_type_disabled;
   sslConfig->scache_size= 0;
   sslConfig->scache_timeout= 0;
#endif
}

Server::Config::Config(Config& other)
{
   port= na;
   compress= other.compress;
   parseSslInfo= other.parseSslInfo;
   sslEnabled= other.sslEnabled;
   threadCount= other.threadCount;

#ifdef CEX_WITH_SSL
   sslVerifyMode= other.sslVerifyMode;
   sslConfig= (evhtp_ssl_cfg_t*)calloc(1, sizeof(evhtp_ssl_cfg_t));
   memcpy(sslConfig, other.sslConfig, sizeof(evhtp_ssl_cfg_t));
#endif
}

Server::Config::~Config()
{
#ifdef CEX_WITH_SSL
   ::free(sslConfig->pemfile);
   ::free(sslConfig->privfile);
   ::free(sslConfig->cafile);
   ::free(sslConfig->capath);
   ::free(sslConfig->ciphers);
   ::free(sslConfig->dhparams);
   ::free(sslConfig->named_curve);
   ::free(sslConfig->ciphers);     
   ::free(sslConfig);
#endif
}

//***************************************************************************
} // namespace cex

