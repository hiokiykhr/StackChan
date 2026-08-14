/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	v1 "stackChan/api/file/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/net/ghttp"
)

const (
	baseFileDir       = "file"
	defaultUploadDir  = "uploads"
	maxUploadFileSize = 5 * 1024 * 1024
)

var (
	allowedUploadDirs = map[string]string{
		"":        defaultUploadDir,
		"uploads": defaultUploadDir,
		"avatar":  "avatar",
		"post":    "post",
		"dance":   "dance",
		"pano":    "pano",
		"device":  "device",
		"app":     "app",
		"tmp":     "tmp",
	}
	allowedUploadExt = map[string][]string{
		".jpg":  {"image/jpeg"},
		".jpeg": {"image/jpeg"},
		".png":  {"image/png"},
		".gif":  {"image/gif"},
		".webp": {"image/webp"},
		".json": {"application/json", "text/plain"},
		".txt":  {"text/plain"},
		".mp3":  {"audio/mpeg", "application/octet-stream"},
		".wav":  {"audio/wav", "audio/x-wav", "application/octet-stream"},
		".bin":  {"application/octet-stream"},
	}
)

func AddFile(ctx context.Context, req *v1.FileReq) (res *v1.FileRes, err error) {
	currentDir, err := os.Getwd()
	if err != nil {
		return nil, err
	}
	path, err := saveUploadedFile(currentDir, req.Directory, req.Name, req.File)
	if err != nil {
		return nil, err
	}
	return &v1.FileRes{Path: path}, nil
}

func saveUploadedFile(currentDir, directory, name string, upload *ghttp.UploadFile) (string, error) {
	if upload == nil || upload.Size == 0 || name == "" {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "file or filename is empty")
	}
	purpose, ok := allowedUploadDirs[directory]
	if !ok {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "upload directory is not allowed")
	}
	if filepath.Base(name) != name || filepath.IsAbs(name) || strings.ContainsAny(name, "\x00/\\") {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "invalid filename")
	}
	ext := strings.ToLower(filepath.Ext(name))
	allowedMimes, ok := allowedUploadExt[ext]
	if !ok {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "file extension is not allowed")
	}

	rootAbs, err := filepath.Abs(filepath.Join(currentDir, baseFileDir))
	if err != nil {
		return "", err
	}
	targetDir := filepath.Join(rootAbs, purpose)
	if !pathInsideRoot(rootAbs, targetDir) {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "invalid upload directory")
	}
	if err := os.MkdirAll(targetDir, 0700); err != nil {
		return "", err
	}
	if evalDir, err := filepath.EvalSymlinks(targetDir); err != nil || !pathInsideRoot(rootAbs, evalDir) {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "invalid upload directory")
	}

	src, err := upload.Open()
	if err != nil {
		return "", err
	}
	defer src.Close()

	finalName, err := randomFileName(ext)
	if err != nil {
		return "", err
	}
	finalPath := filepath.Join(targetDir, finalName)
	if !pathInsideRoot(rootAbs, finalPath) {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "invalid upload path")
	}
	tmpPath := finalPath + ".tmp"
	tmpFile, err := os.OpenFile(tmpPath, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return "", err
	}
	committed := false
	defer func() {
		_ = tmpFile.Close()
		if !committed {
			_ = os.Remove(tmpPath)
		}
	}()

	limited := io.LimitReader(src, maxUploadFileSize+1)
	written, err := io.Copy(tmpFile, limited)
	if err != nil {
		return "", err
	}
	if written > maxUploadFileSize {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "file too large")
	}
	if written == 0 {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "file is empty")
	}
	if err := tmpFile.Close(); err != nil {
		return "", err
	}
	if err := validateUploadedFileSignature(tmpPath, allowedMimes); err != nil {
		return "", err
	}
	if err := os.Rename(tmpPath, finalPath); err != nil {
		return "", err
	}
	committed = true
	return filepath.ToSlash(filepath.Join(baseFileDir, purpose, finalName)), nil
}

func validateUploadedFileSignature(path string, allowed []string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	buf := make([]byte, 512)
	n, err := file.Read(buf)
	if err != nil && !errors.Is(err, io.EOF) {
		return err
	}
	detected := http.DetectContentType(buf[:n])
	for _, allowedMime := range allowed {
		if detected == allowedMime || strings.HasPrefix(detected, allowedMime+";") {
			return nil
		}
	}
	return gerror.NewCodef(gcode.CodeInvalidParameter, "file content type is not allowed: %s", detected)
}

func randomFileName(ext string) (string, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return fmt.Sprintf("%s%s", hex.EncodeToString(b[:]), ext), nil
}

func pathInsideRoot(rootAbs, target string) bool {
	targetAbs, err := filepath.Abs(target)
	if err != nil {
		return false
	}
	rel, err := filepath.Rel(rootAbs, targetAbs)
	if err != nil {
		return false
	}
	return rel == "." || (rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator)))
}
