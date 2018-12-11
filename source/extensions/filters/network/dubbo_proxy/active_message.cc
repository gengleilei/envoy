#include "extensions/filters/network/dubbo_proxy/active_message.h"

#include "extensions/filters/network/dubbo_proxy/app_exception.h"
#include "extensions/filters/network/dubbo_proxy/conn_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

// class ResponseDecoder
ResponseDecoder::ResponseDecoder(ActiveMessage& parent, Deserializer& deserializer,
                                 Protocol& protocol)
    : parent_(parent), decoder_(std::make_unique<Decoder>(protocol, deserializer, this)),
      complete_(false) {}

ResponseDecoder::~ResponseDecoder() {}

bool ResponseDecoder::onData(Buffer::Instance& data) {
  ENVOY_LOG(debug, "dubbo response: the received reply data length is {}", data.length());

  upstream_buffer_.move(data);
  bool underflow = false;
  decoder_->onData(upstream_buffer_, underflow);
  ASSERT(complete_ || underflow);
  return complete_;
}

Network::FilterStatus ResponseDecoder::transportBegin() {
  parent_.parent_.stats().response_.inc();

  parent_.response_buffer_.drain(parent_.response_buffer_.length());
  ProtocolDataPassthroughConverter::initProtocolConverter(parent_.response_buffer_);

  return Network::FilterStatus::Continue;
}

Network::FilterStatus ResponseDecoder::transportEnd() {
  parent_.parent_.read_callbacks()->connection().write(parent_.response_buffer_, false);
  ENVOY_LOG(debug,
            "dubbo response: the upstream response message has been forwarded to the downstream");
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ResponseDecoder::messageBegin(MessageType type, int64_t message_id,
                                                    SerializationType serialization_type) {
  UNREFERENCED_PARAMETER(type);
  UNREFERENCED_PARAMETER(message_id);
  UNREFERENCED_PARAMETER(serialization_type);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ResponseDecoder::messageEnd(MessageMetadataSharedPtr metadata) {
  ASSERT(metadata->message_type() == MessageType::Reply ||
         metadata->message_type() == MessageType::Exception);

  if (metadata->message_type() == MessageType::Exception) {
    parent_.parent_.stats().response_exception_.inc();
  }

  metadata_ = metadata;

  switch (metadata->response_status()) {
  case ResponseStatus::Ok:
    parent_.parent_.stats().response_success_.inc();
    break;
  default:
    parent_.parent_.stats().response_error_.inc();
    ENVOY_LOG(error, "dubbo response status: {}",
              static_cast<uint8_t>(metadata->response_status()));
    break;
  }

  complete_ = true;

  ENVOY_LOG(debug, "dubbo response: complete processing of upstream response messages, id is {}",
            metadata->request_id());

  return Network::FilterStatus::Continue;
}

DecoderEventHandler* ResponseDecoder::newDecoderEventHandler() { return this; }

// class ActiveMessageDecoderFilter
ActiveMessageDecoderFilter::ActiveMessageDecoderFilter(ActiveMessage& parent,
                                                       DubboFilters::DecoderFilterSharedPtr filter)
    : parent_(parent), handle_(filter) {}

uint64_t ActiveMessageDecoderFilter::requestId() const { return parent_.request_id_; }

uint64_t ActiveMessageDecoderFilter::streamId() const { return parent_.stream_id_; }

const Network::Connection* ActiveMessageDecoderFilter::connection() const {
  return parent_.connection();
}

void ActiveMessageDecoderFilter::continueDecoding() {
  const Network::FilterStatus status = parent_.applyDecoderFilters(this);
  if (status == Network::FilterStatus::Continue) {
    // All filters have been executed for the current decoder state.
    if (parent_.pending_message_end_) {
      // If the filter stack was paused during messageEnd, handle end-of-request details.
      parent_.finalizeRequest();
    }
    parent_.continueDecoding();
  }
}

Router::RouteConstSharedPtr ActiveMessageDecoderFilter::route() { return parent_.route(); }

DeserializerType ActiveMessageDecoderFilter::downstreamDeserializerType() const {
  return parent_.downstreamDeserializerType();
}

ProtocolType ActiveMessageDecoderFilter::downstreamProtocolType() const {
  return parent_.downstreamProtocolType();
}

void ActiveMessageDecoderFilter::sendLocalReply(const DubboFilters::DirectResponse& response,
                                                bool end_stream) {
  parent_.sendLocalReply(response, end_stream);
}

void ActiveMessageDecoderFilter::startUpstreamResponse(Deserializer& deserializer,
                                                       Protocol& protocol) {
  parent_.startUpstreamResponse(deserializer, protocol);
}

DubboFilters::UpstreamResponseStatus
ActiveMessageDecoderFilter::upstreamData(Buffer::Instance& buffer) {
  return parent_.upstreamData(buffer);
}

void ActiveMessageDecoderFilter::resetDownstreamConnection() {
  parent_.resetDownstreamConnection();
}

StreamInfo::StreamInfo& ActiveMessageDecoderFilter::streamInfo() { return parent_.streamInfo(); }

// class ActiveMessage
ActiveMessage::ActiveMessage(ConnectionManager& parent)
    : parent_(parent),
      request_timer_(new Stats::Timespan(parent_.stats_.request_time_ms_, parent_.time_system_)),
      request_id_(-1), stream_id_(parent_.random_generator_.random()),
      stream_info_(parent_.time_system_), pending_message_end_(false), local_response_sent_(false) {
  parent_.stats_.request_active_.inc();
  stream_info_.setDownstreamLocalAddress(parent_.read_callbacks_->connection().localAddress());
  stream_info_.setDownstreamRemoteAddress(parent_.read_callbacks_->connection().remoteAddress());
}

ActiveMessage::~ActiveMessage() {
  parent_.stats_.request_active_.dec();
  request_timer_->complete();
  for (auto& filter : decoder_filters_) {
    filter->handle_->onDestroy();
  }

  ENVOY_LOG(debug, "dubbo request: ActiveMessage::~ActiveMessage");
}

Network::FilterStatus ActiveMessage::transportBegin() {
  filter_action_ = [](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->transportBegin();
  };

  return this->applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::transportEnd() {
  filter_action_ = [](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->transportEnd();
  };

  return this->applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::messageBegin(MessageType type, int64_t message_id,
                                                  SerializationType serialization_type) {
  using message_tuple = std::tuple<MessageType, int64_t, SerializationType>;
  filter_context_ = message_tuple(type, message_id, serialization_type);
  filter_action_ = [this](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    message_tuple& t = absl::any_cast<message_tuple&>(filter_context_);
    MessageType& type = std::get<0>(t);
    int64_t& message_id = std::get<1>(t);
    SerializationType& serialization_type = std::get<2>(t);
    return filter->messageBegin(type, message_id, serialization_type);
  };

  return this->applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::messageEnd(MessageMetadataSharedPtr metadata) {
  ASSERT(metadata->message_type() == MessageType::Call ||
         metadata->message_type() == MessageType::Oneway);

  ENVOY_LOG(debug, "dubbo request: start processing downstream request messages, id is {}",
            metadata->request_id());

  switch (metadata->serialization_type()) {
  case SerializationType::Json:
  case SerializationType::Hessian:
    break;
  default:
    throw EnvoyException(fmt::format("dubbo request: unexpected serialization type {}",
                                     static_cast<uint8_t>((metadata->serialization_type()))));
  }

  metadata_ = metadata;

  filter_context_ = metadata;
  filter_action_ = [this](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->messageEnd(metadata);
  };

  auto status = applyDecoderFilters(nullptr);
  if (status == Network::FilterStatus::StopIteration) {
    pending_message_end_ = true;
    return status;
  }

  finalizeRequest();

  ENVOY_LOG(debug, "dubbo request: complete processing of downstream request messages, id is {}",
            metadata_->request_id());

  return status;
}

Network::FilterStatus ActiveMessage::transferHeaderTo(const Buffer::Instance& header_buf,
                                                      size_t size) {
  filter_context_ = std::tuple<const Buffer::Instance&, size_t>(header_buf, size);
  filter_action_ = [this](ProtocolDataPassthroughConverter* transfer) -> Network::FilterStatus {
    std::tuple<const Buffer::Instance&, size_t>& t =
        absl::any_cast<std::tuple<const Buffer::Instance&, size_t>&>(filter_context_);
    const Buffer::Instance& buf = std::get<0>(t);
    size_t size = std::get<1>(t);
    return transfer->transferHeaderTo(buf, size);
  };

  return applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::transferBodyTo(const Buffer::Instance& body_buf, size_t size) {
  filter_context_ = std::tuple<const Buffer::Instance&, size_t>(body_buf, size);
  filter_action_ = [this](ProtocolDataPassthroughConverter* transfer) -> Network::FilterStatus {
    std::tuple<const Buffer::Instance&, size_t>& t =
        absl::any_cast<std::tuple<const Buffer::Instance&, size_t>&>(filter_context_);
    const Buffer::Instance& buf = std::get<0>(t);
    size_t size = std::get<1>(t);
    return transfer->transferBodyTo(buf, size);
  };

  return applyDecoderFilters(nullptr);
}

void ActiveMessage::finalizeRequest() {
  pending_message_end_ = false;

  parent_.stats_.request_.inc();

  bool is_one_way = false;
  switch (metadata_->message_type()) {
  case MessageType::Call:
    parent_.stats_.request_twoway_.inc();
    break;
  case MessageType::Oneway:
    parent_.stats_.request_oneway_.inc();
    is_one_way = true;
    break;
  default:
    break;
  }

  if (local_response_sent_ || is_one_way) {
    parent_.deferredMessage(*this);
  }
}

void ActiveMessage::createFilterChain() {
  parent_.config_.filterFactory().createFilterChain(*this);
}

DubboProxy::Router::RouteConstSharedPtr ActiveMessage::route() {
  if (cached_route_)
    return cached_route_.value();

  if (metadata_ != nullptr) {
    DubboProxy::Router::RouteConstSharedPtr route =
        parent_.config_.routerConfig().route(*metadata_, stream_id_);
    cached_route_ = std::move(route);
    return cached_route_.value();
  }

  return nullptr;
}

Network::FilterStatus ActiveMessage::applyDecoderFilters(ActiveMessageDecoderFilter* filter) {
  if (!local_response_sent_) {
    std::list<ActiveMessageDecoderFilterPtr>::iterator entry;
    if (!filter) {
      entry = decoder_filters_.begin();
    } else {
      entry = std::next(filter->entry());
    }

    for (; entry != decoder_filters_.end(); entry++) {
      const Network::FilterStatus status = filter_action_((*entry)->handle_.get());
      if (local_response_sent_) {
        break;
      }

      if (status != Network::FilterStatus::Continue) {
        return status;
      }
    }
  }

  filter_action_ = nullptr;
  filter_context_.reset();

  return Network::FilterStatus::Continue;
}

void ActiveMessage::sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream) {
  metadata_->setRequestId(request_id_);
  parent_.sendLocalReply(*metadata_, response, end_stream);

  if (end_stream) {
    return;
  }

  local_response_sent_ = true;
}

void ActiveMessage::startUpstreamResponse(Deserializer& deserializer, Protocol& protocol) {
  ENVOY_LOG(debug, "dubbo response: start upstream");
  ASSERT(response_decoder_ == nullptr);

  // Create a response message decoder.
  response_decoder_ = std::make_unique<ResponseDecoder>(*this, deserializer, protocol);
}

DubboFilters::UpstreamResponseStatus ActiveMessage::upstreamData(Buffer::Instance& buffer) {
  ASSERT(response_decoder_ != nullptr);

  try {
    if (response_decoder_->onData(buffer)) {
      if (requestId() != response_decoder_->requestId()) {
        throw EnvoyException(fmt::format("dubbo response: request ID is not equal, {}:{}",
                                         requestId(), response_decoder_->requestId()));
      }

      // Completed upstream response.
      parent_.deferredMessage(*this);
      return DubboFilters::UpstreamResponseStatus::Complete;
    }
    return DubboFilters::UpstreamResponseStatus::MoreData;
  } catch (const AppException& ex) {
    ENVOY_LOG(error, "dubbo response: application exception ({})", ex.what());
    parent_.stats_.response_decoding_error_.inc();

    sendLocalReply(ex, true);
    return DubboFilters::UpstreamResponseStatus::Reset;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "dubbo response: exception ({})", parent_.read_callbacks_->connection(),
                   ex.what());
    parent_.stats_.response_decoding_error_.inc();

    onError(ex.what());
    return DubboFilters::UpstreamResponseStatus::Reset;
  }
}

void ActiveMessage::resetDownstreamConnection() {
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

uint64_t ActiveMessage::requestId() const {
  return metadata_ != nullptr ? metadata_->request_id() : 0;
}

uint64_t ActiveMessage::streamId() const { return stream_id_; }

void ActiveMessage::continueDecoding() { parent_.continueDecoding(); }

DeserializerType ActiveMessage::downstreamDeserializerType() const {
  return parent_.deserializer_->type();
}

ProtocolType ActiveMessage::downstreamProtocolType() const { return parent_.protocol_->type(); }

StreamInfo::StreamInfo& ActiveMessage::streamInfo() { return stream_info_; }

const Network::Connection* ActiveMessage::connection() const {
  return &parent_.read_callbacks_->connection();
}

void ActiveMessage::addDecoderFilter(DubboFilters::DecoderFilterSharedPtr filter) {
  ActiveMessageDecoderFilterPtr wrapper =
      std::make_unique<ActiveMessageDecoderFilter>(*this, filter);
  filter->setDecoderFilterCallbacks(*wrapper);
  wrapper->moveIntoListBack(std::move(wrapper), decoder_filters_);
}

void ActiveMessage::onReset() { parent_.deferredMessage(*this); }

void ActiveMessage::onError(const std::string& what) {
  if (metadata_) {
    sendLocalReply(AppException(AppExceptionType::BadResponse, what), true);
    return;
  }

  parent_.deferredMessage(*this);
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
