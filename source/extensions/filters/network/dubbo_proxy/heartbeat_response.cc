#include "extensions/filters/network/dubbo_proxy/heartbeat_response.h"

#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

DubboFilters::DirectResponse::ResponseType
HeartbeatResponse::encode(MessageMetadata& metadata, DubboProxy::Protocol& protocol,
                          Deserializer& deserializer, Buffer::Instance& buffer) const {
  std::string response("");
  metadata.setMessageType(MessageType::Reply);
  if (!protocol.encode(buffer, response.size(), metadata)) {
    throw EnvoyException("failed to encode local reply message");
  } else {
    deserializer.serializeRpcResult(buffer, response,
                                    static_cast<uint8_t>(RpcResponseType::ResponseWithNullValue));
  }

  ENVOY_LOG(debug, "buffer length {}", buffer.length());
  return DirectResponse::ResponseType::SuccessReply;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
