package cmd

import (
	"context"
	"fmt"
	"github.com/spf13/cobra"
	"os"
	"rungo/src"
)

func init() {
	rootCmd.AddCommand(exampleCmd)
	rootCmd.Flags().BoolVarP(&src.Verbose, "verbose", "v", false, "verbose output")
}

const (
	Version = "0.1.0-beta"
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
		ctx := context.Background()
		src.Run(ctx)
	},
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}
