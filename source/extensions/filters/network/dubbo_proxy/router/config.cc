#include "extensions/filters/network/dubbo_proxy/router/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/network/dubbo_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

DubboFilters::FilterFactoryCb RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::dubbo::router::v2alpha1::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(proto_config);
  UNREFERENCED_PARAMETER(stat_prefix);

  return [&context](DubboFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Router>(context.clusterManager()));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RouterFilterConfig, DubboFilters::NamedDubboFilterConfigFactory>
    register_;

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
