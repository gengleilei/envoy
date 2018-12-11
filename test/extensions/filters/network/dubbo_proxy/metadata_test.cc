#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(MessageMetadataTest, Fields) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasMethodName());
  EXPECT_THROW(metadata.method_name(), absl::bad_optional_access);
  metadata.setMethodName("method");
  EXPECT_TRUE(metadata.hasMethodName());
  EXPECT_EQ("method", metadata.method_name());

  EXPECT_FALSE(metadata.hasServiceVersion());
  EXPECT_THROW(metadata.service_version(), absl::bad_optional_access);
  metadata.setServiceVersion("1.0.0");
  EXPECT_TRUE(metadata.hasServiceVersion());
  EXPECT_EQ("1.0.0", metadata.service_version());

  EXPECT_FALSE(metadata.hasServiceGroup());
  EXPECT_THROW(metadata.service_group(), absl::bad_optional_access);
  metadata.setServiceGroup("group");
  EXPECT_TRUE(metadata.hasServiceGroup());
  EXPECT_EQ("group", metadata.service_group());
}

TEST(MessageMetadataTest, Headers) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasHeaders());
  metadata.addHeaderValue("k", "v");
  EXPECT_EQ(metadata.headers_size(), 1);
}

TEST(MessageMetadataTest, Parameters) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasParameters());
  metadata.addParameterValue(0, "string", "test");
  EXPECT_TRUE(metadata.hasParameters());
  EXPECT_EQ(metadata.parameters_size(), 1);
  auto parameter = metadata.getParameterValue(0);
  EXPECT_EQ(parameter->type_, "string");
  EXPECT_EQ(parameter->value_, "test");
  EXPECT_EQ(metadata.getParameterValue(1), nullptr);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
