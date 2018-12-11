
#pragma once

#include <map>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/fmt.h"
#include "common/http/header_map_impl.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/message_define.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MessageMetadata {
public:
  struct ParameterValue {
    ParameterValue(uint32_t index, const std::string& type, const std::string& value)
        : index_(index), type_(type), value_(value) {}
    ~ParameterValue() {}

    uint32_t index_;
    std::string type_;
    std::string value_;
  };
  typedef std::map<uint32_t, ParameterValue> ParameterValueMap;
  typedef std::unique_ptr<ParameterValueMap> ParameterValueMapPtr;

  MessageMetadata() {}

  void setServiceName(const std::string& name) { service_name_ = name; }
  const std::string& service_name() const { return service_name_; }

  void setMethodName(const std::string& name) { method_name_ = name; }
  const std::string& method_name() const { return method_name_.value(); }
  bool hasMethodName() const { return method_name_.has_value(); }

  void setServiceVersion(const std::string& version) { service_version_ = version; }
  const std::string& service_version() const { return service_version_.value(); }
  bool hasServiceVersion() const { return service_version_.has_value(); }

  void setServiceGroup(const std::string& group) { group_ = group; }
  const std::string& service_group() const { return group_.value(); }
  bool hasServiceGroup() const { return group_.has_value(); }

  void setMessageType(MessageType type) { message_type_ = type; }
  MessageType message_type() const { return message_type_; }

  void setRequestId(int64_t id) { request_id_ = id; }
  int64_t request_id() const { return request_id_; }

  void setSerializationType(SerializationType type) { serialization_type_ = type; }
  SerializationType serialization_type() const { return serialization_type_; }

  void setResponseStatus(ResponseStatus status) { status_ = status; }
  ResponseStatus response_status() const { return status_; }

  void addParameterValue(uint32_t index, const std::string& type, const std::string& value) {
    if (!parameter_map_.has_value()) {
      parameter_map_ = std::make_unique<ParameterValueMap>();
    }
    parameter_map_.value()->emplace(index, ParameterValue(index, type, value));
  }
  const ParameterValue* getParameterValue(uint32_t index) const {
    if (parameter_map_.has_value()) {
      auto itor = parameter_map_.value()->find(index);
      if (itor != parameter_map_.value()->end())
        return &itor->second;
    }

    return nullptr;
  }
  bool hasParameters() const { return parameter_map_.has_value(); }
  size_t parameters_size() const {
    return parameter_map_.has_value() ? parameter_map_.value()->size() : 0;
  }

  bool hasHeaders() const { return headers_.has_value(); }
  const Http::HeaderMap& headers() const { return *(headers_.value()); }
  void addHeaderValue(const std::string& key, const std::string& value) {
    if (!headers_.has_value()) {
      headers_ = std::make_unique<Http::HeaderMapImpl>();
    }
    headers_.value()->addCopy(Http::LowerCaseString(key), value);
  }
  void addHeaderReferenceValue(const Http::LowerCaseString& key, const std::string& value) {
    if (!headers_.has_value()) {
      headers_ = std::make_unique<Http::HeaderMapImpl>();
    }
    headers_.value()->addReference(key, value);
  }
  size_t headers_size() const { return headers_.has_value() ? headers_.value()->size() : 0; }

private:
  MessageType message_type_{MessageType::Call};
  SerializationType serialization_type_{SerializationType::Hessian};
  ResponseStatus status_{ResponseStatus::Ok};

  int64_t request_id_ = 0;

  // Routing metadata.
  std::string service_name_;
  absl::optional<std::string> method_name_;
  absl::optional<std::string> service_version_;
  absl::optional<std::string> group_;
  absl::optional<ParameterValueMapPtr> parameter_map_;
  absl::optional<std::unique_ptr<Http::HeaderMapImpl>> headers_; // attachment
};

typedef std::shared_ptr<MessageMetadata> MessageMetadataSharedPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
