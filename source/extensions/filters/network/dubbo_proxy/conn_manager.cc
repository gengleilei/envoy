#include "extensions/filters/network/dubbo_proxy/conn_manager.h"

#include "envoy/common/exception.h"

#include "common/common/fmt.h"

#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/heartbeat_response.h"
#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

ConnectionManager::ConnectionManager(Config& config, Runtime::RandomGenerator& random_generator,
                                     Event::TimeSystem& time_system)
    : config_(config), time_system_(time_system), stats_(config_.stats()),
      random_generator_(random_generator), deserializer_(config.createDeserializer()),
      protocol_(config.createProtocol()),
      decoder_(std::make_unique<Decoder>(*protocol_.get(), *deserializer_.get(), this)) {}

ConnectionManager::~ConnectionManager() {}

Network::FilterStatus ConnectionManager::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "dubbo: read {} bytes", data.length());
  request_buffer_.move(data);
  dispatch();

  if (end_stream) {
    ENVOY_CONN_LOG(trace, "downstream half-closed", read_callbacks_->connection());

    // Downstream has closed. Unless we're waiting for an upstream connection to complete a oneway
    // request, close. The special case for oneway requests allows them to complete before the
    // ConnectionManager is destroyed.
    if (stopped_) {
      ASSERT(!active_message_list_.empty());
      auto metadata = (*active_message_list_.begin())->metadata();
      if (metadata->message_type() == MessageType::Oneway) {
        ENVOY_CONN_LOG(trace, "waiting for one-way completion", read_callbacks_->connection());
        half_closed_ = true;
        return Network::FilterStatus::StopIteration;
      }
    }

    ENVOY_LOG(debug, "dubbo: end data processing");
    resetAllMessages(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus ConnectionManager::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void ConnectionManager::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*this);
  read_callbacks_->connection().enableHalfClose(true);
}

void ConnectionManager::onEvent(Network::ConnectionEvent event) {
  resetAllMessages(event == Network::ConnectionEvent::LocalClose);
}

DecoderEventHandler* ConnectionManager::newDecoderEventHandler() {
  ENVOY_LOG(debug, "dubbo: create the new docoder enent handler");

  ActiveMessagePtr new_message(new ActiveMessage(*this));
  new_message->createFilterChain();
  new_message->moveIntoList(std::move(new_message), active_message_list_);

  return (*active_message_list_.begin()).get();
}

void ConnectionManager::onHeartbeat(MessageMetadataSharedPtr metadata) {
  stats_.request_event_.inc();
  HeartbeatResponse heartbeat;
  Buffer::OwnedImpl response_buffer;
  heartbeat.encode(*metadata, *protocol_, *deserializer_, response_buffer);
  if (read_callbacks_->connection().state() == Network::Connection::State::Open) {
    read_callbacks_->connection().write(response_buffer, false);
  } else {
    ENVOY_LOG(warn, "dubbo: downstream connection is closed or closing");
  }
}

void ConnectionManager::dispatch() {
  if (0 == request_buffer_.length()) {
    ENVOY_LOG(warn, "dubbo: it's empty data");
    return;
  }

  if (stopped_) {
    ENVOY_CONN_LOG(debug, "dubbo: dubbo filter stopped", read_callbacks_->connection());
    return;
  }

  try {
    bool underflow = false;
    while (!underflow) {
      Network::FilterStatus status = decoder_->onData(request_buffer_, underflow);
      if (status == Network::FilterStatus::StopIteration) {
        stopped_ = true;
        break;
      }
    }
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "dubbo: error {}", ex.what());

    stats_.request_decoding_error_.inc();

    // Use the current rpc to send an error downstream, if possible.
    if (!active_message_list_.empty())
      active_message_list_.front()->onError(ex.what());

    resetAllMessages(true);
  }
}

void ConnectionManager::sendLocalReply(MessageMetadata& metadata,
                                       const DubboFilters::DirectResponse& response,
                                       bool end_stream) {
  ENVOY_LOG(error, "dubbo: send local reply");
  Buffer::OwnedImpl buffer;
  const DubboFilters::DirectResponse::ResponseType result =
      response.encode(metadata, *protocol_, *deserializer_, buffer);
  read_callbacks_->connection().write(buffer, end_stream);
  if (end_stream) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  switch (result) {
  case DubboFilters::DirectResponse::ResponseType::SuccessReply:
    stats_.response_success_.inc();
    break;
  case DubboFilters::DirectResponse::ResponseType::ErrorReply:
    stats_.response_error_.inc();
    break;
  case DubboFilters::DirectResponse::ResponseType::Exception:
    stats_.response_exception_.inc();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void ConnectionManager::continueDecoding() {
  ENVOY_CONN_LOG(debug, "dubbo filter continued", read_callbacks_->connection());
  ENVOY_LOG(debug, "dubbo filter continued");
  stopped_ = false;
  dispatch();

  if (!stopped_ && half_closed_) {
    // If we're half closed, but not stopped waiting for an upstream,
    // reset any pending rpcs and close the connection.
    resetAllMessages(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ConnectionManager::deferredMessage(ActiveMessage& message) {
  read_callbacks_->connection().dispatcher().deferredDelete(
      message.removeFromList(active_message_list_));
}

void ConnectionManager::resetAllMessages(bool local_reset) {
  while (!active_message_list_.empty()) {
    if (local_reset) {
      ENVOY_CONN_LOG(debug, "local close with active request", read_callbacks_->connection());
      stats_.cx_destroy_local_with_active_rq_.inc();
    } else {
      ENVOY_CONN_LOG(debug, "remote close with active request", read_callbacks_->connection());
      stats_.cx_destroy_remote_with_active_rq_.inc();
    }

    active_message_list_.front()->onReset();
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
