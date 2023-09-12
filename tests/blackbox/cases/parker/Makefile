.PHONY: all linux_amd64 linux_arm64 copy-version-linux_amd64 copy-version-linux_arm64

all: linux_amd64 linux_arm64

linux_amd64:
	@echo "Building for linux_amd64 using go build..."
	mkdir -p build/linux_amd64/bin
	@echo "GOOS=linux GOARCH=amd64 go build -o build/linux_amd64/bin/parker parkergo/main.go"
	GOOS=linux GOARCH=amd64 go build -o build/linux_amd64/bin/parker parkergo/main.go
	@echo "GOOS=linux GOARCH=amd64 go build -o build/linux_amd64/bin/runner parkergo/runner/main.go"
	GOOS=linux GOARCH=amd64 go build -o build/linux_amd64/bin/runner parkergo/runner/main.go
	@make copy-version-linux_amd64

linux_arm64:
	@echo "Building for linux_arm64 using nature build..."
	mkdir -p build/linux_arm64/bin
	@echo "BUILD_OS=linux BUILD_ARCH=arm64 nature build -o build/linux_arm64/bin/parker parker.go"
	BUILD_OS=linux BUILD_ARCH=arm64 nature build -o build/linux_arm64/bin/parker parker.go
	@echo "BUILD_OS=linux BUILD_ARCH=arm64 nature build -o build/linux_arm64/bin/runner runner.go"
	BUILD_OS=linux BUILD_ARCH=arm64 nature build -o build/linux_arm64/bin/runner runner.go
	@make copy-version-linux_arm64

copy-version-linux_amd64:
	@echo "Copying VERSION to build/linux_amd64..."
	cp VERSION build/linux_amd64/

copy-version-linux_arm64:
	@echo "Copying VERSION to build/linux_arm64..."
	cp VERSION build/linux_arm64/
