package main

import (
	"crypto/ed25519"
	"fmt"
	"os"
)

func main() {
	seed, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	if len(seed) != 32 {
		fmt.Fprintf(os.Stderr, "Error: seed must be 32 bytes, got %d\n", len(seed))
		os.Exit(1)
	}
	priv := ed25519.NewKeyFromSeed(seed)
	pub := priv.Public().(ed25519.PublicKey)

	// Print as C array
	fmt.Printf("// Ed25519 public key (derived from server seed)\nstatic const uint8_t key[32] = {")
	for i, b := range pub {
		if i > 0 {
			fmt.Print(",")
		}
		if i%11 == 0 {
			fmt.Print("\n    ")
		} else {
			fmt.Print(" ")
		}
		fmt.Printf("0x%02x", b)
	}
	fmt.Println("\n};")

	// Also print hex for reference
	fmt.Printf("\n// Hex: ")
	for _, b := range pub {
		fmt.Printf("%02x", b)
	}
	fmt.Println()
}
