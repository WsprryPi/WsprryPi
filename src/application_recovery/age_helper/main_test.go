// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"strings"
	"testing"

	"filippo.io/age"
)

func TestRoundTripAndMaintainedReader(t *testing.T) {
	content := bytes.Repeat([]byte("private synthetic recovery record\x00"), 5000)
	var encrypted bytes.Buffer
	if err := transform("encrypt", "synthetic long test passphrase", bytes.NewReader(content), &encrypted); err != nil {
		t.Fatal("encryption failed")
	}
	ciphertext := bytes.Clone(encrypted.Bytes())
	var decoded bytes.Buffer
	if err := transform("decrypt", "synthetic long test passphrase", bytes.NewReader(ciphertext), &decoded); err != nil {
		t.Fatal("decryption failed")
	}
	if !bytes.Equal(content, decoded.Bytes()) {
		t.Fatal("round trip changed bytes")
	}
	identity, _ := age.NewScryptIdentity("synthetic long test passphrase")
	identity.SetMaxWorkFactor(15)
	reader, err := age.Decrypt(bytes.NewReader(ciphertext), identity)
	if err != nil {
		t.Fatal("maintained reader rejected archive")
	}
	plain, err := io.ReadAll(reader)
	if err != nil || !bytes.Equal(plain, content) {
		t.Fatal("maintained reader changed bytes")
	}
	for _, mutation := range []string{"wrong-passphrase", "truncated", "changed-tag", "changed-header"} {
		t.Run(mutation, func(t *testing.T) {
			input := bytes.Clone(ciphertext)
			passphrase := "synthetic long test passphrase"
			switch mutation {
			case "wrong-passphrase":
				passphrase = "different test passphrase"
			case "truncated":
				input = input[:len(input)-1]
			case "changed-tag":
				input[len(input)-1] ^= 1
			case "changed-header":
				input[40] ^= 1
			}
			if transform("decrypt", passphrase, bytes.NewReader(input), io.Discard) == nil {
				t.Fatal("invalid archive accepted")
			}
		})
	}
}

func TestHeaderResourceRefusals(t *testing.T) {
	for _, header := range []string{
		strings.Repeat("x", 4096),
		"age-encryption.org/v2\n",
		"age-encryption.org/v1\n-> scrypt salt 30\n",
		"age-encryption.org/v1\n-> scrypt salt 18\n",
		"age-encryption.org/v1\n-> X25519 key\n",
		"age-encryption.org/v1\n-> scrypt salt 015\n",
		"age-encryption.org/v1\n-> scrypt salt 15 extra\n",
	} {
		if _, err := boundedHeader(strings.NewReader(header)); err == nil {
			t.Fatal("unbounded or unsupported profile accepted")
		}
	}
}

func TestSecretFraming(t *testing.T) {
	frame := func(length uint32, value []byte) []byte {
		var out bytes.Buffer
		_ = binary.Write(&out, binary.BigEndian, length)
		out.Write(value)
		return out.Bytes()
	}
	for _, bad := range [][]byte{nil, frame(0, nil), frame(1025, nil), frame(4, []byte("abc")), frame(3, []byte("abcd"))} {
		if _, err := secret(bytes.NewReader(bad)); err == nil {
			t.Fatal("malformed secret accepted")
		}
	}
	if value, err := secret(bytes.NewReader(frame(3, []byte("abc")))); err != nil || value != "abc" {
		t.Fatal("valid secret frame refused")
	}
}
