/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include "io.h"
#include "transport_tcp.h"
#include "poll_set.h"
#include "header.h"
#include "file_log.h"
#include <ros/assert.h>
#include <sstream>
#include <boost/bind.hpp>
#include <fcntl.h>
#include <errno.h>
namespace ros2
{

bool TransportTCP::s_use_keepalive_ = true;
bool TransportTCP::s_use_ipv6_ = false;

TransportTCP::TransportTCP(PollSet* poll_set, int flags)
: sock_(ROS_INVALID_SOCKET)
, closed_(false)
, expecting_read_(false)
, expecting_write_(false)
, is_server_(false)
, server_port_(-1)
, poll_set_(poll_set)
, flags_(flags)
, messages_(false)
, reading_(false)
, send_msgs_(0)
, msg_counter_(0)
, filter_(1)
{

}

TransportTCP::~TransportTCP()
{
  ROS_ASSERT_MSG(sock_ == -1, "TransportTCP socket [%d] was never closed", sock_);
}

bool TransportTCP::setSocket(int sock)
{
  sock_ = sock;
  return initializeSocket();
}

bool TransportTCP::setNonBlocking()
{
  if (!(flags_ & SYNCHRONOUS))
  {
	  int result = set_non_blocking(sock_);
	  if ( result != 0 ) {
	      ROS_ERROR("setting socket [%d] as non_blocking failed with error [%d]", sock_, result);
      close();
      return false;
    }
  }

  return true;
}

bool TransportTCP::setReuse()
{
  int flag = 1;
  int error;
  if ((error = setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag))) != 0)
  {
    ROS_ERROR("setting SO_REUSEADDR on socket [%d] failed with error [%d]", sock_, error);
    return false;
  }
  else
  {
    ROS_DEBUG("Set SO_REUSEADDR");
  }
  return true;
}

bool TransportTCP::initializeSocket()
{
  ROS_ASSERT(sock_ != ROS_INVALID_SOCKET);

  if (!setNonBlocking())
  {
    return false;
  }

  setKeepAlive(s_use_keepalive_, 60, 10, 9);

  // connect() will set cached_remote_host_ because it already has the host/port available
  if (cached_remote_host_.empty())
  {
    if (is_server_)
    {
      cached_remote_host_ = "TCPServer Socket";
    }
    else
    {
      std::stringstream ss;
      ss << getClientURI() << " on socket " << sock_;
      cached_remote_host_ = ss.str();
    }
  }

#ifdef ROSCPP_USE_TCP_NODELAY
  setNoDelay(true);
#endif

  ROS_ASSERT(poll_set_ || (flags_ & SYNCHRONOUS));
  if (poll_set_)
  {
    ROS_DEBUG("Adding tcp socket [%d] to pollset", sock_);
    poll_set_->addSocket(sock_, boost::bind(&TransportTCP::socketUpdate, this, _1), shared_from_this());
  }

  if (!(flags_ & SYNCHRONOUS))
  {
    //enableRead();
  }

  return true;
}

void TransportTCP::parseHeader(const Header& header)
{
  std::string nodelay;
  if (header.getValue("tcp_nodelay", nodelay) && nodelay == "1")
  {
    ROSCPP_LOG_DEBUG("Setting nodelay on socket [%d]", sock_);
    setNoDelay(true);
  }
}

void TransportTCP::setNoDelay(bool nodelay)
{
  int flag = nodelay ? 1 : 0;
  int result = setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
  if (result < 0)
  {
    ROS_ERROR("setsockopt failed to set TCP_NODELAY on socket [%d] [%s]", sock_, cached_remote_host_.c_str());
  }
}

void TransportTCP::setKeepAlive(bool use, uint32_t idle, uint32_t interval, uint32_t count)
{
  if (use)
  {
    int val = 1;
    if (setsockopt(sock_, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
    {
      ROS_DEBUG("setsockopt failed to set SO_KEEPALIVE on socket [%d] [%s]", sock_, cached_remote_host_.c_str());
    }

/* cygwin SOL_TCP does not seem to support TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT */
#if defined(SOL_TCP) && !defined(__CYGWIN__)
    val = idle;
    if (setsockopt(sock_, SOL_TCP, TCP_KEEPIDLE, &val, sizeof(val)) != 0)
    {
      ROS_DEBUG("setsockopt failed to set TCP_KEEPIDLE on socket [%d] [%s]", sock_, cached_remote_host_.c_str());
    }

    val = interval;
    if (setsockopt(sock_, SOL_TCP, TCP_KEEPINTVL, &val, sizeof(val)) != 0)
    {
      ROS_DEBUG("setsockopt failed to set TCP_KEEPINTVL on socket [%d] [%s]", sock_, cached_remote_host_.c_str());
    }

    val = count;
    if (setsockopt(sock_, SOL_TCP, TCP_KEEPCNT, &val, sizeof(val)) != 0)
    {
      ROS_DEBUG("setsockopt failed to set TCP_KEEPCNT on socket [%d] [%s]", sock_, cached_remote_host_.c_str());
    }
#endif
  }
  else
  {
    int val = 0;
    if (setsockopt(sock_, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
    {
    	ROS_DEBUG("setsockopt failed to set SO_KEEPALIVE on socket [%d] [%s]", sock_, cached_remote_host_.c_str());
    }
  }
}

bool TransportTCP::connect(const std::string& host, int port)
{
  sock_ = socket(s_use_ipv6_ ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  connected_host_ = host;
  connected_port_ = port;

  if (sock_ == ROS_INVALID_SOCKET)
  {
    ROS_ERROR("socket() failed with error [%s]",  last_socket_error_string());
    return false;
  }

  setNonBlocking();

  sockaddr_storage sas;
  socklen_t sas_len;

  in_addr ina;
  in6_addr in6a;
  if (inet_pton(AF_INET, host.c_str(), &ina) == 1)
  {
    sockaddr_in *address = (sockaddr_in*) &sas;
    sas_len = sizeof(sockaddr_in);

    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    address->sin_addr.s_addr = ina.s_addr;
  }
  else if (inet_pton(AF_INET6, host.c_str(), &in6a) == 1)
  {
    sockaddr_in6 *address = (sockaddr_in6*) &sas;
    sas_len = sizeof(sockaddr_in6);
    address->sin6_family = AF_INET6;
    address->sin6_port = htons(port);
    memcpy(address->sin6_addr.s6_addr, in6a.s6_addr, sizeof(in6a.s6_addr));
  }
  else
  {
    struct addrinfo* addr;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;

    if (getaddrinfo(host.c_str(), NULL, &hints, &addr) != 0)
    {
      close();
      ROS_ERROR("couldn't resolve publisher host [%s]", host.c_str());
      return false;
    }

    bool found = false;
    struct addrinfo* it = addr;
    char namebuf[128];
    for (; it; it = it->ai_next)
    {
      if (!s_use_ipv6_ && it->ai_family == AF_INET)
      {
        sockaddr_in *address = (sockaddr_in*) &sas;
        sas_len = sizeof(*address);

        memcpy(address, it->ai_addr, it->ai_addrlen);
        address->sin_family = it->ai_family;
        address->sin_port = htons(port);

        strcpy(namebuf, inet_ntoa(address->sin_addr));
        found = true;
        break;
      }
      if (s_use_ipv6_ && it->ai_family == AF_INET6)
      {
        sockaddr_in6 *address = (sockaddr_in6*) &sas;
        sas_len = sizeof(*address);

        memcpy(address, it->ai_addr, it->ai_addrlen);
        address->sin6_family = it->ai_family;
        address->sin6_port = htons((u_short) port);

        // TODO IPV6: does inet_ntop need other includes for Windows?
        inet_ntop(AF_INET6, (void*)&(address->sin6_addr), namebuf, sizeof(namebuf));
        found = true;
        break;
      }
    }

    freeaddrinfo(addr);

    if (!found)
    {
      ROS_ERROR("Couldn't resolve an address for [%s]\n", host.c_str());
      return false;
    }

    ROSCPP_LOG_DEBUG("Resolved publisher host [%s] to [%s] for socket [%d]", host.c_str(), namebuf, sock_);
  }

  int ret = ::connect(sock_, (sockaddr*) &sas, sas_len);
  // windows might need some time to sleep (input from service robotics hack) add this if testing proves it is necessary.
  ROS_ASSERT((flags_ & SYNCHRONOUS) || ret != 0);
  if (((flags_ & SYNCHRONOUS) && ret != 0) || // synchronous, connect() should return 0
      (!(flags_ & SYNCHRONOUS) && last_socket_error() != ROS_SOCKETS_ASYNCHRONOUS_CONNECT_RETURN)) // asynchronous, connect() should return -1 and WSAGetLastError()=WSAEWOULDBLOCK/errno=EINPROGRESS
  {
    ROSCPP_LOG_DEBUG("Connect to tcpros publisher [%s:%d] failed with error [%d, %s]", host.c_str(), port, ret, last_socket_error_string());
    close();

    return false;
  }

  // from daniel stonier:
#ifdef WIN32
  // This is hackish, but windows fails at recv() if its slow to connect (e.g. happens with wireless)
  // recv() needs to check if its connected or not when its asynchronous?
  Sleep(100);
#endif


  std::stringstream ss;
  ss << host << ":" << port << " on socket " << sock_;
  cached_remote_host_ = ss.str();

  if (!initializeSocket())
  {
    return false;
  }

  if (flags_ & SYNCHRONOUS)
  {
    ROSCPP_LOG_DEBUG("connect() succeeded to [%s:%d] on socket [%d]", host.c_str(), port, sock_);
  }
  else
  {
    ROSCPP_LOG_DEBUG("Async connect() in progress to [%s:%d] on socket [%d]", host.c_str(), port, sock_);
  }

  return true;
}

bool TransportTCP::listen(int port, int backlog, const AcceptCallback& accept_cb)
{
  is_server_ = true;
  accept_cb_ = accept_cb;

  if (s_use_ipv6_)
  {
    sock_ = socket(AF_INET6, SOCK_STREAM, 0);
    sockaddr_in6 *address = (sockaddr_in6 *)&server_address_;
    address->sin6_family = AF_INET6;
    address->sin6_addr = in6addr_any;
    address->sin6_port = htons(port);
    sa_len_ = sizeof(sockaddr_in6);
  }
  else
  {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in *address = (sockaddr_in *)&server_address_;
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = INADDR_ANY;
    address->sin_port = htons(port);
    sa_len_ = sizeof(sockaddr_in);
  }

  if (sock_ <= 0)
  {
    ROS_ERROR("socket() failed with error [%s]", last_socket_error_string());
    return false;
  }


  if (port != 0)
  {
    setReuse();
  }

  if (bind(sock_, (sockaddr *)&server_address_, sa_len_) < 0)
  {
    ROS_ERROR("bind() failed with error [%s]", last_socket_error_string());
    return false;
  }

  ::listen(sock_, backlog);
  getsockname(sock_, (sockaddr *)&server_address_, &sa_len_);

  switch (server_address_.ss_family)
  {
    case AF_INET:
      server_port_ = ntohs(((sockaddr_in *)&server_address_)->sin_port);
      break;
    case AF_INET6:
      server_port_ = ntohs(((sockaddr_in6 *)&server_address_)->sin6_port);
      break;
  }

  if (!initializeSocket())
  {
    return false;
  }

  if (!(flags_ & SYNCHRONOUS))
  {
    enableRead();
  }

  return true;
}

void TransportTCP::close()
{
  Callback disconnect_cb;

  if (!closed_)
  {
    {
      boost::recursive_mutex::scoped_lock lock(close_mutex_);

      if (!closed_)
      {
        closed_ = true;

        ROS_ASSERT(sock_ != ROS_INVALID_SOCKET);

        if (poll_set_)
        {
          poll_set_->delSocket(sock_);
        }

        ::shutdown(sock_, ROS_SOCKETS_SHUT_RDWR);
        if ( close_socket(sock_) != 0 )
        {
          ROS_ERROR("Error closing socket [%d]: [%s]", sock_, last_socket_error_string());
        } else
        {
          ROSCPP_LOG_DEBUG("TCP socket [%d] closed", sock_);
        }
        sock_ = ROS_INVALID_SOCKET;

        disconnect_cb = disconnect_cb_;

        disconnect_cb_ = Callback();
        read_cb_ = Callback();
        write_cb_ = Callback();
        accept_cb_ = AcceptCallback();
      }
    }
  }

  if (disconnect_cb)
  {
    disconnect_cb(shared_from_this());
  }
}

int32_t TransportTCP::read(uint8_t* buffer, uint32_t size)
{
  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);

    if (closed_)
    {
      ROSCPP_LOG_DEBUG("Tried to read on a closed socket [%d]", sock_);
      return -1;
    }
  }

  ROS_ASSERT(size > 0);

  // never read more than INT_MAX since this is the maximum we can report back with the current return type
  uint32_t read_size = std::min(size, static_cast<uint32_t>(INT_MAX));
  int num_bytes = ::recv(sock_, reinterpret_cast<char*>(buffer), read_size, 0);
  if (num_bytes < 0)
  {
	if ( !last_socket_error_is_would_block() ) // !WSAWOULDBLOCK / !EAGAIN && !EWOULDBLOCK
    {
      ROSCPP_LOG_DEBUG("recv() on socket [%d] failed with error [%s]", sock_, last_socket_error_string());
      close();
    }
    else
    {
      num_bytes = 0;
    }
  }
  else if (num_bytes == 0)
  {
    ROSCPP_LOG_DEBUG("Socket [%d] received 0/%u bytes, closing", sock_, size);
    close();
    return -1;
  }

  return num_bytes;
}

int32_t TransportTCP::write(uint8_t* buffer, uint32_t size)
{
  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);

    if (closed_)
    {
      ROSCPP_LOG_DEBUG("Tried to write on a closed socket [%d]", sock_);
      return -1;
    }
  }

  ROS_ASSERT(size > 0);

  // never write more than INT_MAX since this is the maximum we can report back with the current return type
  uint32_t writesize = std::min(size, static_cast<uint32_t>(INT_MAX));
  int num_bytes = ::send(sock_, reinterpret_cast<const char*>(buffer), writesize, 0);
  if (num_bytes < 0)
  {
    if ( !last_socket_error_is_would_block() )
    {
      ROSCPP_LOG_DEBUG("send() on socket [%d] failed with error [%s]", sock_, last_socket_error_string());
      close();
    }
    else
    {
      num_bytes = 0;
    }
  }

  return num_bytes;
}

void TransportTCP::enableRead()
{
  ROS_ASSERT(!(flags_ & SYNCHRONOUS));

  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);

    if (closed_)
    {
      return;
    }
  }

  if (!expecting_read_)
  {
    poll_set_->addEvents(sock_, POLLIN);
    expecting_read_ = true;
  }
}

void TransportTCP::disableRead()
{
  ROS_ASSERT(!(flags_ & SYNCHRONOUS));

  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);

    if (closed_)
    {
      return;
    }
  }

  if (expecting_read_)
  {
    poll_set_->delEvents(sock_, POLLIN);
    expecting_read_ = false;
  }
}

void TransportTCP::enableWrite()
{
  ROS_ASSERT(!(flags_ & SYNCHRONOUS));

  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);

    if (closed_)
    {
      return;
    }
  }

  if (!expecting_write_)
  {
    poll_set_->addEvents(sock_, POLLOUT);
    expecting_write_ = true;
  }
}

void TransportTCP::disableWrite()
{
  ROS_ASSERT(!(flags_ & SYNCHRONOUS));

  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);

    if (closed_)
    {
      return;
    }
  }

  if (expecting_write_)
  {
    poll_set_->delEvents(sock_, POLLOUT);
    expecting_write_ = false;
  }
}

TransportTCPPtr TransportTCP::accept()
{
  ROS_ASSERT(is_server_);

  sockaddr client_address;
  socklen_t len = sizeof(client_address);
  int new_sock = ::accept(sock_, (sockaddr *)&client_address, &len);
  if (new_sock >= 0)
  {
    ROSCPP_LOG_DEBUG("Accepted connection on socket [%d], new socket [%d]", sock_, new_sock);

    TransportTCPPtr transport(new TransportTCP(poll_set_, flags_));
    if (!transport->setSocket(new_sock))
    {
      ROS_ERROR("Failed to set socket on transport for socket %d", new_sock);
    }

    return transport;
  }
  else
  {
    ROS_ERROR("accept() on socket [%d] failed with error [%s]", sock_,  last_socket_error_string());
  }

  return TransportTCPPtr();
}

void TransportTCP::socketUpdate(int events)
{
  {
    boost::recursive_mutex::scoped_lock lock(close_mutex_);
    if (closed_)
    {
      return;
    }

    // Handle read events before err/hup/nval, since there may be data left on the wire
    if ((events & POLLIN) && expecting_read_)
    {
      if (is_server_)
      {
        // Should not block here, because poll() said that it's ready
        // for reading
        TransportTCPPtr transport = accept();
        if (transport)
        {
          ROS_ASSERT(accept_cb_);
          accept_cb_(transport);
        }
      }
      else
      {
        if (read_cb_)
        {
          read_cb_(shared_from_this());
        }
      }
    }

    if ((events & POLLOUT) && expecting_write_)
    {
      if (write_cb_)
      {
        write_cb_(shared_from_this());
      }
    }
  }

  if((events & POLLERR) ||
     (events & POLLHUP) ||
     (events & POLLNVAL))
  {
    uint32_t error = -1;
    socklen_t len = sizeof(error);
    if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) < 0)
    {
      ROSCPP_LOG_DEBUG("getsockopt failed on socket [%d]", sock_);
    }
  #ifdef _MSC_VER
    char err[60];
    strerror_s(err,60,error);
    ROSCPP_LOG_DEBUG("Socket %d closed with (ERR|HUP|NVAL) events %d: %s", sock_, events, err);
  #else
    ROSCPP_LOG_DEBUG("Socket %d closed with (ERR|HUP|NVAL) events %d: %s", sock_, events, strerror(error));
  #endif

    close();
  }
}

std::string TransportTCP::getTransportInfo()
{
  return "TCPROS connection to [" + cached_remote_host_ + "]";
}

std::string TransportTCP::getClientURI()
{
  ROS_ASSERT(!is_server_);

  sockaddr_storage sas;
  socklen_t sas_len = sizeof(sas);
  getpeername(sock_, (sockaddr *)&sas, &sas_len);

  sockaddr_in *sin = (sockaddr_in *)&sas;
  sockaddr_in6 *sin6 = (sockaddr_in6 *)&sas;

  char namebuf[128];
  int port;

  switch (sas.ss_family)
  {
    case AF_INET:
      port = ntohs(sin->sin_port);
      strcpy(namebuf, inet_ntoa(sin->sin_addr));
      break;
    case AF_INET6:
      port = ntohs(sin6->sin6_port);
      inet_ntop(AF_INET6, (void*)&(sin6->sin6_addr), namebuf, sizeof(namebuf));
      break;
    default:
      namebuf[0] = 0;
      port = 0;
      break;
  }

  std::string ip = namebuf;
  std::stringstream uri;
  uri << ip << ":" << port;

  return uri.str();
}

void TransportTCP::enableMessagePass()
{
  boost::recursive_mutex::scoped_lock lock(message_mutex_);
  messages_ = true;
  setReadCallback(boost::bind(&TransportTCP::readMessage, this, _1));
  setWriteCallback(boost::bind(&TransportTCP::writeMessage, this, _1));
}

void TransportTCP::setMsgCallback(const ReadMessageCallback &cb)
{
  boost::recursive_mutex::scoped_lock lock(message_mutex_);
  readmsg_cb_ = cb;
}

void TransportTCP::writeMessage(const TransportPtr &trans)
{
  boost::recursive_mutex::scoped_lock lock(message_mutex_);
  if (send_msgs_.size() == 0)
  {
    ROS_ERROR("Asked to write message, but no messages left");
    ROS_BREAK();
  }

  boost::shared_ptr<Message> msg = send_msgs_.front();
  int wrote;
  if (msg->numframes_filled < 4)
  {
    // Send number of frames
    ROS_DEBUG("Sending #frames = %i", msg->numframes);
    uint8_t *sz = (uint8_t*)&msg->numframes;
    wrote = write(sz + msg->numframes_filled, 4 - msg->numframes_filled);
    if (wrote >= 0)
    {
      msg->numframes_filled += wrote;
    }
    else
    {
      close();
      return;
    }
  }
  else
  {
    writeFrame(msg);
  }
}

void TransportTCP::writeFrame(boost::shared_ptr<Message> msg)
{
  Frame &frame = msg->frames.at(msg->frame_ind);
  if (msg->frame_sz_filled < 4)
  {
    // send frame size
    ROS_DEBUG("Sending frame %i size = %i", msg->frame_ind, msg->frame_sz);
    uint8_t *sz = (uint8_t*)&msg->frame_sz;
    uint32_t left = 4 - msg->frame_sz_filled;
    int32_t wrote = write(sz + msg->frame_sz_filled, left);
    if (wrote >= 0)
    {
      msg->frame_sz_filled += wrote;
    }
    else
    {
      close();
      return;
    }
  }
  else
  {
    // Send frame payload
    ROS_DEBUG("Sending frame %i payload %i/%i sent",
             msg->frame_ind, msg->frame_filled, frame.size);
    uint32_t left = frame.size - msg->frame_filled;
    if (left > 0)
    {
      uint32_t wrote = write(frame.data.get() + msg->frame_filled, left);
      if (wrote >= 0)
      {
        ROS_DEBUG("Wrote %i", wrote);
        msg->frame_filled += wrote;
      }
      else
      {
        close();
        return;
      }
    }
  }
  // Check if frame and message is done
  if (msg->frame_filled == frame.size)
  {
    ROS_DEBUG("Finished frame %i", msg->frame_ind);

    msg->frame_ind++;;
    if (msg->frame_ind < msg->frames.size())
    {
      msg->frame_filled = 0;
      msg->frame_sz = msg->frames.at(msg->frame_ind).size;
      msg->frame_sz_filled = 0;
    }
    else
    {
      ROS_DEBUG("Finished sending message");
      send_msgs_.pop_front();
      if (send_msgs_.size() == 0)
      {
        disableWrite();
      }
    }
  }
}

void TransportTCP::readMessage(const TransportPtr &trans)
{
  boost::recursive_mutex::scoped_lock lock(message_mutex_);
  if (!messages_)
  {
    ROS_ERROR("readMessage() called, but enableMessagePass() was not");
    ROS_BREAK();
  }

  if (!reading_)
  {
    ROS_DEBUG("Starting to read a message");
    read_msg_.frames.resize(0);
    read_msg_.numframes_filled = 0;
    read_msg_.frame_ind = 0;
    read_msg_.frame_filled = 0;
    read_msg_.frame_sz_filled = 0;
    reading_ = true;
  }

  // Read message length
  if (read_msg_.numframes_filled < 4)
  {
    ROS_DEBUG("Reading number of frames");

    uint32_t to_read = 4 - read_msg_.numframes_filled;
    int32_t read_sz = read(((uint8_t*)&read_msg_.numframes) + read_msg_.numframes_filled,
                           to_read);
    if (read_sz < 0)
    {
      return;
    }
    read_msg_.numframes_filled += read_sz;
    if (read_msg_.numframes_filled == 4)
    {
      ROS_DEBUG("There are %i frames", read_msg_.numframes);
      if (read_msg_.numframes == 0)
      {
        ROS_WARN("Message with 0 frames; closing connection");
        close();
      }
      read_msg_.frames.resize(read_msg_.numframes);
    }
  }
  else
  {
    readFrame();
  }
}

void TransportTCP::readFrame()
{
  Frame &frame = read_msg_.frames.at(read_msg_.frame_ind);
  if (read_msg_.frame_sz_filled < 4)
  {
    // Read frame size
    ROS_DEBUG("Reading frame %i size", read_msg_.frame_ind);
    uint32_t to_read = 4 - read_msg_.frame_sz_filled;
    int32_t read_sz = read(((uint8_t*)&frame.size) + read_msg_.frame_sz_filled,
                            to_read);
    if (read_sz < 0)
    {
      return;
    }
    read_msg_.frame_sz_filled += read_sz;
    if (read_msg_.frame_sz_filled == 4)
    {
      ROS_DEBUG("Frame %i has %i bytes", read_msg_.frame_ind, frame.size);
      frame.data.reset(new uint8_t[frame.size]);
    }
  }
  else
  {
    // Read frame
    uint32_t to_read = frame.size - read_msg_.frame_filled;
    if (to_read > 0)
    {
      int32_t read_sz = read(frame.data.get() + read_msg_.frame_filled,
                             to_read);
      if (read_sz < 0)
      {
        return;
      }
      read_msg_.frame_filled += read_sz;
      ROS_DEBUG("Got %i/%i bytes for frame %i",
                read_msg_.frame_filled, frame.size, read_msg_.frame_ind);
    }
  }

  if (read_msg_.frame_filled == frame.size)
  {
    read_msg_.frame_ind++;
    read_msg_.frame_filled = 0;
    read_msg_.frame_sz_filled = 0;

    if (read_msg_.frame_ind == read_msg_.frames.size())
    {
      readmsg_cb_(read_msg_.frames);
      reading_ = false;
    }
  }

}

void TransportTCP::sendFrames(const std::vector<Frame> &frames)
{
  boost::recursive_mutex::scoped_lock lock(message_mutex_);
  if (!messages_)
  {
    ROS_ERROR("sendFrames() called, but enableMessagePass() was not");
    ROS_BREAK();
  }

  msg_counter_++;
  msg_counter_ = msg_counter_ % filter_;
  if (msg_counter_ != 0)
  {
    return;
  }

  boost::shared_ptr<Message> msg(new Message);
  msg->frames = frames;
  msg->numframes = frames.size();
  msg->numframes_filled = 0;
  msg->frame_ind = 0;
  msg->frame_filled = 0;
  msg->frame_sz = msg->frames.at(0).size;
  msg->frame_sz_filled = 0;

  if (send_msgs_.empty())
  {
    enableWrite();
  }
  send_msgs_.push_back(msg);
}

void TransportTCP::sendMessage(const boost::shared_array<uint8_t> &buffer, uint32_t size)
{
  Frame f;
  f.data = buffer;
  f.size = size;
  std::vector<Frame> frames;
  frames.push_back(f);
  sendFrames(frames);
}

void TransportTCP::setFilter(int filter_num)
{
  assert(filter_num > 0);
  filter_ = filter_num;
  msg_counter_ = filter_num - 1;
}

} // namespace ros2
