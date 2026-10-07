package main

import (
	"os/exec"
	"syscall"
)

func hotspotChild(cmd *exec.Cmd) { cmd.SysProcAttr = &syscall.SysProcAttr{Pdeathsig: syscall.SIGTERM} }
