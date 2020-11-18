ARCH?=armhf

# modbus app
MODBUS_VERSION=1.2.2
DRONE_BUILD_NUMBER ?= unknown
BUILD_NUMBER       ?= $(DRONE_BUILD_NUMBER)
VERSION?=$(MODBUS_VERSION)-$(BUILD_NUMBER)


.PHONY: build-sample-protocol pack-sample-protocol clean-img

build-sample-protocol:
	docker build \
		--rm \
		-f app/Dockerfile.$(ARCH) \
		-t mxswdc2/fieldbusprotocol:$(VERSION)-$(ARCH) \
		.

pack-sample-protocol:
	./tdk pack -e ARCH=$(ARCH) -e VERSION=$(VERSION) -w app


clean-img:
	docker rmi mxswdc2/fieldbusprotocol:$(VERSION)-$(ARCH)
