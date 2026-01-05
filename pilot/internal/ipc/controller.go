// Package ipc provides inter-process communication with the C++ harness.
// It manages the harness subprocess and handles the JSON telemetry protocol.
package ipc

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os/exec"
	"sync"
	"time"
)

// Command represents commands sent to the harness
type Command string

const (
	CmdStart  Command = "START"
	CmdStop   Command = "STOP"
	CmdPause  Command = "PAUSE"
	CmdResume Command = "RESUME"
	CmdStatus Command = "STATUS"
	CmdKill   Command = "KILL"
)

// EventType represents the type of telemetry event from the harness
type EventType string

const (
	EventStatus    EventType = "status"
	EventText      EventType = "txt"
	EventLevel     EventType = "level"
	EventError     EventType = "err"
	EventInfo      EventType = "info"
	EventSession   EventType = "session"
	EventHeartbeat EventType = "heartbeat"
)

// TelemetryEvent represents a JSON message from the harness
type TelemetryEvent struct {
	Event     EventType `json:"evt"`
	State     string    `json:"state,omitempty"`
	Body      string    `json:"body,omitempty"`
	DB        float64   `json:"db,omitempty"`
	Time      int64     `json:"time,omitempty"`
	Timestamp int64     `json:"ts,omitempty"`
	
	// Session events
	Action   string `json:"action,omitempty"`
	ID       string `json:"id,omitempty"`
	Path     string `json:"path,omitempty"`
	Bytes    int64  `json:"bytes,omitempty"`
	Duration int64  `json:"duration,omitempty"`
}

// EventHandler is a callback for telemetry events
type EventHandler func(event TelemetryEvent)

// Controller manages the harness subprocess and IPC
type Controller struct {
	binaryPath string
	outputDir  string
	
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout io.ReadCloser
	stderr io.ReadCloser
	
	mu         sync.RWMutex
	state      string
	recording  bool
	sessionID  string
	lastLevel  float64
	
	handlers   []EventHandler
	handlersMu sync.RWMutex
	
	done chan struct{}
}

// NewController creates a new harness controller
func NewController(binaryPath string) *Controller {
	return &Controller{
		binaryPath: binaryPath,
		state:      "idle",
		done:       make(chan struct{}),
	}
}

// SetOutputDir sets the output directory for recordings
func (c *Controller) SetOutputDir(dir string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.outputDir = dir
}

// OnEvent registers a handler for telemetry events
func (c *Controller) OnEvent(handler EventHandler) {
	c.handlersMu.Lock()
	defer c.handlersMu.Unlock()
	c.handlers = append(c.handlers, handler)
}

// Start launches the harness subprocess
func (c *Controller) Start() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	c.cmd = exec.Command(c.binaryPath, "-v")
	
	var err error
	c.stdin, err = c.cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("failed to get stdin pipe: %w", err)
	}
	
	c.stdout, err = c.cmd.StdoutPipe()
	if err != nil {
		return fmt.Errorf("failed to get stdout pipe: %w", err)
	}
	
	c.stderr, err = c.cmd.StderrPipe()
	if err != nil {
		return fmt.Errorf("failed to get stderr pipe: %w", err)
	}
	
	if err := c.cmd.Start(); err != nil {
		return fmt.Errorf("failed to start harness: %w", err)
	}
	
	// Start the telemetry listener
	go c.listenTelemetry()
	go c.logStderr()
	
	return nil
}

// listenTelemetry reads and processes JSON telemetry from stdout
func (c *Controller) listenTelemetry() {
	scanner := bufio.NewScanner(c.stdout)
	
	for scanner.Scan() {
		select {
		case <-c.done:
			return
		default:
		}
		
		line := scanner.Text()
		if line == "" {
			continue
		}
		
		var event TelemetryEvent
		if err := json.Unmarshal([]byte(line), &event); err != nil {
			// Log but don't crash on malformed JSON
			continue
		}
		
		// Update internal state
		c.processEvent(event)
		
		// Notify handlers
		c.handlersMu.RLock()
		for _, handler := range c.handlers {
			handler(event)
		}
		c.handlersMu.RUnlock()
	}
}

// logStderr logs stderr output from the harness
func (c *Controller) logStderr() {
	scanner := bufio.NewScanner(c.stderr)
	for scanner.Scan() {
		line := scanner.Text()
		if line != "" {
			log.Printf("[harness] %s", line)
		}
	}
}

// processEvent updates internal state based on events
func (c *Controller) processEvent(event TelemetryEvent) {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	switch event.Event {
	case EventStatus:
		c.state = event.State
		c.recording = (event.State == "recording")
		
	case EventLevel:
		c.lastLevel = event.DB
		
	case EventSession:
		if event.Action == "start" {
			c.sessionID = event.ID
		} else if event.Action == "end" {
			c.sessionID = ""
		}
	}
}

// sendCommand sends a command to the harness
func (c *Controller) sendCommand(cmd Command, args ...string) error {
	c.mu.RLock()
	stdin := c.stdin
	c.mu.RUnlock()
	
	if stdin == nil {
		return fmt.Errorf("harness not running")
	}
	
	cmdStr := string(cmd)
	for _, arg := range args {
		cmdStr += " " + arg
	}
	cmdStr += "\n"
	
	_, err := io.WriteString(stdin, cmdStr)
	return err
}

// StartRecording begins a recording session
func (c *Controller) StartRecording() error {
	c.mu.RLock()
	outputDir := c.outputDir
	c.mu.RUnlock()
	
	if outputDir != "" {
		return c.sendCommand(CmdStart, outputDir)
	}
	return c.sendCommand(CmdStart)
}

// Stop stops the current recording
func (c *Controller) Stop() error {
	return c.sendCommand(CmdStop)
}

// Pause pauses the current recording
func (c *Controller) Pause() error {
	return c.sendCommand(CmdPause)
}

// Resume resumes a paused recording
func (c *Controller) Resume() error {
	return c.sendCommand(CmdResume)
}

// Status requests current status from the harness
func (c *Controller) Status() error {
	return c.sendCommand(CmdStatus)
}

// Terminate kills the harness process
func (c *Controller) Terminate() {
	close(c.done)
	
	if err := c.sendCommand(CmdKill); err != nil {
		// Force kill if command fails
		c.mu.RLock()
		cmd := c.cmd
		c.mu.RUnlock()
		
		if cmd != nil && cmd.Process != nil {
			cmd.Process.Kill()
		}
	}
	
	// Wait for process to exit (with timeout)
	c.mu.RLock()
	cmd := c.cmd
	c.mu.RUnlock()
	
	if cmd != nil {
		done := make(chan error, 1)
		go func() {
			done <- cmd.Wait()
		}()
		
		select {
		case <-done:
		case <-time.After(3 * time.Second):
			if cmd.Process != nil {
				cmd.Process.Kill()
			}
		}
	}
}

// IsRecording returns whether recording is active
func (c *Controller) IsRecording() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.recording
}

// State returns the current harness state
func (c *Controller) State() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.state
}

// LastLevel returns the last audio level in dB
func (c *Controller) LastLevel() float64 {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.lastLevel
}

// SessionID returns the current session ID
func (c *Controller) SessionID() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.sessionID
}
