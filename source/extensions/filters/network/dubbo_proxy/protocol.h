#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

enum class ProtocolType {
  Dubbo,
  HSF,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST PROTOCOL TYPE
  LastProtocolType = HSF,
};

/**
 * Names of available Protocol implementations.
 */
class ProtocolNameValues {
public:
  typedef std::map<ProtocolType, std::string> ProtocolTypeNameMap;

  const ProtocolTypeNameMap protocolTypeNameMap = {
      {ProtocolType::Dubbo, "dubbo"},
      {ProtocolType::HSF, "HSF"},
  };

  const std::string& fromType(ProtocolType type) const {
    auto itor = protocolTypeNameMap.find(type);
    if (itor != protocolTypeNameMap.end())
      return itor->second;

    NOT_REACHED_GCOVR_EXCL_LINE;
  }
};

typedef ConstSingleton<ProtocolNameValues> ProtocolNames;

/**
 * See https://dubbo.incubator.apache.org/en-us/docs/dev/implementation.html
 */
class Protocol {
public:
  struct Context {
    size_t header_size_ = 0;
    int32_t body_size_ = 0;
    bool is_heartbeat_ = false;
  };
  virtual ~Protocol() {}
  Protocol() {}
  virtual const std::string& name() const PURE;

  /**
   * @return ProtocolType the protocol type
   */
  virtual ProtocolType type() const PURE;

  /*
   * decodes the dubbo protocol message, potentially invoking callbacks.
   * If successful, the message is removed from the buffer.
   *
   * @param buffer the currently buffered dubbo data.
   * @param context save the meta data of current messages.
   * @param metadata the meta data of current messages
   * @return bool true if a complete message was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this protocol.
   */
  virtual bool decode(Buffer::Instance& buffer, Context* context,
                      MessageMetadataSharedPtr metadata) PURE;

  /*
   * encodes the dubbo protocol message.
   *
   * @param buffer save the currently buffered dubbo data.
   * @param metadata the meta data of dubbo protocol
   * @return bool true if the protocol coding succeeds.
   */
  virtual bool encode(Buffer::Instance& buffer, int32_t body_size,
                      const MessageMetadata& metadata) PURE;
};

typedef std::unique_ptr<Protocol> ProtocolPtr;

/**
 * Implemented by each Dubbo protocol and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedProtocolConfigFactory {
public:
  virtual ~NamedProtocolConfigFactory() {}

  /**
   * Create a particular Dubbo protocol
   * @return ProtocolFactoryCb the protocol
   */
  virtual ProtocolPtr createProtocol() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of Dubbo protocol
   * produced by the factory.
   */
  virtual std::string name() PURE;

  /**
   * Convenience method to lookup a factory by type.
   * @param ProtocolType the protocol type
   * @return NamedProtocolConfigFactory& for the ProtocolType
   */
  static NamedProtocolConfigFactory& getFactory(ProtocolType type) {
    const std::string& name = ProtocolNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactory<NamedProtocolConfigFactory>(name);
  }
};

/**
 * ProtocolFactoryBase provides a template for a trivial NamedProtocolConfigFactory.
 */
template <class ProtocolImpl> class ProtocolFactoryBase : public NamedProtocolConfigFactory {
  ProtocolPtr createProtocol() override { return std::move(std::make_unique<ProtocolImpl>()); }

  std::string name() override { return name_; }

protected:
  ProtocolFactoryBase(ProtocolType type) : name_(ProtocolNames::get().fromType(type)) {}

private:
  const std::string name_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
