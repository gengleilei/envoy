#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

// Supported serialization type
enum class SerializationType : uint8_t {
  Hessian = 2,
  Json = 6,
};

// Message Type
enum class MessageType : uint8_t {
  Call = 0,
  Reply = 1,
  Oneway = 3,
  Exception = 4,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST MESSAGE TYPE
  LastMessageType = Exception,
};

/**
 * Dubbo protocol response status types.
 * See org.apache.dubbo.remoting.exchange
 */
enum class ResponseStatus : uint8_t {
  Ok = 20,
  ClientTimeout = 30,
  ServerTimeout = 31,
  BadRequest = 40,
  BadResponse = 50,
  ServiceNotFound = 60,
  ServiceError = 70,
  ServerError = 80,
  ClientError = 90,
  ServerThreadpoolExhaustedError = 100,
};

/**
 * RpcResult represent the result of an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcResult.java
 */
class RpcResult {
public:
  virtual ~RpcResult() {}
  virtual bool hasException() const PURE;
};

typedef std::unique_ptr<RpcResult> RpcResultPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
