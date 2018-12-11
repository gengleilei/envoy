#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using testing::StrictMock;

TEST(DubboProtocolImplTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  DubboProtocolImpl dubbo_protocol;
  Protocol::Context context;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context, metadata));
  buffer.add(std::string(15, 0x00));
  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context, metadata));
}

TEST(DubboProtocolImplTest, Name) {
  DubboProtocolImpl dubbo_protocol;
  EXPECT_EQ(dubbo_protocol.name(), "dubbo");
}

TEST(DubboProtocolImplTest, Normal) {
  DubboProtocolImpl dubbo_protocol;
  Protocol::Context context;
  // Normal dubbo request message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, metadata));
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(1, context.body_size_);
    EXPECT_EQ(false, context.is_heartbeat_);
    EXPECT_EQ(MessageType::Call, metadata->message_type());
  }

  // Normal dubbo response message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', 0x42, 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, metadata));
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(1, context.body_size_);
    EXPECT_EQ(false, context.is_heartbeat_);
    EXPECT_EQ(MessageType::Reply, metadata->message_type());
  }
}

TEST(DubboProtocolImplTest, InvalidProtocol) {
  DubboProtocolImpl dubbo_protocol;
  Protocol::Context context;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();

  // Invalid dubbo magic number
  {
    Buffer::OwnedImpl buffer;
    addInt64(buffer, 0);
    addInt64(buffer, 0);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message magic number 0");
  }

  // Invalid message size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, DubboProtocolImpl::MaxBodySize + 1);
    std::string exception_string =
        fmt::format("invalid dubbo message size {}", DubboProtocolImpl::MaxBodySize + 1);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              exception_string);
  }

  // Invalid serialization type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message serialization type 3");
  }

  // Invalid response status
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x42, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message response status 0");
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy