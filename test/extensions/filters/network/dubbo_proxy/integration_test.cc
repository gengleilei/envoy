#include "test/extensions/filters/network/dubbo_proxy/integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::Combine;
using testing::Test;
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DubboConnManagerIntegrationTest : public BaseDubboIntegrationTest, public Test {
public:
  static void SetUpTestCase() {
    dubbo_config_ = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.dubbo_proxy
          config:
            stat_prefix: dubbo_stats
            protocol_type: Dubbo
            serialization_type: Hessian2
            route_config:
              - name: test1
                interface: org.apache.dubbo.samples.basic.api.DemoService
                version: "0.0.0"
                routes:
                  - match:
                      method:
                        name: sayHello
                    route:
                        cluster: "cluster_0"
                  - match:
                      method:
                        name: sayBye
                    route:
                        cluster: "cluster_0"
              - name: test2
                interface: org.apache.dubbo.samples.basic.api.DemoQueryService
                version: "0.0.0"
                routes:
                  - match:
                      method:
                        name: queryName
                    route:
                        cluster: "cluster_0"
                  - match:
                      method:
                        name: queryAge
                    route:
                        cluster: "cluster_0"
                  - match:
                      method:
                        name: queryEmail
                    route:
                        cluster: "cluster_0"
              - name: test3
                interface: org.apache.dubbo.samples.basic.api.DemoExceptionService
                version: "0.0.0"
                routes:
                  - match:
                      method:
                        name: generateBizException
                    route:
                        cluster: "cluster_0"
                  - match:
                      method:
                        name: reportError
                    route:
                        cluster: "cluster_0"
      )EOF";
  }

  void initializeCall(const std::string& service, const std::string& method) {
    PayloadOptions options(service, method);
    preparePayloads(options, request_bytes_, response_bytes_);
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() > 0);
    initializeCommon();
  }

  void initializeOneway(const std::string& service, const std::string& method) {
    PayloadOptions options(service, method, true);
    preparePayloads(options, request_bytes_, response_bytes_);
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() == 0);
    initializeCommon();
  }

  // We allocate as many upstreams as there are clusters, with each upstream being allocated
  // to clusters in the order they're defined in the bootstrap config.
  void initializeCommon() {
    const int upstream_count = 1;
    setUpstreamCount(upstream_count);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      for (int i = 1; i < upstream_count; i++) {
        auto* c = bootstrap.mutable_static_resources()->add_clusters();
        c->MergeFrom(bootstrap.static_resources().clusters()[0]);
        c->set_name(fmt::format("cluster_{}", i));
      }
    });

    BaseDubboIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void Execute() {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
    tcp_client->write(request_bytes_.toString());

    FakeRawConnectionPtr fake_upstream_connection;
    FakeUpstream* expected_upstream = getExpectedUpstream();
    ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
    std::string data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
    Buffer::OwnedImpl upstream_request(data);
    EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

    ASSERT_TRUE(fake_upstream_connection->write(response_bytes_.toString()));

    tcp_client->waitForData(response_bytes_.toString());
    tcp_client->close();

    EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

    Stats::CounterSharedPtr counter = test_server_->counter("dubbo.dubbo_stats.request_twoway");
    EXPECT_NE(nullptr, counter);
    EXPECT_EQ(1U, counter->value());
    counter = test_server_->counter("dubbo.dubbo_stats.response_success");
    EXPECT_EQ(1U, counter->value());
  }

protected:
  FakeUpstream* getExpectedUpstream() { return fake_upstreams_[0].get(); }

  bool multiplexed_;

  std::string result_;

  Buffer::OwnedImpl request_bytes_;
  Buffer::OwnedImpl response_bytes_;
};

TEST_F(DubboConnManagerIntegrationTest, CallSayHelloMethod) {
  initializeCall("demoService", "sayHello");
  Execute();
}

TEST_F(DubboConnManagerIntegrationTest, CallSayByeMethod) {
  initializeCall("demoService", "sayBye");
  Execute();
}

TEST_F(DubboConnManagerIntegrationTest, CallQueryAgeMethod) {
  initializeCall("demoQueryService", "queryAge");
  Execute();
}

TEST_F(DubboConnManagerIntegrationTest, CallQueryEmailMethod) {
  initializeCall("demoQueryService", "queryEmail");
  Execute();
}

TEST_F(DubboConnManagerIntegrationTest, BizException) {
  initializeCall("demoExceptionService", "generateBizException");
  Execute();
}

TEST_F(DubboConnManagerIntegrationTest, OneWay) {
  initializeOneway("demoExceptionService", "reportError");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeUpstream* expected_upstream = getExpectedUpstream();
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_TRUE(TestUtility::buffersEqual(upstream_request, request_bytes_));
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  tcp_client->close();

  Stats::CounterSharedPtr counter = test_server_->counter("dubbo.dubbo_stats.request_oneway");
  EXPECT_EQ(1U, counter->value());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
