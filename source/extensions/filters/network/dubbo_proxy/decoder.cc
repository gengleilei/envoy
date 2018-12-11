#include "extensions/filters/network/dubbo_proxy/decoder.h"

#include "common/common/macros.h"

#include "extensions/filters/network/dubbo_proxy/heartbeat_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

DecoderStateMachine::DecoderStatus
DecoderStateMachine::onTransportBegin(Buffer::Instance& buffer, Protocol::Context& context) {
  if (!protocol_.decode(buffer, &context, metadata_)) {
    ENVOY_LOG(debug, "dubbo decoder: need more data for {} protocol", protocol_.name());
    return DecoderStatus(ProtocolState::WaitForData);
  }

  if (context.is_heartbeat_) {
    buffer.drain(context.header_size_);
    decoder_callbacks_.onHeartbeat(metadata_);
    return DecoderStatus(ProtocolState::Done, Network::FilterStatus::Continue);
  } else {
    handler_ = decoder_callbacks_.newDecoderEventHandler();
  }
  return DecoderStatus(ProtocolState::onTransferHeaderTo, handler_->transportBegin());
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::onTransportEnd() {
  ENVOY_LOG(debug, "dubbo decoder: complete protocol processing");
  return DecoderStatus(ProtocolState::Done, handler_->transportEnd());
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::onTransferHeaderTo(Buffer::Instance& buffer,
                                                                           size_t length) {
  auto status =
      DecoderStatus(ProtocolState::onMessageBegin, handler_->transferHeaderTo(buffer, length));
  buffer.drain(length);
  return status;
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::onTransferBodyTo(Buffer::Instance& buffer,
                                                                         int32_t length) {
  auto status =
      DecoderStatus(ProtocolState::onTransportEnd, handler_->transferBodyTo(buffer, length));
  buffer.drain(length);
  return status;
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::onMessageBegin() {
  ENVOY_LOG(debug, "dubbo decoder: start deserializing messages, deserializer name {}",
            deserializer_.name());
  return DecoderStatus(ProtocolState::onMessageEnd,
                       handler_->messageBegin(metadata_->message_type(), metadata_->request_id(),
                                              metadata_->serialization_type()));
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::onMessageEnd(Buffer::Instance& buffer,
                                                                     int32_t message_size) {
  ENVOY_LOG(debug, "dubbo decoder: expected body size is {}", message_size);

  if (buffer.length() < static_cast<uint64_t>(message_size)) {
    ENVOY_LOG(debug, "dubbo decoder: need more data for {} deserialization, current size {}",
              deserializer_.name(), buffer.length());
    return DecoderStatus(ProtocolState::WaitForData);
  }

  switch (metadata_->message_type()) {
  case MessageType::Oneway:
    ENVOY_LOG(debug, "dubbo decoder: oneway message");
  case MessageType::Call:
    deserializer_.deserializeRpcInvocation(buffer, message_size, metadata_);
    break;
  case MessageType::Reply: {
    auto info = deserializer_.deserializeRpcResult(buffer, message_size);
    if (info->hasException()) {
      metadata_->setMessageType(MessageType::Exception);
    }
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  ENVOY_LOG(debug, "dubbo decoder: ends the deserialization of the message");
  return DecoderStatus(ProtocolState::onTransferBodyTo, handler_->messageEnd(metadata_));
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::handleState(Buffer::Instance& buffer) {
  switch (state_) {
  case ProtocolState::onTransportBegin:
    return onTransportBegin(buffer, context_);
  case ProtocolState::onTransferHeaderTo:
    return onTransferHeaderTo(buffer, context_.header_size_);
  case ProtocolState::onMessageBegin:
    return onMessageBegin();
  case ProtocolState::onMessageEnd:
    return onMessageEnd(buffer, context_.body_size_);
  case ProtocolState::onTransferBodyTo:
    return onTransferBodyTo(buffer, context_.body_size_);
  case ProtocolState::onTransportEnd:
    return onTransportEnd();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

ProtocolState DecoderStateMachine::run(Buffer::Instance& buffer) {
  while (state_ != ProtocolState::Done) {
    ENVOY_LOG(trace, "dubbo decoder: state {}, {} bytes available",
              ProtocolStateNameValues::name(state_), buffer.length());

    DecoderStatus s = handleState(buffer);
    if (s.next_state_ == ProtocolState::WaitForData) {
      return ProtocolState::WaitForData;
    }

    state_ = s.next_state_;

    ASSERT(s.filter_status_.has_value());
    if (s.filter_status_.value() == Network::FilterStatus::StopIteration) {
      return ProtocolState::StopIteration;
    }
  }

  return state_;
}

typedef std::unique_ptr<DecoderStateMachine> DecoderStateMachinePtr;

Decoder::Decoder(Protocol& protocol, Deserializer& deserializer,
                 DecoderCallbacks* decoder_callbacks)
    : deserializer_(deserializer), protocol_(protocol), decoder_callbacks_(decoder_callbacks) {}

Network::FilterStatus Decoder::onData(Buffer::Instance& data, bool& buffer_underflow) {
  ENVOY_LOG(debug, "dubbo decoder: {} bytes available", data.length());
  buffer_underflow = false;

  if (!decode_started_) {
    start();
  }

  ASSERT(state_machine_ != nullptr);

  ENVOY_LOG(debug, "dubbo decoder: protocol {}, state {}, {} bytes available", protocol_.name(),
            ProtocolStateNameValues::name(state_machine_->currentState()), data.length());

  ProtocolState rv = state_machine_->run(data);
  switch (rv) {
  case ProtocolState::WaitForData:
    ENVOY_LOG(debug, "dubbo decoder: wait for data");
    buffer_underflow = true;
    return Network::FilterStatus::Continue;
  case ProtocolState::StopIteration:
    ENVOY_LOG(debug, "dubbo decoder: wait for continuation");
    return Network::FilterStatus::StopIteration;
  case ProtocolState::HeartbeatEventDone:
    ENVOY_LOG(debug, "dubbo decoder: complete heartbeat processing");
    return Network::FilterStatus::StopIteration;
  default:
    break;
  }

  ASSERT(rv == ProtocolState::Done);

  complete();
  buffer_underflow = (data.length() == 0);
  ENVOY_LOG(debug, "dubbo decoder: data length {}", data.length());
  return Network::FilterStatus::Continue;
}

void Decoder::start() {
  metadata_ = std::make_shared<MessageMetadata>();
  state_machine_ = std::make_unique<DecoderStateMachine>(protocol_, deserializer_, metadata_,
                                                         *decoder_callbacks_);
  decode_started_ = true;
}

void Decoder::complete() {
  metadata_.reset();
  state_machine_.reset();
  decode_started_ = false;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
