package cmd

import (
	"fmt"
	"github.com/spf13/cobra"
	"os"
	"os/signal"
	"rungo/src"
)

func init() {
	rootCmd.AddCommand(exampleCmd)
	//rootCmd.Flags().BoolVarP(&src.Verbose, "verbose", "v", false, "verbose output")
	// 读取环境变量 PARKER_VERBOSE 进行配置
}

const (
	Version = "0.1.0"
)

var exampleCmd = &cobra.Command{
	Use:   "example",
	Short: "",
	Run: func(cmd *cobra.Command, args []string) {
		// example
		println("example")
	},
}

var rootCmd = &cobra.Command{
	Version: Version,
	Run: func(cmd *cobra.Command, args []string) {

		c := make(chan os.Signal)
		// 没有参数表示监听所有信号
		signal.Notify(c)

		src.Run(c)
	},
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}
