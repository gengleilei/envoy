#include "extensions/filters/network/dubbo_proxy/conn_manager_config.h"

#include "common/common/utility.h"
#include "common/config/utility.h"

#include "extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "extensions/filters/network/dubbo_proxy/filters/well_known_names.h"
#include "extensions/filters/network/dubbo_proxy/stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

namespace {

using ProtoSerializationType =
    envoy::config::filter::network::dubbo_proxy::v2alpha1::SerializationType;
using ProtoProtocolType = envoy::config::filter::network::dubbo_proxy::v2alpha1::ProtocolType;

typedef std::map<ProtoSerializationType, DeserializerType> DeserializerTypeMap;

static const DeserializerTypeMap& deserializerTypeMap() {
  CONSTRUCT_ON_FIRST_USE(DeserializerTypeMap,
                         {
                             {ProtoSerializationType::Hessian2, DeserializerType::Hessian},
                         });
}

typedef std::map<ProtoProtocolType, ProtocolType> ProtocolTypeMap;

static const ProtocolTypeMap& protocolTypeMap() {
  CONSTRUCT_ON_FIRST_USE(ProtocolTypeMap, {
                                              {ProtoProtocolType::Dubbo, ProtocolType::Dubbo},
                                              {ProtoProtocolType::HSF, ProtocolType::HSF},
                                          });
}

DeserializerType lookupDeserializer(ProtoSerializationType type) {
  const auto& iter = deserializerTypeMap().find(type);
  if (iter == deserializerTypeMap().end()) {
    throw EnvoyException(fmt::format(
        "unknown deserializer {}",
        envoy::config::filter::network::dubbo_proxy::v2alpha1::SerializationType_Name(type)));
  }

  return iter->second;
}

ProtocolType lookupProtocol(ProtoProtocolType protocol) {
  const auto& iter = protocolTypeMap().find(protocol);
  if (iter == protocolTypeMap().end()) {
    throw EnvoyException(fmt::format(
        "unknown protocol {}",
        envoy::config::filter::network::dubbo_proxy::v2alpha1::ProtocolType_Name(protocol)));
  }
  return iter->second;
}

} // namespace

ConfigImpl::ConfigImpl(const DubboProxyConfig& config,
                       Server::Configuration::FactoryContext& context)
    : context_(context), stats_prefix_(fmt::format("dubbo.{}.", config.stat_prefix())),
      stats_(DubboFilterStats::generateStats(stats_prefix_, context_.scope())),
      deserializer_type_(lookupDeserializer(config.serialization_type())),
      protocol_type_(lookupProtocol(config.protocol_type())),
      route_matcher_(new Router::MultiRouteMatcher(config.route_config())) {
  if (config.dubbo_filters().empty()) {
    ENVOY_LOG(debug, "using default router filter");

    envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboFilter router_config;
    router_config.set_name(DubboFilters::DubboFilterNames::get().ROUTER);
    registerFilter(router_config);
  } else {
    for (const auto& filter_config : config.dubbo_filters()) {
      registerFilter(filter_config);
    }
  }
}

void ConfigImpl::createFilterChain(DubboFilters::FilterChainFactoryCallbacks& callbacks) {
  for (const DubboFilters::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

Router::RouteConstSharedPtr ConfigImpl::route(const MessageMetadata& metadata,
                                              uint64_t random_value) const {
  return route_matcher_->route(metadata, random_value);
}

ProtocolPtr ConfigImpl::createProtocol() {
  return NamedProtocolConfigFactory::getFactory(protocol_type_).createProtocol();
}

DeserializerPtr ConfigImpl::createDeserializer() {
  return NamedDeserializerConfigFactory::getFactory(deserializer_type_).createDeserializer();
}

void ConfigImpl::registerFilter(const DubboFilterConfig& proto_config) {
  const ProtobufTypes::String& string_name = proto_config.name();

  ENVOY_LOG(debug, "    dubbo filter #{}", filter_factories_.size());
  ENVOY_LOG(debug, "      name: {}", string_name);

  const Json::ObjectSharedPtr filter_config =
      MessageUtil::getJsonObjectFromMessage(proto_config.config());
  ENVOY_LOG(debug, "      config: {}", filter_config->asJsonString());

  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<DubboFilters::NamedDubboFilterConfigFactory>(
          string_name);

  ProtobufTypes::MessagePtr message =
      Envoy::Config::Utility::translateToFactoryConfig(proto_config, factory);
  DubboFilters::FilterFactoryCb callback =
      factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);

  filter_factories_.push_back(callback);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
