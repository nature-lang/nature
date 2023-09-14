.PHONY: all linux_amd64 linux_arm64 copy-version-linux_amd64 copy-version-linux_arm64

all: linux_amd64 linux_arm64

linux_amd64:
	@echo "Building for linux_amd64 using nature build..."
	mkdir -p build/linux_amd64/bin
	BUILD_OS=linux BUILD_ARCH=amd64 nature build -o build/linux_amd64/bin/parker parker.n
	BUILD_OS=linux BUILD_ARCH=amd64 nature build -o build/linux_amd64/bin/runner runner.n
	@make copy-version-linux_amd64

linux_arm64:
	@echo "Building for linux_arm64 using go build..."
	mkdir -p build/linux_arm64/bin
	GOOS=linux GOARCH=arm64 go build -o build/linux_arm64/bin/parker parkergo/main.go
	GOOS=linux GOARCH=arm64 go build -o build/linux_arm64/bin/runner parkergo/runner/main.go
	@make copy-version-linux_arm64



install:
ifndef TARGET
	@echo "Please provide TARGET as an argument. E.g: make install TARGET=linux_amd64"
	@exit 1
endif
	@echo "Installing for $(TARGET)..."
	mkdir -p /usr/local/parker
	cp -r build/$(TARGET)/* /usr/local/parker/

package:

copy-version-linux_amd64:
	@echo "Copying VERSION to build/linux_amd64..."
	cp VERSION build/linux_amd64/

copy-version-linux_arm64:
	@echo "Copying VERSION to build/linux_arm64..."
	cp VERSION build/linux_arm64/
