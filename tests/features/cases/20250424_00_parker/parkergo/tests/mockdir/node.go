package main

import (
	"fmt"
	"os"
	"strconv"
	"time"
)

func main() {
	count := 10

	str, exists := os.LookupEnv("REPEAT_COUNT")
	if exists {
		temp, err := strconv.Atoi(str)
		if err == nil && temp > 0 {
			fmt.Printf("read env count success: %d\n", temp)
			count = temp
		}
	}

	for i := 0; i < count; i++ {
		fmt.Printf("%d hello world\n", i)
		time.Sleep(1 * time.Second)
	}
}
