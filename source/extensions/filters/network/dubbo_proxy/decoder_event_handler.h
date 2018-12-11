#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/deserializer.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class ProtocolDataPassthroughConverter {
public:
  ProtocolDataPassthroughConverter() : buffer_(nullptr) {}
  virtual ~ProtocolDataPassthroughConverter() {}

  void initProtocolConverter(Buffer::Instance& buffer) { buffer_ = &buffer; }

  virtual Network::FilterStatus transferHeaderTo(const Buffer::Instance& header_buf, size_t size) {
    if (buffer_ != nullptr) {
      Buffer::OwnedImpl copy(header_buf);
      buffer_->move(copy, size);
    }
    return Network::FilterStatus::Continue;
  }
  virtual Network::FilterStatus transferBodyTo(const Buffer::Instance& body_buf, size_t size) {
    if (buffer_ != nullptr) {
      Buffer::OwnedImpl copy(body_buf);
      buffer_->move(copy, size);
    }
    return Network::FilterStatus::Continue;
  }

protected:
  Buffer::Instance* buffer_{};
};

class DecoderEventHandler : public ProtocolDataPassthroughConverter {
public:
  virtual ~DecoderEventHandler() override {}

  virtual Network::FilterStatus transportBegin() PURE;
  virtual Network::FilterStatus transportEnd() PURE;

  virtual Network::FilterStatus messageBegin(MessageType type, int64_t message_id,
                                             SerializationType serialization_type) PURE;
  virtual Network::FilterStatus messageEnd(MessageMetadataSharedPtr metadata) PURE;
};

class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}

  /**
   * @return DecoderEventHandler& a new DecoderEventHandler for a message.
   */
  virtual DecoderEventHandler* newDecoderEventHandler() PURE;

  virtual void onHeartbeat(MessageMetadataSharedPtr) {}
};

typedef std::shared_ptr<DecoderEventHandler> DecoderEventHandlerSharedPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
