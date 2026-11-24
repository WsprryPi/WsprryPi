// SPDX-License-Identifier: MIT
// Application-private age v1 adapter. Secrets enter only through a private pipe;
// archive bytes use stdin/stdout. The caller owns private staging/publication.
package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"filippo.io/age"
)

const workFactor = 15                            // scrypt N=32768, r=8, p=1: approximately 32 MiB.
const maximumBytes int64 = (2 << 30) - (2 << 20) // Leave room for age framing within the 2 GiB upload bound.
const maximumPassphrase = 1024

func secret(input io.Reader) (string, error) {
	var length uint32
	if err := binary.Read(input, binary.BigEndian, &length); err != nil {
		return "", err
	}
	if length == 0 || length > maximumPassphrase {
		return "", errors.New("invalid secret frame")
	}
	value := make([]byte, int(length))
	defer clear(value)
	if _, err := io.ReadFull(input, value); err != nil {
		return "", err
	}
	var extra [1]byte
	if n, err := input.Read(extra[:]); n != 0 || err != io.EOF {
		return "", errors.New("extra secret bytes")
	}
	return string(value), nil
}

// Reject unbounded recipient/header parsing before invoking the maintained
// parser or allocating scrypt memory. This helper supports one fixed recovery
// profile, not arbitrary age recipient documents.
func boundedHeader(input io.Reader) (io.Reader, error) {
	reader := bufio.NewReaderSize(input, 1024)
	var header bytes.Buffer
	for index := 0; index < 4; index++ {
		line, err := reader.ReadSlice('\n')
		if err != nil || len(line) > 1024 {
			return nil, errors.New("invalid age header")
		}
		switch index {
		case 0:
			if string(line) != "age-encryption.org/v1\n" {
				return nil, errors.New("unsupported age version")
			}
		case 1:
			fields := strings.Fields(string(line))
			if len(fields) != 4 || fields[0] != "->" || fields[1] != "scrypt" || fields[3] != "15" {
				return nil, errors.New("unsupported age profile")
			}
		case 2:
			if len(line) != 44 {
				return nil, errors.New("invalid wrapped key")
			}
		case 3:
			if !bytes.HasPrefix(line, []byte("--- ")) || len(line) != 48 {
				return nil, errors.New("invalid header authenticator")
			}
		}
		header.Write(line)
	}
	return io.MultiReader(bytes.NewReader(header.Bytes()), reader), nil
}

func copyBounded(output io.Writer, input io.Reader) error {
	count, err := io.CopyBuffer(output, io.LimitReader(input, maximumBytes+1), make([]byte, 64*1024))
	if err != nil {
		return err
	}
	if count > maximumBytes {
		return errors.New("archive exceeds bound")
	}
	return nil
}

func transform(action, passphrase string, input io.Reader, output io.Writer) error {
	if action == "encrypt" {
		recipient, err := age.NewScryptRecipient(passphrase)
		if err != nil {
			return err
		}
		recipient.SetWorkFactor(workFactor)
		encrypted, err := age.Encrypt(output, recipient)
		if err != nil {
			return err
		}
		if err := copyBounded(encrypted, input); err != nil {
			return err
		}
		return encrypted.Close()
	}
	if action != "decrypt" {
		return errors.New("unsupported action")
	}
	bounded, err := boundedHeader(input)
	if err != nil {
		return err
	}
	identity, err := age.NewScryptIdentity(passphrase)
	if err != nil {
		return err
	}
	identity.SetMaxWorkFactor(workFactor)
	decrypted, err := age.Decrypt(bounded, identity)
	if err != nil {
		return err
	}
	return copyBounded(output, decrypted)
}

func run(args []string) error {
	if len(args) == 0 || (args[0] != "encrypt" && args[0] != "decrypt") {
		return errors.New("unsupported invocation")
	}
	descriptor, err := secretChannel(args)
	if err != nil {
		return err
	}
	defer descriptor.Close()
	passphrase, err := secret(descriptor)
	if err != nil {
		return err
	}
	defer func() { passphrase = "" }()
	return transform(args[0], passphrase, os.Stdin, os.Stdout)
}

func main() {
	if err := run(os.Args[1:]); err != nil {
		// Never relay parser/provider errors containing untrusted archive metadata.
		fmt.Fprintln(os.Stderr, "Recovery encryption failed; archive, passphrase, or resource profile is invalid.")
		os.Exit(2)
	}
}
