//go:build !windows

// SPDX-License-Identifier: MIT
package main

import (
	"errors"
	"os"
)

func secretChannel(args []string) (*os.File, error) {
	if len(args) != 1 {
		return nil, errors.New("unsupported invocation")
	}
	descriptor := os.NewFile(3, "passphrase-pipe")
	if descriptor == nil {
		return nil, errors.New("missing secret descriptor")
	}
	return descriptor, nil
}
