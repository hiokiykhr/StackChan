package service

import "testing"

func TestAllowedUploadDirsPreserveClientCompatibilityAliases(t *testing.T) {
	cases := map[string]string{
		"":        defaultUploadDir,
		"uploads": defaultUploadDir,
		"moments": "dance",
		"posts":   "post",
		"apps":    "app",
	}
	for input, want := range cases {
		got, ok := allowedUploadDirs[input]
		if !ok {
			t.Fatalf("allowedUploadDirs[%q] is missing", input)
		}
		if got != want {
			t.Fatalf("allowedUploadDirs[%q] = %q, want %q", input, got, want)
		}
	}
}
