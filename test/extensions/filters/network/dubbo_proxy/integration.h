#pragma once

#include <string>
#include <vector>

#include "test/integration/integration.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * DriverMode represents the modes the test driver server modes.
 */
enum class DriverMode {
  // Server returns successful responses.
  Success,

  // Server throws application exceptions.
  Exception,
};

struct PayloadOptions {
  PayloadOptions(std::string service_name, std::string method_name, bool is_oneway = false,
                 std::vector<std::string> method_args = {},
                 std::vector<std::pair<std::string, std::string>> headers = {})
      : service_name_(service_name), method_name_(method_name), is_oneway_(is_oneway),
        method_args_(method_args), headers_(headers) {}

  std::string modeName() const;

  // const DriverMode mode_;
  const std::string service_name_;
  const std::string method_name_;
  const bool is_oneway_;
  const std::vector<std::string> method_args_;
  const std::vector<std::pair<std::string, std::string>> headers_;
};

class BaseDubboIntegrationTest : public BaseIntegrationTest {
public:
  BaseDubboIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, realTime(), dubbo_config_) {}

  /**
   * Given PayloadOptions, generate a client request and server response and store the
   * data in the given Buffers.
   */
  void preparePayloads(const PayloadOptions& options, Buffer::Instance& request_buffer,
                       Buffer::Instance& response_buffer);

protected:
  // Tests should use a static SetUpTestCase method to initialize this field with a suitable
  // configuration.
  static std::string dubbo_config_;

private:
  void readAll(std::string file, Buffer::Instance& buffer);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
