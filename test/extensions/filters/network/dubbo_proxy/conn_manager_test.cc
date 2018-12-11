#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "extensions/filters/network/dubbo_proxy/conn_manager_config.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using ConfigDubboProxy = envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy;

class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(ConfigDubboProxy proto_config, Server::Configuration::MockFactoryContext& context,
                 DubboFilters::DecoderFilterSharedPtr decoder_filter, DubboFilterStats& stats)
      : ConfigImpl(proto_config, context), decoder_filter_(decoder_filter), stats_(stats) {}

  // ConfigImpl
  DubboFilterStats& stats() override { return stats_; }
  void createFilterChain(DubboFilters::FilterChainFactoryCallbacks& callbacks) override {
    if (custom_filter_) {
      callbacks.addDecoderFilter(custom_filter_);
    }
    callbacks.addDecoderFilter(decoder_filter_);
  }

  DeserializerPtr createDeserializer() override {
    if (deserializer_) {
      return DeserializerPtr{deserializer_};
    }
    return ConfigImpl::createDeserializer();
  }

  ProtocolPtr createProtocol() override {
    if (protocol_) {
      return ProtocolPtr{protocol_};
    }
    return ConfigImpl::createProtocol();
  }

  DubboFilters::DecoderFilterSharedPtr custom_filter_;
  DubboFilters::DecoderFilterSharedPtr decoder_filter_;
  DubboFilterStats& stats_;
  MockDeserializer* deserializer_{};
  MockProtocol* protocol_{};
};

class ConnectionManagerTest : public testing::Test {
public:
  ConnectionManagerTest() : stats_(DubboFilterStats::generateStats("test.", store_)) {}
  ~ConnectionManagerTest() { filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList(); }

  TimeSource& timeSystem() { return factory_context_.dispatcher().timeSystem(); }

  void initializeFilter() { initializeFilter(""); }

  void initializeFilter(const std::string& yaml) {
    for (const auto& counter : store_.counters()) {
      counter->reset();
    }

    if (!yaml.empty()) {
      MessageUtil::loadFromYaml(yaml, proto_config_);
      MessageUtil::validate(proto_config_);
    }

    proto_config_.set_stat_prefix("test");
    decoder_filter_.reset(new NiceMock<DubboFilters::MockDecoderFilter>());
    config_ =
        std::make_unique<TestConfigImpl>(proto_config_, factory_context_, decoder_filter_, stats_);
    if (custom_deserializer_) {
      config_->deserializer_ = custom_deserializer_;
    }
    if (custom_protocol_) {
      config_->protocol_ = custom_protocol_;
    }
    if (custom_filter_) {
      config_->custom_filter_ = custom_filter_;
    }

    decoder_event_handler_.reset(new NiceMock<MockDecoderEventHandler>());

    ON_CALL(random_, random()).WillByDefault(Return(42));
    filter_ = std::make_unique<ConnectionManager>(
        *config_, random_, filter_callbacks_.connection_.dispatcher_.timeSystem());
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void writeHessianErrorResponseMessage(Buffer::Instance& buffer, bool is_event,
                                        int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x46});                   // Response status
    addInt64(buffer, request_id);                    // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x01}); // Body Length
  }

  void writeHessianExceptionResponseMessage(Buffer::Instance& buffer, bool is_event,
                                            int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                      // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,     // Body Length
                           '\x90',                     // return type, exception
                           0x05, 't', 'e', 's', 't'}); // return body
  }

  void writeInvalidResponseMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', 0x43, 0x14, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00, 0x06,                         // Body Length
        '\x94',                                             // return type
        0x05,   't',    'e',  's',  't',                    // return body
    });
  }

  void writeInvalidRequestMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', '\xc3', 0x00, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00,   0x16,                         // Body Length
        0x05,   '2',    '.',    '0',  '.',  '2',              // Dubbo version
        0x04,   't',    'e',    's',  't',                    // Service naem
        0x05,   '0',    '.',    '0',  '.',  '0',              // Service version
        0x04,   't',    'e',    's',  't',                    // method name
    });
  }

  void writePartialHessianResponseMessage(Buffer::Instance& buffer, bool is_event,
                                          int64_t request_id, bool start) {

    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x14});
      addInt64(buffer, request_id);                  // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x06, // Body Length
                             '\x94'});               // return type, exception
    } else {
      buffer.add(std::string{0x05, 't', 'e', 's', 't'}); // return body
    }
  }

  void writeHessianResponseMessage(Buffer::Instance& buffer, bool is_event, int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                              // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,             // Body Length
                           '\x94', 0x05, 't', 'e', 's', 't'}); // return type, exception
  }

  void writePartialHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                         int64_t request_id, bool start) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x00});
      addInt64(buffer, request_id);                           // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x16,          // Body Length
                             0x05, '2', '.', '0', '.', '2'}); // Dubbo version
    } else {
      buffer.add(std::string{
          0x04, 't', 'e', 's', 't',      // Service naem
          0x05, '0', '.', '0', '.', '0', // Service version
          0x04, 't', 'e', 's', 't',      // method name
      });
    }
  }

  void writeHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                  int64_t request_id) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x00});
    addInt64(buffer, request_id);                            // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x16,           // Body Length
                           0x05, '2',  '.',  '0',  '.', '2', // Dubbo version
                           0x04, 't',  'e',  's',  't',      // Service name
                           0x05, '0',  '.',  '0',  '.', '0', // Service version
                           0x04, 't',  'e',  's',  't'});    // method name
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::shared_ptr<DubboFilters::MockDecoderFilter> decoder_filter_;
  std::shared_ptr<MockDecoderEventHandler> decoder_event_handler_;
  Stats::IsolatedStoreImpl store_;
  DubboFilterStats stats_;
  ConfigDubboProxy proto_config_;

  std::unique_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<ConnectionManager> filter_;
  MockDeserializer* custom_deserializer_{};
  MockProtocol* custom_protocol_{};
  DubboFilters::DecoderFilterSharedPtr custom_filter_;
};

TEST_F(ConnectionManagerTest, OnDataHandlesRequestTwoWay) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesRequestOneWay) {
  initializeFilter();
  bool one_way = true;
  writeHessianRequestMessage(buffer_, one_way, false, 0x0F);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesRequestEvent) {
  initializeFilter();
  bool event = true;
  writeHessianRequestMessage(buffer_, false, event, 0x0F);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_event").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesMessageSplitAcrossBuffers) {
  initializeFilter();
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());

  // Complete the buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, false);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesProtocolError) {
  initializeFilter();
  writeInvalidRequestMessage(buffer_);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0, buffer_.length());

  // Sniffing is now disabled.
  bool one_way = true;
  writeHessianRequestMessage(buffer_, one_way, false, 0x0F);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesProtocolErrorOnWrite) {
  initializeFilter();

  // Start the read buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);
  uint64_t len = buffer_.length();

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  len -= buffer_.length();

  // Disable sniffing
  writeInvalidRequestMessage(write_buffer_);

  DubboProtocolImpl protocol;
  HessianDeserializerImpl deserializer;
  callbacks->startUpstreamResponse(deserializer, protocol);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_NE(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

TEST_F(ConnectionManagerTest, OnDataStopsSniffingWithTooManyPendingCalls) {
  initializeFilter();
  for (int i = 0; i < 64; i++) {
    writeHessianRequestMessage(buffer_, false, false, i);
  }

  EXPECT_CALL(*decoder_filter_, messageEnd(_)).Times(64);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(64U, store_.gauge("test.request_active").value());

  // Sniffing is now disabled.
  writeInvalidRequestMessage(buffer_);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, OnWriteHandlesResponse) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeHessianResponseMessage(write_buffer_, false, 0x0F);

  DubboProtocolImpl protocol;
  HessianDeserializerImpl deserializer;
  callbacks->startUpstreamResponse(deserializer, protocol);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, OnWriteHandlesResponseError) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  writeHessianExceptionResponseMessage(write_buffer_, false, 1);

  DubboProtocolImpl protocol;
  HessianDeserializerImpl deserializer;
  callbacks->startUpstreamResponse(deserializer, protocol);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, OnWriteHandlesResponseException) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());

  writeInvalidRequestMessage(write_buffer_);

  DubboProtocolImpl protocol;
  HessianDeserializerImpl deserializer;
  callbacks->startUpstreamResponse(deserializer, protocol);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

// Tests stop iteration/resume with multiple filters.
TEST_F(ConnectionManagerTest, OnDataResumesWithNextFilter) {
  auto* filter = new NiceMock<DubboFilters::MockDecoderFilter>();
  custom_filter_.reset(filter);

  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  // First filter stops iteration.
  {
    EXPECT_CALL(*filter, transportBegin()).WillOnce(Return(Network::FilterStatus::StopIteration));
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(0U, store_.counter("test.request").value());
    EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  }

  // Resume processing.
  {
    InSequence s;
    EXPECT_CALL(*decoder_filter_, transportBegin())
        .WillOnce(Return(Network::FilterStatus::Continue));
    EXPECT_CALL(*filter, messageEnd(_)).WillOnce(Return(Network::FilterStatus::Continue));
    EXPECT_CALL(*decoder_filter_, messageEnd(_)).WillOnce(Return(Network::FilterStatus::Continue));
    callbacks->continueDecoding();
  }

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
}

// Tests multiple filters are invoked in the correct order.
TEST_F(ConnectionManagerTest, OnDataHandlesDubboCallWithMultipleFilters) {
  auto* filter = new NiceMock<DubboFilters::MockDecoderFilter>();
  custom_filter_.reset(filter);
  initializeFilter();

  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  InSequence s;
  EXPECT_CALL(*filter, transportBegin()).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*decoder_filter_, transportBegin()).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*filter, messageEnd(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*decoder_filter_, messageEnd(_)).WillOnce(Return(Network::FilterStatus::Continue));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, PipelinedRequestAndResponse) {
  initializeFilter();

  writeHessianRequestMessage(buffer_, false, false, 1);
  writeHessianRequestMessage(buffer_, false, false, 2);

  std::list<DubboFilters::DecoderFilterCallbacks*> callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillRepeatedly(Invoke(
          [&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks.push_back(&cb); }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(2U, store_.gauge("test.request_active").value());
  EXPECT_EQ(2U, store_.counter("test.request").value());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  DubboProtocolImpl protocol;
  HessianDeserializerImpl deserializer;

  writeHessianResponseMessage(write_buffer_, false, 0x01);
  callbacks.front()->startUpstreamResponse(deserializer, protocol);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());

  writeHessianResponseMessage(write_buffer_, false, 0x02);
  callbacks.front()->startUpstreamResponse(deserializer, protocol);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_success").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, ResetDownstreamConnection) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->resetDownstreamConnection();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, OnEvent) {
  // No active calls
  {
    initializeFilter();
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Remote close mid-request
  {
    initializeFilter();

    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Local close mid-request
  {
    initializeFilter();
    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Remote close before response
  {
    initializeFilter();
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Local close before response
  {
    initializeFilter();
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }
}

TEST_F(ConnectionManagerTest, ResponseWithUnknownSequenceID) {
  initializeFilter();

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  writeHessianResponseMessage(write_buffer_, false, 10);

  DubboProtocolImpl protocol;
  HessianDeserializerImpl deserializer;
  callbacks->startUpstreamResponse(deserializer, protocol);

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

TEST_F(ConnectionManagerTest, OnDataWithFilterSendsLocalReply) {
  auto* filter = new NiceMock<DubboFilters::MockDecoderFilter>();
  custom_filter_.reset(filter);

  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&, Deserializer&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::SuccessReply;
      }));

  // First filter sends local reply.
  EXPECT_CALL(*filter, messageEnd(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr) -> Network::FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return Network::FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(fake_response, buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ConnectionManagerTest, OnDataWithFilterSendsLocalErrorReply) {
  auto* filter = new NiceMock<DubboFilters::MockDecoderFilter>();
  custom_filter_.reset(filter);

  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&, Deserializer&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));

  // First filter sends local reply.
  EXPECT_CALL(*filter, messageEnd(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr) -> Network::FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return Network::FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(fake_response, buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
