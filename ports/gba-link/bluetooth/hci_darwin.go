package main

import "errors"

func controllerCommand(_ uint16, _ []byte) error {
	return errors.New("legacy HCI commands are only supported on Brick Linux")
}
