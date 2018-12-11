#pragma once

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/message_define.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

enum class DeserializerType {
  Hessian,
  Json,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST PROTOCOL TYPE
  LastDeserializerType = Json,
};

/**
 * Names of available deserializer implementations.
 */
class DeserializerNameValues {
public:
  typedef std::map<DeserializerType, std::string> DeserializerTypeNameMap;

  const DeserializerTypeNameMap deserializerTypeNameMap = {
      {DeserializerType::Hessian, "hessian"},
      {DeserializerType::Json, "json"},
  };

  const std::string& fromType(DeserializerType type) const {
    auto itor = deserializerTypeNameMap.find(type);
    if (itor != deserializerTypeNameMap.end())
      return itor->second;

    NOT_REACHED_GCOVR_EXCL_LINE;
  }
};

typedef ConstSingleton<DeserializerNameValues> DeserializerNames;

class Deserializer {
public:
  virtual ~Deserializer() {}
  /**
   * Return this Deserializer's name
   *
   * @return std::string containing the serialization name.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return DeserializerType the deserializer type
   */
  virtual DeserializerType type() const PURE;

  /**
   * deserialize an rpc call
   * If successful, the RpcInvocation removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @body_size the complete RpcInvocation size
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size,
                                        MessageMetadataSharedPtr metadata) PURE;
  /**
   * deserialize result of an rpc call
   * If successful, the RpcResult removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @body_size the complete RpcResult size
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual RpcResultPtr deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) PURE;

  virtual void serializeRpcResult(Buffer::Instance& output_buffer, const std::string& content,
                                  uint8_t type) PURE;
};

typedef std::unique_ptr<Deserializer> DeserializerPtr;

/**
 * Implemented by each Dubbo deserialize and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedDeserializerConfigFactory {
public:
  virtual ~NamedDeserializerConfigFactory() {}

  /**
   * Create a particular Dubbo deserialize.
   * @return DeserializerPtr the transport
   */
  virtual DeserializerPtr createDeserializer() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of Dubbo deserializer
   * produced by the factory.
   */
  virtual std::string name() PURE;

  /**
   * Convenience method to lookup a factory by type.
   * @param TransportType the transport type
   * @return NamedDeserializerConfigFactory& for the TransportType
   */
  static NamedDeserializerConfigFactory& getFactory(DeserializerType type) {
    const std::string& name = DeserializerNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactory<NamedDeserializerConfigFactory>(name);
  }
};

/**
 * DeserializerFactoryBase provides a template for a trivial NamedDeserializerConfigFactory.
 */
template <class DeserializerImpl>
class DeserializerFactoryBase : public NamedDeserializerConfigFactory {
  DeserializerPtr createDeserializer() override {
    return std::move(std::make_unique<DeserializerImpl>());
  }

  std::string name() override { return name_; }

protected:
  DeserializerFactoryBase(DeserializerType type) : name_(DeserializerNames::get().fromType(type)) {}

private:
  const std::string name_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy