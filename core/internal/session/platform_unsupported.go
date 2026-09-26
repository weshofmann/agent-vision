//go:build !darwin

package session

import (
	"context"
	"errors"
)

type DarwinSpawner struct{}
type DarwinPoller struct{}

func (DarwinSpawner) Spawn(context.Context, SpawnConfig) (*Resources, error) {
	return nil, errors.New("agentvision-core: unsupported platform (Darwin required)")
}
func (DarwinPoller) Wait(context.Context, int, bool) error {
	return errors.New("agentvision-core: unsupported platform (Darwin required)")
}
