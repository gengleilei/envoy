#include "extensions/filters/network/dubbo_proxy/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "extensions/filters/network/dubbo_proxy/conn_manager_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Network::FilterFactoryCb DubboProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Config> filter_config(new ConfigImpl(proto_config, context));

  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ConnectionManager>(
        *filter_config, context.random(), context.dispatcher().timeSystem()));
  };
}

/**
 * Static registration for the dubbo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<DubboProxyFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
