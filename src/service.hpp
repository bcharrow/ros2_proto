#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "transport_tcp.h"

#include <boost/thread/condition_variable.hpp>
#include <boost/bind.hpp>

#include <ros/serialization.h>

namespace ros2 {

template <typename M>
class ServiceManager {
public:
  typedef typename M::Request Request;
  typedef typename M::Response Response;

  typedef boost::function<void(const Request&, Response*)> Callback;

  ServiceManager(TransportTCPPtr server) : server_(server) {

  }

  ~ServiceManager() {
    server_->close();
  }

  const TransportTCP& socket() {
    return *server_;
  }

  void init(int port) {
    if (!server_->listen(port, 5, boost::bind(&ServiceManager::onAccept, this, _1))) {
      ROS_ERROR("Failed to listen");
      ROS_BREAK();
    }
  }

  void bind(const Callback &cb) {
    boost::mutex::scoped_lock lock(mutex_);
    cb_ = cb;
  }

  void onAccept(const TransportTCPPtr &tcp) {
    boost::mutex::scoped_lock lock(mutex_);
    ROS_INFO("Got a connection from %s", tcp->getClientURI().c_str());
    tcp->enableMessagePass();
    tcp->enableRead();
    tcp->setMsgCallback(boost::bind(&ServiceManager::onReceive, this, tcp, _1));
    tcp->setDisconnectCallback(boost::bind(&ServiceManager::onDisconnect, this, _1));
    connections_.push_back(tcp);
  }

  void onReceive(const TransportTCPPtr &tcp,
                 const std::vector<Frame> &frames) {
    if (frames.size() != 1) {
      ROS_ERROR("Got %zu frames; expected 1", frames.size());
      ROS_BREAK();
    }
    const boost::shared_array<uint8_t> &bytes = frames[0].data;
    uint32_t sz = frames[0].size;

    Request req;
    Response rep;

    ros::serialization::IStream istream(bytes.get(), sz);
    ros::serialization::Serializer<Request>::read(istream, req);

    {
      boost::mutex::scoped_lock lock(mutex_);
      cb_(req, &rep);
    }
    uint32_t rep_sz = ros::serialization::serializationLength(rep);
    boost::shared_array<uint8_t> rep_bytes(new uint8_t[rep_sz]);
    ros::serialization::OStream ostream(rep_bytes.get(), rep_sz);
    ros::serialization::serialize(ostream, rep);
    ROS_INFO_STREAM("Sending response: " << rep);
    tcp->sendMessage(rep_bytes, rep_sz);
  }

  void onDisconnect(const TransportPtr &tcp) {
    boost::mutex::scoped_lock lock(mutex_);

    for (std::list<TransportTCPPtr>::iterator it = connections_.begin();
         it != connections_.end(); ++it) {
      if (*it == tcp) {
        connections_.erase(it);
        return;
      }
    }
    ROS_ERROR("Disconnect from TransportPtr, but not in list!");
  }

private:
  boost::mutex mutex_;
  TransportTCPPtr server_;
  std::list<TransportTCPPtr> connections_;
  Callback cb_;
};

template <typename M>
class ServiceClient {
public:
  ServiceClient() : req_rep_(NULL), done_(true) {

  }

  void call(boost::shared_ptr<TransportTCP> server, M *req_rep) {
    server->enableMessagePass();
    server->enableRead();
    server->setMsgCallback(boost::bind(&ServiceClient::onResponse, this, _1));

    uint32_t req_sz = ros::serialization::serializationLength(req_rep->request);
    boost::shared_array<uint8_t> req_bytes(new uint8_t[req_sz]);
    ros::serialization::OStream ostream(req_bytes.get(), req_sz);
    ros::serialization::serialize(ostream, req_rep->request);
    server->sendMessage(req_bytes, req_sz);
    {
      boost::unique_lock<boost::mutex> lock(mutex_);
      req_rep_ = req_rep;
      done_ = false;
      while (done_ == false) {
        cv_.wait(lock);
      }
    }
    req_rep_ = NULL;
  }

private:
  void onResponse(const std::vector<Frame> &frames) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    if (frames.size() != 1) {
      ROS_ERROR("Got %zu frames; expected 1", frames.size());
      ROS_BREAK();
    }
    const boost::shared_array<uint8_t> &bytes = frames[0].data;
    uint32_t sz = frames[0].size;

    if (done_ != false) {
      ROS_ERROR("done_ is not false; unexpected request?");
      ROS_BREAK();
    }
    ros::serialization::IStream istream(bytes.get(), sz);
    ros::serialization::Serializer<typename M::Response>::read(istream, req_rep_->response);
    done_ = true;
    cv_.notify_all();
  }

  boost::mutex mutex_;
  boost::condition_variable cv_;
  M *req_rep_;
  bool done_;
};

//============================== MultiService ===============================//
class ServiceCallback {
public:
  virtual ~ServiceCallback() {}
  virtual void call(const boost::shared_array<uint8_t>&, uint32_t,
                    boost::shared_array<uint8_t>*, uint32_t*) = 0;
};

template <typename M>
class ServiceCallbackT : public ServiceCallback {
public:
  typedef typename M::Request Request;
  typedef typename M::Response Response;
  typedef boost::function<void(const Request&, Response*)> Callback;

  ServiceCallbackT(const Callback &cb) : cb_(cb) {}

  virtual void call(const boost::shared_array<uint8_t> &req_bytes, uint32_t req_sz,
                    boost::shared_array<uint8_t>* resp_bytes, uint32_t* resp_sz) {
    Request request;
    Response response;
    ros::serialization::IStream istream(req_bytes.get(), req_sz);
    ros::serialization::Serializer<Request>::read(istream, request);

    cb_(request, &response);

    *resp_sz = ros::serialization::serializationLength(response);
    resp_bytes->reset(new uint8_t[*resp_sz]);
    ros::serialization::OStream ostream(resp_bytes->get(), *resp_sz);
    ros::serialization::serialize(ostream, response);
  }

private:
  Callback cb_;
};

typedef boost::shared_ptr<ServiceCallback> ServiceCallbackPtr;

class MultiServiceManager {
public:
  MultiServiceManager(TransportTCPPtr server) : server_(server) {

  }

  ~MultiServiceManager() {
    server_->close();
  }

  const TransportTCP& socket() { return *server_; }

  void init(int port) {
    if (!server_->listen(port, 5,
                         boost::bind(&MultiServiceManager::onAccept, this, _1))) {
      ROS_ERROR("Failed to listen");
      ROS_BREAK();
    }
  }

  void bind(const std::string &method, ServiceCallbackPtr cb) {
    boost::mutex::scoped_lock lock(mutex_);
    cbs_[method] = cb;
  }

  void onAccept(const TransportTCPPtr &tcp) {
    boost::mutex::scoped_lock lock(mutex_);
    ROS_INFO("Got a connection from %s", tcp->getClientURI().c_str());
    tcp->enableMessagePass();
    tcp->enableRead();
    tcp->setMsgCallback(boost::bind(&MultiServiceManager::onReceive, this,
                                    tcp, _1));
    tcp->setDisconnectCallback(boost::bind(&MultiServiceManager::onDisconnect,
                                           this, _1));
    connections_.push_back(tcp);
  }

  void onReceive(const TransportTCPPtr &tcp,
                 const std::vector<Frame> &frames) {
    boost::mutex::scoped_lock lock(mutex_);
    if (frames.size() != 2) {
      ROS_ERROR("Got %zu frames; expected 2", frames.size());
      ROS_BREAK();
    }

    std::string method((char*)frames[0].data.get(), frames[0].size);
    ROS_INFO("Received a message for method %s", method.c_str());

    const boost::shared_array<uint8_t> &req_bytes = frames[1].data;
    uint32_t req_sz = frames[1].size;

    std::map<std::string, ServiceCallbackPtr>::iterator it = cbs_.find(method);
    if (it != cbs_.end()) {
      boost::shared_array<uint8_t> resp_bytes(NULL);
      uint32_t resp_sz;
      it->second->call(req_bytes, req_sz, &resp_bytes, &resp_sz);
      tcp->sendMessage(resp_bytes, resp_sz);
    } else {
      ROS_WARN("No method bound for type %s", method.c_str());
    }
  }

  void onDisconnect(const TransportPtr &tcp) {
    boost::mutex::scoped_lock lock(mutex_);

    for (std::list<TransportTCPPtr>::iterator it = connections_.begin();
         it != connections_.end(); ++it) {
      if (*it == tcp) {
        connections_.erase(it);
        return;
      }
    }
    ROS_ERROR("Disconnect from TransportPtr, but not in list!");
  }

private:
  boost::mutex mutex_;
  TransportTCPPtr server_;
  std::list<TransportTCPPtr> connections_;
  std::map<std::string, ServiceCallbackPtr> cbs_;
};

class ServiceResponse {
public:
  virtual ~ServiceResponse() {}
  virtual void call(const std::vector<Frame> &frames) = 0;
  virtual TransportTCPPtr& socket() = 0;
};

template<typename T>
class ServiceResponseT : public ServiceResponse {
public:
  typedef typename T::Request Request;
  typedef typename T::Response Response;
  typedef boost::function<void(const T&)> Callback;

  ServiceResponseT(const Callback &cb, TransportTCPPtr tcp, const Request &req)
    : cb_(cb), tcp_(tcp)  {
    req_resp_.request = req;
  }

  virtual void call(const std::vector<Frame> &frames) {
    if (frames.size() != 1) {
      ROS_ERROR("Got %zu frames; expected 1", frames.size());
      ROS_BREAK();
    }

    ros::serialization::IStream istream(frames[0].data.get(), frames[0].size);
    ros::serialization::Serializer<Response>::read(istream, req_resp_.response);

    cb_(req_resp_);

    tcp_->close();
  }

  virtual TransportTCPPtr& socket() {
    return tcp_;
  }

private:
  T req_resp_;
  Callback cb_;
  TransportTCPPtr tcp_;
};

class MultiServiceClient {
public:
  MultiServiceClient() { }

  template <typename M>
  void call(TransportTCPPtr server,
            const std::string &method,
            const M &req_rep,
            const typename ServiceResponseT<M>::Callback &resp_cb) {
    boost::mutex::scoped_lock lock(mutex_);

    const typename M::Request &req = req_rep.request;
    ServiceResponseT<M> *srv_resp_ptr = new ServiceResponseT<M>(resp_cb, server, req);

    server->enableMessagePass();
    server->enableRead();
    server->setMsgCallback(boost::bind(&ServiceResponseT<M>::call, srv_resp_ptr, _1));
    server->setDisconnectCallback(boost::bind(&MultiServiceClient::onDisconnect, this, _1));
    uint32_t req_sz = ros::serialization::serializationLength(req);
    boost::shared_array<uint8_t> req_bytes(new uint8_t[req_sz]);
    ros::serialization::OStream ostream(req_bytes.get(), req_sz);
    ros::serialization::serialize(ostream, req);

    std::vector<Frame> frames;
    frames.resize(2);
    frames[0].size = method.size();
    frames[0].data.reset(new uint8_t[method.size()]);
    copy(method.begin(), method.end(), frames[0].data.get());
    frames[1].size = req_sz;
    frames[1].data = req_bytes;

    server->sendFrames(frames);

    outgoing_calls_.push_back(srv_resp_ptr);
  }

private:
  void onDisconnect(const TransportPtr &conn) {
    removeConnection(conn);
  }

  void removeConnection(const TransportPtr &conn) {
    boost::mutex::scoped_lock lock(mutex_);
    for (std::list<ServiceResponse*>::iterator it = outgoing_calls_.begin();
         it != outgoing_calls_.end(); ++it) {
      TransportTCPPtr sock = (*it)->socket();
      if (sock == conn) {
        delete *it;
        outgoing_calls_.erase(it);
        return;
      }
    }
    ROS_WARN("Couldn't find connection to erase");
  }
  boost::mutex mutex_;
  std::list<ServiceResponse*> outgoing_calls_;
};

}

#endif // SERVICE_HPP
