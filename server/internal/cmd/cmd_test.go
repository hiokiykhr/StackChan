package cmd

import (
	"os"
	"path/filepath"
	"testing"
)

func TestSafeStaticFilePathAllowsFileUnderRoot(t *testing.T) {
	root := t.TempDir()
	want := filepath.Join(root, "ok.txt")
	if err := os.WriteFile(want, []byte("ok"), 0600); err != nil {
		t.Fatal(err)
	}
	got, err := safeStaticFilePath(root, "ok.txt")
	if err != nil {
		t.Fatalf("safeStaticFilePath returned error: %v", err)
	}
	wantEval, err := filepath.EvalSymlinks(want)
	if err != nil {
		t.Fatal(err)
	}
	if got != wantEval {
		t.Fatalf("got %q, want %q", got, wantEval)
	}
}

func TestSafeStaticFilePathRejectsTraversal(t *testing.T) {
	root := t.TempDir()
	outside := filepath.Join(filepath.Dir(root), "secret.txt")
	if err := os.WriteFile(outside, []byte("secret"), 0600); err != nil {
		t.Fatal(err)
	}
	cases := []string{
		"../secret.txt",
		"..%2fsecret.txt",
		"..%252fsecret.txt",
		filepath.Join(filepath.Dir(root), "secret.txt"),
		"%2e%2e/secret.txt",
		"safe/..%2f..%2fsecret.txt",
	}
	for _, tc := range cases {
		t.Run(tc, func(t *testing.T) {
			if got, err := safeStaticFilePath(root, tc); err == nil {
				t.Fatalf("expected rejection, got %q", got)
			}
		})
	}
}

func TestSafeStaticFilePathRejectsSymlinkEscape(t *testing.T) {
	root := t.TempDir()
	outside := filepath.Join(filepath.Dir(root), "secret.txt")
	if err := os.WriteFile(outside, []byte("secret"), 0600); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(root, "link.txt")
	if err := os.Symlink(outside, link); err != nil {
		t.Skipf("symlink not supported: %v", err)
	}
	if got, err := safeStaticFilePath(root, "link.txt"); err == nil {
		t.Fatalf("expected symlink escape rejection, got %q", got)
	}
}

func TestSafeStaticFilePathRejectsNonRegularFile(t *testing.T) {
	root := t.TempDir()
	if err := os.Mkdir(filepath.Join(root, "dir"), 0700); err != nil {
		t.Fatal(err)
	}
	if got, err := safeStaticFilePath(root, "dir"); err == nil {
		t.Fatalf("expected directory rejection, got %q", got)
	}
}
