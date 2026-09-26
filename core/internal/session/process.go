package session

import (
	"os"
	"path/filepath"
	"strings"
)

func shellEnvironment(c SpawnConfig) (string, []string) {
	env := c.Env
	if env == nil {
		env = os.Environ()
	}
	candidate := c.Shell
	if candidate == "" {
		for _, v := range env {
			if strings.HasPrefix(v, "SHELL=") {
				candidate = strings.TrimPrefix(v, "SHELL=")
			}
		}
	}
	info, e := os.Stat(candidate)
	if !filepath.IsAbs(candidate) || e != nil || !info.Mode().IsRegular() || info.Mode().Perm()&0111 == 0 {
		candidate = "/bin/sh"
	}
	out := make([]string, 0, len(env)+3)
	for _, v := range env {
		if strings.HasPrefix(v, "SHELL=") || strings.HasPrefix(v, "TERM=") || strings.HasPrefix(v, "COLORTERM=") {
			continue
		}
		out = append(out, v)
	}
	return candidate, append(out, "SHELL="+candidate, "TERM=xterm-256color", "COLORTERM=truecolor")
}
