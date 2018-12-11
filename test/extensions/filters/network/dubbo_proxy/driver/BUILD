licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")

envoy_package()

filegroup(
    name = "generate_fixture",
    srcs = ["generate_fixture.sh"],
    data = [
        ":consumer",
        ":provider",
    ],
)

filegroup(
    name = "consumer",
    srcs = ["org.apache.dubbo.samples.basic.BasicConsumer.jar"],
    data = [],
)

filegroup(
    name = "provider",
    srcs = ["org.apache.dubbo.samples.basic.BasicProvider.jar"],
    data = [],
)
