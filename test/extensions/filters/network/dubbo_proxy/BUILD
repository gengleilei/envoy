licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

envoy_package()

envoy_cc_mock(
    name = "mocks_lib",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:deserializer_interface",
        "//source/extensions/filters/network/dubbo_proxy:protocol_interface",
        "//source/extensions/filters/network/dubbo_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_extension_cc_test(
    name = "hessian_utils_test",
    srcs = ["hessian_utils_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_utils_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "dubbo_protocol_impl_test",
    srcs = ["dubbo_protocol_impl_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "hessian_deserializer_impl_test",
    srcs = ["hessian_deserializer_impl_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_deserializer_impl_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "conn_manager_config_test",
    srcs = ["conn_manager_config_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//source/extensions/filters/network/dubbo_proxy:conn_manager_config_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_config_interface",
        "//test/mocks/server:server_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/filter/network/dubbo_proxy/v2alpha1:dubbo_proxy_cc",
    ],
)

envoy_extension_cc_test(
    name = "conn_manager_test",
    srcs = ["conn_manager_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//source/extensions/filters/network/dubbo_proxy:conn_manager_config_lib",
        "//source/extensions/filters/network/dubbo_proxy:conn_manager_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_deserializer_impl_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "route_matcher_test",
    srcs = ["route_matcher_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//source/extensions/filters/network/dubbo_proxy/router:router_matcher",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        "//source/extensions/filters/network/dubbo_proxy:app_exception_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_deserializer_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:registry_lib",
    ],
)

envoy_extension_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)

envoy_extension_cc_test(
    name = "decoder_test",
    srcs = ["decoder_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:decoder_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)

envoy_extension_cc_test(
    name = "app_exception_test",
    srcs = ["app_exception_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:app_exception_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_deserializer_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)

envoy_extension_cc_test_library(
    name = "integration_lib",
    srcs = ["integration.cc"],
    hdrs = ["integration.h"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//source/extensions/filters/network/dubbo_proxy:conn_manager_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/filters/network/dubbo_proxy/driver:generate_fixture",
    ],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":integration_lib",
        ":utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
    ],
)
