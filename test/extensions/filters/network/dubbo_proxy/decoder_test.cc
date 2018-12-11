#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::Test;
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DecoderStateMachineTestBase {
public:
  DecoderStateMachineTestBase() : metadata_(std::make_shared<MessageMetadata>()) {
    context_.header_size_ = 16;
  }
  virtual ~DecoderStateMachineTestBase() {}

  void initHandler() {
    EXPECT_CALL(decoder_callback_, newDecoderEventHandler())
        .WillOnce(Invoke([this]() -> DecoderEventHandler* { return &handler_; }));
  }

  void initProtocolDecoder(MessageType type, int32_t body_size, bool is_heartbeat = false) {
    EXPECT_CALL(protocol_, decode(_, _, _))
        .WillOnce(Invoke([=](Buffer::Instance&, Protocol::Context* context,
                             MessageMetadataSharedPtr metadata) -> bool {
          context->is_heartbeat_ = is_heartbeat;
          context->body_size_ = body_size;
          metadata->setMessageType(type);
          return true;
        }));
  }

  NiceMock<MockProtocol> protocol_;
  NiceMock<MockDeserializer> deserializer_;
  NiceMock<MockDecoderEventHandler> handler_;
  NiceMock<MockDecoderCallbacks> decoder_callback_;
  MessageMetadataSharedPtr metadata_;
  Protocol::Context context_;
};

class DecoderStateMachineTest : public DecoderStateMachineTestBase, public Test {};

class DecoderTest : public Test {
public:
  DecoderTest() {}
  virtual ~DecoderTest() override {}

  NiceMock<MockProtocol> protocol_;
  NiceMock<MockDeserializer> deserializer_;
  NiceMock<MockDecoderCallbacks> callbacks_;
};

TEST_F(DecoderStateMachineTest, EmptyData) {
  EXPECT_CALL(protocol_, decode(_, _, _)).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(0);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(0);

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
}

TEST_F(DecoderStateMachineTest, OnlyHaveHeaderData) {
  initHandler();
  initProtocolDecoder(MessageType::Call, 1, false);

  EXPECT_CALL(handler_, transportBegin()).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(handler_, messageEnd(_)).Times(0);

  Buffer::OwnedImpl buffer;
  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
}

TEST_F(DecoderStateMachineTest, RequestMessageCallbacks) {
  initHandler();
  initProtocolDecoder(MessageType::Call, 0, false);

  EXPECT_CALL(handler_, transportBegin()).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(handler_, messageEnd(_)).Times(1);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(1);
  EXPECT_CALL(handler_, transportEnd()).Times(1);

  EXPECT_CALL(deserializer_, deserializeRpcInvocation(_, _, _)).WillOnce(Return());

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
}

TEST_F(DecoderStateMachineTest, ResponseMessageCallbacks) {
  initHandler();
  initProtocolDecoder(MessageType::Reply, 0, false);

  EXPECT_CALL(handler_, transportBegin()).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(handler_, messageEnd(_)).Times(1);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(1);
  EXPECT_CALL(handler_, transportEnd()).Times(1);

  EXPECT_CALL(deserializer_, deserializeRpcResult(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, size_t) -> RpcResultPtr {
        return std::make_unique<RpcResultImpl>(false);
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
}

TEST_F(DecoderStateMachineTest, DeserializeRpcInvocationException) {
  initHandler();
  initProtocolDecoder(MessageType::Call, 0, false);

  EXPECT_CALL(handler_, messageEnd(_)).Times(0);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(0);
  EXPECT_CALL(handler_, transportEnd()).Times(0);

  EXPECT_CALL(deserializer_, deserializeRpcInvocation(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, int32_t, MessageMetadataSharedPtr) -> void {
        throw EnvoyException(fmt::format("mock deserialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock deserialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::onMessageEnd);
}

TEST_F(DecoderStateMachineTest, DeserializeRpcResultException) {
  initHandler();
  initProtocolDecoder(MessageType::Reply, 0, false);

  EXPECT_CALL(handler_, messageEnd(_)).Times(0);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(0);
  EXPECT_CALL(handler_, transportEnd()).Times(0);

  EXPECT_CALL(deserializer_, deserializeRpcResult(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, size_t) -> RpcResultPtr {
        throw EnvoyException(fmt::format("mock deserialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock deserialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::onMessageEnd);
}

TEST_F(DecoderStateMachineTest, ProtocolDecodeException) {
  EXPECT_CALL(decoder_callback_, newDecoderEventHandler()).Times(0);
  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, Protocol::Context*, MessageMetadataSharedPtr) -> bool {
        throw EnvoyException(fmt::format("mock deserialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock deserialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::onTransportBegin);
}

TEST_F(DecoderTest, NeedMoreDataForProtocolHeader) {
  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, Protocol::Context*, MessageMetadataSharedPtr) -> bool {
        return false;
      }));
  EXPECT_CALL(callbacks_, newDecoderEventHandler()).Times(0);

  Decoder decoder(protocol_, deserializer_, &callbacks_);

  Buffer::OwnedImpl buffer;
  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), Network::FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

TEST_F(DecoderTest, NeedMoreDataForProtocolBody) {
  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, Protocol::Context* context,
                          MessageMetadataSharedPtr metadata) -> bool {
        metadata->setMessageType(MessageType::Call);
        context->body_size_ = 10;
        return true;
      }));
  EXPECT_CALL(callbacks_, newDecoderEventHandler()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transportBegin()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageEnd(_)).Times(0);
  EXPECT_CALL(callbacks_.handler_, transferBodyTo(_, _)).Times(0);
  EXPECT_CALL(callbacks_.handler_, transportEnd()).Times(0);

  Decoder decoder(protocol_, deserializer_, &callbacks_);

  Buffer::OwnedImpl buffer;
  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), Network::FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

TEST_F(DecoderTest, decodeResponseMessage) {
  Buffer::OwnedImpl buffer;
  buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));

  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, Protocol::Context* context,
                           MessageMetadataSharedPtr metadata) -> bool {
        metadata->setMessageType(MessageType::Reply);
        context->body_size_ = buffer.length();
        return true;
      }));
  EXPECT_CALL(deserializer_, deserializeRpcResult(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, size_t) -> RpcResultPtr {
        return std::make_unique<RpcResultImpl>(true);
      }));
  EXPECT_CALL(callbacks_, newDecoderEventHandler()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transportBegin()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageEnd(_)).Times(1);
  EXPECT_CALL(callbacks_.handler_, transferBodyTo(_, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, transportEnd()).Times(1);

  Decoder decoder(protocol_, deserializer_, &callbacks_);

  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), Network::FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
