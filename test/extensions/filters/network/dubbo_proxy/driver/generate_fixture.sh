#!/bin/bash

# Generates request and response fixtures for integration tests.

# Usage: generate_fixture.sh -s [service name] -m [method name]

set -e

function usage() {
	echo "Usage: $0 -s [service name] -m [method name]"
	exit 1
}

FIXTURE_DIR="${TEST_TMPDIR}"
mkdir -p "${FIXTURE_DIR}"

SERVICE="$2"
METHOD="$4"

if ! shift 4; then
    usage
fi

DRIVER_DIR="${TEST_RUNDIR}/test/extensions/filters/network/dubbo_proxy/driver"

JAVA_VERSION=${JAVA_VERSION:-1.8}
PLATFORM="$(uname -s | tr 'A-Z' 'a-z')"

PATHSEP=":"
case "${PLATFORM}" in
linux)
	# JAVA_HOME must point to a Java installation.
	JAVA_HOME="${JAVA_HOME:-$(readlink -f $(which javac) | sed 's_/bin/javac__')}"
	;;

freebsd)
	# JAVA_HOME must point to a Java installation.
	JAVA_HOME="${JAVA_HOME:-/usr/local/openjdk8}"
	;;

darwin)
	if [[ -z "$JAVA_HOME" ]]; then
		JAVA_HOME="$(/usr/libexec/java_home -v ${JAVA_VERSION}+ 2>/dev/null)" ||
			fail "Could not find JAVA_HOME, please ensure a JDK (version ${JAVA_VERSION}+) is installed."
	fi
	;;

msys* | mingw* | cygwin*)
	# Use a simplified platform string.
	PLATFORM="windows"
	PATHSEP=";"
	# Find the latest available version of the SDK.
	JAVA_HOME="${JAVA_HOME:-$(ls -d C:/Program\ Files/Java/jdk* | sort | tail -n 1)}"
	# Replace backslashes with forward slashes.
	JAVA_HOME="${JAVA_HOME//\\//}"
	;;
esac

echo $JAVA_HOME

FLAG_FILE="${TEST_TMPDIR}/flag"
rm -f "${FLAG_FILE}"

echo $FLAG_FILE

# start provider
$JAVA_HOME/bin/java -jar "${DRIVER_DIR}/org.apache.dubbo.samples.basic.BasicProvider.jar" "${TEST_TMPDIR}" 1>/dev/null 2>/dev/null &

SERVER_PID="$!"

trap "kill ${SERVER_PID}" EXIT

echo $SERVER_PID

if [[ ! -a "${DRIVER_DIR}/org.apache.dubbo.samples.basic.BasicProvider.jar" ]]; then
	echo "${DRIVER_DIR}/org.apache.dubbo.samples.basic.BasicProvider.jar"
fi

# wait for provider startup to complete
sleep 15

REQUEST_FILE="${FIXTURE_DIR}/${SERVICE}-${METHOD}-dubbo.request"
RESPONSE_FILE="${FIXTURE_DIR}/${SERVICE}-${METHOD}-dubbo.response"
rm -f "${REQUEST_FILE}"
rm -f "${RESPONSE_FILE}"

# start consumer
$JAVA_HOME/bin/java -jar "${DRIVER_DIR}/org.apache.dubbo.samples.basic.BasicConsumer.jar" "${REQUEST_FILE}" "${RESPONSE_FILE}" "${SERVICE}" "${METHOD}"
