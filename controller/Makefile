.PHONY: controller clean 

ARCH               ?= amd64
GO                  = CGO_ENABLED=0 go
GOCGO               = CGO_ENABLED=1 go

ifeq ($(ARCH),armhf)
GO                 := GOOS=linux GOARCH=arm GOARM=7 $(GO)
GOCGO              := CC=arm-linux-gnueabihf-gcc GOOS=linux GOARCH=arm GOARM=7 $(GOCGO)
endif

LDFLAGS            += -s -w
GOFLAGS             = -ldflags "$(LDFLAGS)"

fbcontroller:
	$(GOCGO) build $(GOFLAGS) -o ./bin/fbcontroller ./main.go

clean:
	rm -rf ./bin
