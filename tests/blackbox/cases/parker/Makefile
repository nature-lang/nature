.PHONY: all amd64 arm64 install package

TARGET_DIR=$(PWD)/build/parker/bin

all: amd64 arm64

amd64:
	@echo "Building for amd64 using nature build..."
	mkdir -p $(TARGET_DIR)
	npkg sync
	BUILD_OS=linux BUILD_ARCH=amd64 nature build -o $(TARGET_DIR)/parker parker.n
	BUILD_OS=linux BUILD_ARCH=amd64 nature build -o $(TARGET_DIR)/runner runner.n
	@make copy-version

arm64:
	@echo "Building for arm64 using go build..."
	mkdir -p $(TARGET_DIR)
	cd parkergo && go mod download
	cd parkergo && GOOS=linux GOARCH=arm64 go build -ldflags="-s -w" -o $(TARGET_DIR)/parker main.go
	cd parkergo && GOOS=linux GOARCH=arm64 go build -ldflags="-s -w" -o $(TARGET_DIR)/runner runner/main.go
	@make copy-version


install:
	@echo "Installing..."
	mkdir -p /usr/local/parker
	cp -r build/parker/* /usr/local/parker/

package:
	@echo "Packaging..."
	@test "$(ARCH)" || (echo "Error: ARCH is not set. Please set it like 'make package ARCH=amd64'" && exit 1)
	$(eval VERSION=$(shell cat VERSION))
	cd build && tar -czvf parker-$(VERSION)-linux-$(ARCH).tar.gz parker
	mv build/parker-$(VERSION)-linux-$(ARCH).tar.gz releases/
	@echo "Packaging done."


copy-version:
	@echo "Copying VERSION to build..."
	cp VERSION build/parker
	cp LICENSE build/parker
