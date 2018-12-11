#pragma once

#include "extensions/filters/network/dubbo_proxy/deserializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

enum class RpcResponseType : uint8_t {
  ResponseWithException = 0,
  ResponseWithValue = 1,
  ResponseWithNullValue = 2,
  ResponseWithExceptionWithAttachments = 3,
  ResponseValueWithAttachments = 4,
  ResponseNullValueWithAttachments = 5,

  ResponseTypeCount,
};

class HessianDeserializerImpl : public Deserializer {
public:
  HessianDeserializerImpl() {}
  ~HessianDeserializerImpl() {}
  virtual const std::string& name() const override {
    return DeserializerNames::get().fromType(type());
  }
  virtual DeserializerType type() const override { return DeserializerType::Hessian; }
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size,
                                        MessageMetadataSharedPtr metadata) override;
  virtual RpcResultPtr deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) override;

  virtual void serializeRpcResult(Buffer::Instance& output_buffer, const std::string& content,
                                  uint8_t type) override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy