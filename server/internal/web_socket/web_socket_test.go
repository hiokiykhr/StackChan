package web_socket

import (
	"context"
	"encoding/binary"
	"testing"
	"time"
)

func TestValidateAuthTokenRejectsMalformedWithoutPanic(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	cases := []string{
		"",
		"AABBCCDDEEFF|nonce",
		"AABBCCDDEEFF||1700000000",
		"not-a-mac|nonce|1700000000",
		"AABBCCDDEEFF|nonce|badtime",
		"AABBCCDDEEFF|nonce|999999999999999999999",
		"AABBCCDDEEFF|nonce|1699990000",
		"AABBCCDDEEFF|nonce|1700000000|extra",
	}
	for _, tc := range cases {
		t.Run(tc, func(t *testing.T) {
			if mac, err := validateAuthToken(tc, now); err == nil {
				t.Fatalf("expected error, got mac %q", mac)
			}
		})
	}
}

func TestValidateAuthTokenAcceptsStrictFormat(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	mac, err := validateAuthToken("AA:BB:CC:DD:EE:FF|nonce|1700000000", now)
	if err != nil {
		t.Fatalf("validateAuthToken returned error: %v", err)
	}
	if mac != "AA:BB:CC:DD:EE:FF" {
		t.Fatalf("got mac %q", mac)
	}
}

func TestParseBinaryMessageRejectsMalformedLengthsWithoutPanic(t *testing.T) {
	cases := [][]byte{
		{},
		{TextMessage, 0, 0, 0},
		{TextMessage, 0, 0, 0, 10, 'x'},
		{TextMessage, 0, 0, 0, 1, 'x', 'y'},
	}
	for _, tc := range cases {
		t.Run("case", func(t *testing.T) {
			if _, _, _, ok := parseBinaryMessage(context.Background(), &tc); ok {
				t.Fatalf("expected malformed frame to be rejected")
			}
		})
	}
}

func TestParseBinaryMessageAcceptsExactLength(t *testing.T) {
	msg := make([]byte, 5+3)
	msg[0] = TextMessage
	binary.BigEndian.PutUint32(msg[1:5], 3)
	copy(msg[5:], []byte("hey"))
	msgType, dataLen, payload, ok := parseBinaryMessage(context.Background(), &msg)
	if !ok {
		t.Fatal("expected valid frame")
	}
	if msgType != TextMessage || dataLen != 3 || string(payload) != "hey" {
		t.Fatalf("unexpected parse result: type=%d len=%d payload=%q", msgType, dataLen, payload)
	}
}

func FuzzParseBinaryMessage(f *testing.F) {
	f.Add([]byte{})
	f.Add([]byte{TextMessage, 0, 0, 0, 10, 'x'})
	f.Add([]byte{TextMessage, 0, 0, 0, 1, 'x'})
	f.Fuzz(func(t *testing.T, data []byte) {
		parseBinaryMessage(context.Background(), &data)
	})
}
