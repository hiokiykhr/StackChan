/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package cmd

import (
	"context"
	"errors"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"

	"stackChan/internal/boot"
	"stackChan/internal/controller/admin"
	"stackChan/internal/controller/appstore"
	"stackChan/internal/controller/dance"
	"stackChan/internal/controller/device"
	"stackChan/internal/controller/file"
	"stackChan/internal/controller/friend"
	"stackChan/internal/controller/pano"
	"stackChan/internal/controller/post"
	"stackChan/internal/controller/stackchandevice"
	"stackChan/internal/controller/user"
	"stackChan/internal/controller/xiaozhi"
	"stackChan/internal/middleware"
	"stackChan/internal/web_socket"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/ghttp"
	"github.com/gogf/gf/v2/os/gcmd"
)

var errInvalidStaticPath = errors.New("invalid static file path")

func safeStaticFilePath(rootDir, rawPath string) (string, error) {
	if rawPath == "" || strings.ContainsRune(rawPath, '\x00') {
		return "", errInvalidStaticPath
	}
	decoded := rawPath
	for i := 0; i < 2; i++ {
		unescaped, err := url.PathUnescape(decoded)
		if err != nil {
			return "", errInvalidStaticPath
		}
		if unescaped == decoded {
			break
		}
		decoded = unescaped
	}
	if strings.Contains(decoded, "%") || strings.Contains(decoded, "\\") || filepath.IsAbs(decoded) {
		return "", errInvalidStaticPath
	}
	for _, part := range strings.Split(decoded, "/") {
		if part == ".." || strings.ContainsRune(part, '\x00') {
			return "", errInvalidStaticPath
		}
	}

	rootAbs, err := filepath.Abs(rootDir)
	if err != nil {
		return "", err
	}
	rootEval, err := filepath.EvalSymlinks(rootAbs)
	if err != nil {
		return "", err
	}
	targetAbs, err := filepath.Abs(filepath.Join(rootEval, filepath.Clean(decoded)))
	if err != nil {
		return "", errInvalidStaticPath
	}
	if !pathInside(rootEval, targetAbs) {
		return "", errInvalidStaticPath
	}
	targetEval, err := filepath.EvalSymlinks(targetAbs)
	if err != nil {
		return "", err
	}
	if !pathInside(rootEval, targetEval) {
		return "", errInvalidStaticPath
	}
	info, err := os.Stat(targetEval)
	if err != nil {
		return "", err
	}
	if !info.Mode().IsRegular() {
		return "", errInvalidStaticPath
	}
	return targetEval, nil
}

func pathInside(rootAbs, targetAbs string) bool {
	rel, err := filepath.Rel(rootAbs, targetAbs)
	if err != nil {
		return false
	}
	return rel == "." || (rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator)))
}

var (
	Main = gcmd.Command{
		Name:  "main",
		Usage: "main",
		Brief: "start http server",
		Func: func(ctx context.Context, parser *gcmd.Parser) (err error) {
			s := g.Server()
			s.SetClientMaxBodySize(100 * 1024 * 1024)

			s.Use(middleware.CORS)

			s.BindHandler("/stackChan/ws", web_socket.Handler)

			// heartBeat
			boot.InitCron()

			///Configuration file access
			s.Group("/file", func(group *ghttp.RouterGroup) {
				group.GET("/*filepath", func(r *ghttp.Request) {
					relativePath := r.Get("filepath").String()
					if relativePath == "" {
						r.Response.WriteHeader(http.StatusNotFound)
						r.Response.Write("File not found")
						return
					}
					filePath, err := safeStaticFilePath("file", relativePath)
					if err != nil {
						r.Response.WriteHeader(http.StatusNotFound)
						r.Response.Write("File not found")
						return
					}
					r.Response.ServeFile(filePath)
				})
			})

			s.Group("/stackChan/v2", func(group *ghttp.RouterGroup) {
				group.Middleware(middleware.V2TokenAuthMiddleware, ghttp.MiddlewareHandlerResponse)
				group.Bind(user.NewV2(), dance.NewV2(), device.NewV2())
			})

			s.Group("/stackChan", func(group *ghttp.RouterGroup) {
				group.Middleware(middleware.TokenAuthMiddleware, ghttp.MiddlewareHandlerResponse)
				group.Bind(device.NewV1(), friend.NewV1(), dance.NewV1(), file.NewV1(), post.NewV1(), pano.NewV1(), appstore.NewV1(), xiaozhi.NewV1(), stackchandevice.NewV2())
			})

			s.Group("/admin/stackChan", func(group *ghttp.RouterGroup) {
				group.Middleware(middleware.AdminTokenAuthMiddleware, ghttp.MiddlewareHandlerResponse)
				group.Bind(admin.NewV1(), file.NewV1())
			})

			// Do not fail API/server smoke tests when the optional built admin console is absent.
			if stat, statErr := os.Stat("web/management"); statErr == nil && stat.IsDir() {
				s.SetServerRoot("web/management")
			}

			s.SetPort(12800)
			s.Run()
			return nil
		},
	}
)
