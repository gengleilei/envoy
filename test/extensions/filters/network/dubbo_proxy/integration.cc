#include "test/extensions/filters/network/dubbo_proxy/integration.h"

#include <algorithm>
#include <fstream>

#include "common/filesystem/filesystem_impl.h"

#include "test/test_common/environment.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

const std::string RequestFilePath = "/tmp/envoy_test_dubbo/data.request";
const std::string ResponseFilePath = "/tmp/envoy_test_dubbo/data.response";

// std::string PayloadOptions::modeName() const {
//   switch (mode_) {
//   case DriverMode::Success:
//     return "success";
//   case DriverMode::Exception:
//     return "exception";
//   default:
//     NOT_REACHED_GCOVR_EXCL_LINE;
//   }
// }

std::string BaseDubboIntegrationTest::dubbo_config_;

void BaseDubboIntegrationTest::preparePayloads(const PayloadOptions& options,
                                               Buffer::Instance& request_buffer,
                                               Buffer::Instance& response_buffer) {
  std::vector<std::string> args = {
      TestEnvironment::runfilesPath(
          "test/extensions/filters/network/dubbo_proxy/driver/generate_fixture.sh"),
  };

  std::stringstream file_base;
  file_base << "{{ test_tmpdir }}/";
  file_base << options.service_name_ << "-";
  file_base << options.method_name_ << "-";
  std::string request_file = file_base.str() + "dubbo.request";
  std::string response_file = file_base.str() + "dubbo.response";

  args.push_back("-s");
  args.push_back(options.service_name_);

  args.push_back("-m");
  args.push_back(options.method_name_);

  TestEnvironment::exec(args);

  readAll(request_file, request_buffer);
  if (!options.is_oneway_) {
    readAll(response_file, response_buffer);
  }
}

void BaseDubboIntegrationTest::readAll(std::string file, Buffer::Instance& buffer) {
  file = TestEnvironment::substitute(file, version_);

  std::string data = Filesystem::fileReadToEnd(file);
  buffer.add(data);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
